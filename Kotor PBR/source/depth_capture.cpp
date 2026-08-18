/*
Depth-buffer capture implementation. See depth_capture.h for overview.
*/

#include "depth_capture.h"
#include "file_logger.h"
#include "iat_hook.h"
#include "glFunctions.h"
#include "gl_state_capture.h"
#include "pbr_tune.h"  // DiagLog
#include "shadow_map.h"
#include "glsl_program.h"
#include <stdio.h>
#include <string.h>
#include <map>

// Enums not present in mingw gl.h.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE        0x812F
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE 0x884C
#endif
#ifndef GL_DEPTH_TEXTURE_MODE
#define GL_DEPTH_TEXTURE_MODE   0x884B
#endif
#ifndef GL_LUMINANCE
#define GL_LUMINANCE            0x1909
#endif
#ifndef GL_NONE
#define GL_NONE                 0
#endif

// ============================================================================
// FBO interceptor state. K2 creates a handful of FBOs at startup (color-only,
// no depth attachment) and never uses them for the scene depth pass. We still
// mirror the state here for diagnostic completeness — it's all silent.
// ============================================================================

static PFNGLGENFRAMEBUFFERS         o_GenFramebuffers         = NULL;
static PFNGLDELETEFRAMEBUFFERS      o_DeleteFramebuffers      = NULL;
static PFNGLBINDFRAMEBUFFER         o_BindFramebuffer         = NULL;
static PFNGLFRAMEBUFFERTEXTURE2D    o_FramebufferTexture2D    = NULL;
static PFNGLFRAMEBUFFERRENDERBUFFER o_FramebufferRenderbuffer = NULL;
static PFNGLGENRENDERBUFFERS        o_GenRenderbuffers        = NULL;
static PFNGLDELETERENDERBUFFERS     o_DeleteRenderbuffers     = NULL;
static PFNGLBINDRENDERBUFFER        o_BindRenderbuffer        = NULL;
static PFNGLRENDERBUFFERSTORAGE     o_RenderbufferStorage     = NULL;

struct FBOInfo {
    GLuint  depthAttachId;
    GLuint  depthAttachKind;  // 0 = renderbuffer, 1 = texture, 2 = depth-stencil tex
    GLuint  colorAttachId;
    GLsizei width, height;
    GLenum  format;
};

struct RBInfo {
    GLenum  format;
    GLsizei w, h;
};

static std::map<GLuint, FBOInfo> g_fbos;
static std::map<GLuint, RBInfo>  g_rbos;
static GLuint g_boundDraw = 0;
static GLuint g_boundRead = 0;
static GLuint g_boundRB   = 0;

// Use find() instead of operator[] — operator[] inserts a default-constructed
// FBOInfo for every unknown id, growing the map with phantom entries.
// Returns a static default that the caller may write into (then insert).
static FBOInfo s_defaultFbo;
static FBOInfo &GetFbo(GLuint id)
{
    std::map<GLuint, FBOInfo>::iterator it = g_fbos.find(id);
    if (it != g_fbos.end()) return it->second;
    s_defaultFbo = FBOInfo();
    return s_defaultFbo;
}

void WINAPI my_glGenFramebuffersEXT(GLsizei n, GLuint *ids)
{
    if (o_GenFramebuffers) o_GenFramebuffers(n, ids);
    if (!ids) return;
    for (GLsizei i = 0; i < n; i++) {
        FBOInfo zero = {0,0,0,0,0,0};
        g_fbos[ids[i]] = zero;
    }
}

void WINAPI my_glDeleteFramebuffersEXT(GLsizei n, const GLuint *ids)
{
    if (ids) for (GLsizei i = 0; i < n; i++) g_fbos.erase(ids[i]);
    if (o_DeleteFramebuffers) o_DeleteFramebuffers(n, ids);
}

void WINAPI my_glBindFramebufferEXT(GLenum target, GLuint fbo)
{
    if (o_BindFramebuffer) o_BindFramebuffer(target, fbo);
    if (target == GL_FRAMEBUFFER_EXT)             { g_boundDraw = fbo; g_boundRead = fbo; }
    else if (target == GL_DRAW_FRAMEBUFFER_EXT)   { g_boundDraw = fbo; }
    else if (target == GL_READ_FRAMEBUFFER_EXT)   { g_boundRead = fbo; }
}

void WINAPI my_glFramebufferTexture2DEXT(GLenum target, GLenum attach,
                                         GLenum texTarget, GLuint tex, GLint level)
{
    if (o_FramebufferTexture2D) o_FramebufferTexture2D(target, attach, texTarget, tex, level);
    GLuint id = (target == GL_READ_FRAMEBUFFER_EXT) ? g_boundRead : g_boundDraw;
    if (id == 0) return;
    FBOInfo &f = GetFbo(id);
    if (attach == GL_DEPTH_ATTACHMENT_EXT)         { f.depthAttachId = tex; f.depthAttachKind = 1; }
    else if (attach == GL_DEPTH_STENCIL_ATTACHMENT){ f.depthAttachId = tex; f.depthAttachKind = 2; }
    else if (attach == GL_COLOR_ATTACHMENT0_EXT)   { f.colorAttachId = tex; }
}

void WINAPI my_glFramebufferRenderbufferEXT(GLenum target, GLenum attach,
                                            GLenum rbTarget, GLuint rb)
{
    if (o_FramebufferRenderbuffer) o_FramebufferRenderbuffer(target, attach, rbTarget, rb);
    GLuint id = (target == GL_READ_FRAMEBUFFER_EXT) ? g_boundRead : g_boundDraw;
    if (id == 0) return;
    FBOInfo &f = GetFbo(id);
    if (attach == GL_DEPTH_ATTACHMENT_EXT || attach == GL_DEPTH_STENCIL_ATTACHMENT) {
        f.depthAttachId   = rb;
        f.depthAttachKind = (attach == GL_DEPTH_STENCIL_ATTACHMENT) ? 2 : 0;
        std::map<GLuint, RBInfo>::iterator it = g_rbos.find(rb);
        if (it != g_rbos.end()) {
            f.width  = it->second.w;
            f.height = it->second.h;
            f.format = it->second.format;
        }
    } else if (attach == GL_COLOR_ATTACHMENT0_EXT) {
        f.colorAttachId = rb;
    }
}

void WINAPI my_glGenRenderbuffersEXT(GLsizei n, GLuint *ids)
{
    if (o_GenRenderbuffers) o_GenRenderbuffers(n, ids);
    if (!ids) return;
    for (GLsizei i = 0; i < n; i++) {
        RBInfo zero = {0,0,0};
        g_rbos[ids[i]] = zero;
    }
}

void WINAPI my_glDeleteRenderbuffersEXT(GLsizei n, const GLuint *ids)
{
    if (ids) for (GLsizei i = 0; i < n; i++) g_rbos.erase(ids[i]);
    if (o_DeleteRenderbuffers) o_DeleteRenderbuffers(n, ids);
}

void WINAPI my_glBindRenderbufferEXT(GLenum target, GLuint rb)
{
    if (o_BindRenderbuffer) o_BindRenderbuffer(target, rb);
    g_boundRB = rb;
}

void WINAPI my_glRenderbufferStorageEXT(GLenum target, GLenum format,
                                        GLsizei w, GLsizei h)
{
    if (o_RenderbufferStorage) o_RenderbufferStorage(target, format, w, h);
    if (g_boundRB == 0) return;
    RBInfo &r = g_rbos[g_boundRB];
    r.format = format;
    r.w = w;
    r.h = h;
    // Backfill any FBO that already attached this RB as depth.
    for (std::map<GLuint, FBOInfo>::iterator it = g_fbos.begin(); it != g_fbos.end(); ++it) {
        if (it->second.depthAttachId == g_boundRB && it->second.depthAttachKind == 0) {
            it->second.width  = w;
            it->second.height = h;
            it->second.format = format;
        }
    }
}

PROC DepthCapture_InterceptProc(const char *name)
{
    struct Entry { const char *name; void *wrapper; void **orig; };
    static const Entry table[] = {
        { "glGenFramebuffersEXT",         (void*)my_glGenFramebuffersEXT,         (void**)&o_GenFramebuffers         },
        { "glGenFramebuffers",            (void*)my_glGenFramebuffersEXT,         (void**)&o_GenFramebuffers         },
        { "glDeleteFramebuffersEXT",      (void*)my_glDeleteFramebuffersEXT,      (void**)&o_DeleteFramebuffers      },
        { "glDeleteFramebuffers",         (void*)my_glDeleteFramebuffersEXT,      (void**)&o_DeleteFramebuffers      },
        { "glBindFramebufferEXT",         (void*)my_glBindFramebufferEXT,         (void**)&o_BindFramebuffer         },
        { "glBindFramebuffer",            (void*)my_glBindFramebufferEXT,         (void**)&o_BindFramebuffer         },
        { "glFramebufferTexture2DEXT",    (void*)my_glFramebufferTexture2DEXT,    (void**)&o_FramebufferTexture2D    },
        { "glFramebufferTexture2D",       (void*)my_glFramebufferTexture2DEXT,    (void**)&o_FramebufferTexture2D    },
        { "glFramebufferRenderbufferEXT", (void*)my_glFramebufferRenderbufferEXT, (void**)&o_FramebufferRenderbuffer },
        { "glFramebufferRenderbuffer",    (void*)my_glFramebufferRenderbufferEXT, (void**)&o_FramebufferRenderbuffer },
        { "glGenRenderbuffersEXT",        (void*)my_glGenRenderbuffersEXT,        (void**)&o_GenRenderbuffers        },
        { "glGenRenderbuffers",           (void*)my_glGenRenderbuffersEXT,        (void**)&o_GenRenderbuffers        },
        { "glDeleteRenderbuffersEXT",     (void*)my_glDeleteRenderbuffersEXT,     (void**)&o_DeleteRenderbuffers     },
        { "glDeleteRenderbuffers",        (void*)my_glDeleteRenderbuffersEXT,     (void**)&o_DeleteRenderbuffers     },
        { "glBindRenderbufferEXT",        (void*)my_glBindRenderbufferEXT,        (void**)&o_BindRenderbuffer        },
        { "glBindRenderbuffer",           (void*)my_glBindRenderbufferEXT,        (void**)&o_BindRenderbuffer        },
        { "glRenderbufferStorageEXT",     (void*)my_glRenderbufferStorageEXT,     (void**)&o_RenderbufferStorage     },
        { "glRenderbufferStorage",        (void*)my_glRenderbufferStorageEXT,     (void**)&o_RenderbufferStorage     },
    };

    // Resolve originals through the *system* wglGetProcAddress, not our
    // __E__355__ wrapper — otherwise recursive lookup of a tracked FBO name
    // would route back here and stack-overflow.
    if (!orig_wglGetProcAddress) return NULL;

    for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if (strcmp(name, table[i].name) == 0) {
            if (*table[i].orig == NULL)
                *table[i].orig = (void*)orig_wglGetProcAddress(table[i].name);
            return (PROC)table[i].wrapper;
        }
    }
    return NULL;
}

// ============================================================================
// Swap hooks (IAT). Aspyr K2 calls gdi32!SwapBuffers in practice; the
// wgl variant is hooked as a fallback.
// ============================================================================

typedef BOOL (WINAPI *PFNSWAPBUFFERS)(HDC);
static PFNSWAPBUFFERS s_origSwapBuffersGdi = NULL;
static PFNSWAPBUFFERS s_origWglSwapBuffers = NULL;

// First-swap latches: GL context guaranteed current here, so this is where
// any subsystem that needs glGet/glProc resolution does its lazy init.
static void OnFirstSwap()
{
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    ShadowMap_Init();
    GlslProbe_Init();   // Stage 0 go/no-go: log GL/SL version + prove GLSL compiles
}

// Shared frame-end work, run from whichever SwapBuffers variant K2 calls.
//   - EndCasterPass: binds shadow tex for next frame's FP samples.
//   - SnapshotDefaultDepth: end-of-frame depth grab for the (disabled) depth
//     subsystem; cheap no-op while off.
//   - PbrTune_OnSwap: overlay draw + env re-push.
static void OnSwapCommon()
{
    OnFirstSwap();
    ShadowMap_EndCasterPass();
    DepthCapture_SnapshotDefaultDepth();
    PbrTune_OnSwap();
    PbrStats_OnSwap();        // FPS + draw call overlay (F11 toggle)
    GlslMaterial_OnFrame();   // reset per-frame GLSL uniform cache for next frame
}

static BOOL WINAPI MySwapBuffersGdi(HDC hdc)
{
    OnSwapCommon();
    if (s_origSwapBuffersGdi) return s_origSwapBuffersGdi(hdc);
    return SwapBuffers(hdc);
}

static BOOL WINAPI MyWglSwapBuffers(HDC hdc)
{
    OnSwapCommon();
    if (s_origWglSwapBuffers) return s_origWglSwapBuffers(hdc);
    return FALSE;
}

void DepthCapture_InstallSwapHook()
{
    HMODULE hExe = GetModuleHandleA(NULL);
    s_origSwapBuffersGdi = (PFNSWAPBUFFERS)IatHook(hExe, "gdi32.dll",     "SwapBuffers",     (FARPROC)MySwapBuffersGdi);
    s_origWglSwapBuffers = (PFNSWAPBUFFERS)IatHook(hExe, "opengl32.dll",  "wglSwapBuffers",  (FARPROC)MyWglSwapBuffers);

    char line[200];
    snprintf(line, sizeof(line),
             "[depth] swap hooks: gdi32!SwapBuffers=%p opengl32!wglSwapBuffers=%p\n",
             (void*)s_origSwapBuffersGdi, (void*)s_origWglSwapBuffers);
    PbrLogLine(line);
}

// ============================================================================
// Default-framebuffer depth → private texture on TMU DEPTH_CAPTURE_TMU.
// ============================================================================

typedef void (WINAPI *PFNGLACTIVETEXTURE)(GLenum);
static PFNGLACTIVETEXTURE s_activeTexture   = NULL;
static GLuint  s_depthTex                   = 0;
static GLsizei s_depthTexW                  = 0;
static GLsizei s_depthTexH                  = 0;
static int     s_snapshotDisabled           = 0;
// SS shadow cut — multi-tap ray-march was too noisy on K2's z-precision and
// throttled snapshots caused temporal ghosting on dynamic geometry. Leave
// snapshot disabled; re-enable if a different depth-driven effect lands.
static bool    s_snapshotEnabled            = false;

static PFNGLACTIVETEXTURE ResolveActiveTexture()
{
    if (s_activeTexture) return s_activeTexture;
    if (!orig_wglGetProcAddress) return NULL;
    s_activeTexture = (PFNGLACTIVETEXTURE)orig_wglGetProcAddress("glActiveTexture");
    if (!s_activeTexture)
        s_activeTexture = (PFNGLACTIVETEXTURE)orig_wglGetProcAddress("glActiveTextureARB");
    return s_activeTexture;
}

void DepthCapture_SnapshotDefaultDepth()
{
    if (!s_snapshotEnabled) return;
    if (s_snapshotDisabled) return;

    PFNGLACTIVETEXTURE activeTexture = ResolveActiveTexture();
    if (!activeTexture) {
        s_snapshotDisabled = 1;
        PbrLogLine("[depth] snapshot: no glActiveTexture, disabling\n");
        DiagLog("[depth] no glActiveTexture - disabled");
        return;
    }
    static int s_diagCount = 0;
    if (s_diagCount < 3) {
        s_diagCount++;
        DiagLog("[depth] snapshot fired #%d", s_diagCount);
    }

    GLint vp[4] = {0,0,0,0};
    glGetIntegerv(GL_VIEWPORT, vp);
    GLsizei w = vp[2];
    GLsizei h = vp[3];
    if (w <= 0 || h <= 0) return;

    // K2 calls SwapBuffers for offscreen renders too (HUD panels, inventory
    // mini-views, etc.). Their viewports are small (e.g. 189x354). We only
    // want main scene depth — skip anything below 1280x720.
    if (w < 1280 || h < 720) return;

    GLint savedActiveTMU = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &savedActiveTMU);

    activeTexture(GL_TEXTURE0 + DEPTH_CAPTURE_TMU);

    if (s_depthTex == 0) {
        glGenTextures(1, &s_depthTex);
        if (s_depthTex == 0) {
            activeTexture(savedActiveTMU);
            s_snapshotDisabled = 1;
            PbrLogLine("[depth] snapshot: glGenTextures failed, disabling\n");
            return;
        }
        glBindTexture(GL_TEXTURE_2D, s_depthTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
        // ARB_fragment_program's TEX op needs depth in .r, not a shadow-
        // compare result.
        glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE,    GL_LUMINANCE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,  GL_NONE);
    } else {
        glBindTexture(GL_TEXTURE_2D, s_depthTex);
    }

    // Throttle: shadows don't need 60Hz depth — every 5th frame (~12Hz at
    // 60fps base) is plenty and keeps the full-Copy cost manageable.
    static int s_frameCount = 0;
    s_frameCount++;
    if ((s_frameCount % 5) != 0) {
        activeTexture(savedActiveTMU);
        return;
    }

    // This driver rejects glCopyTexSubImage2D for depth textures with
    // GL_INVALID_VALUE — only full glCopyTexImage2D works. We're now
    // filtered to full-screen swaps and throttled to 1/3 frames, so the
    // re-alloc cost is bounded.
    while (glGetError() != GL_NO_ERROR) {}
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                     vp[0], vp[1], w, h, 0);
    GLenum err = glGetError();

    s_depthTexW = w;
    s_depthTexH = h;
    static int s_logged = 0;
    if (err != GL_NO_ERROR || s_logged == 0) {
        s_logged = 1;
        DiagLog("[depth] CopyTex %dx%d tex=%u err=0x%04x", w, h, s_depthTex, err);
    }

    // We own TMU DEPTH_CAPTURE_TMU — leave our texture bound there so FPs
    // can sample it. Only restore the active TMU selector.
    activeTexture(savedActiveTMU);
}
