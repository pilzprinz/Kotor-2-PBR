/*
PBR hooks implementation.

Forwards calls to system OpenGL via the same `p[]` jump table used by trampolines,
runs PBR association/binding logic around them.
*/

#include "pbr_hooks.h"
#include "pbr_state.h"
#include "file_logger.h"
#include "glfunctions.h"   // p[] trampoline table
#include <stdio.h>
#include <string.h>

// Trampoline indexes into p[] (must match opengl32.def @ordinals).
static const int TRAMP_GLBINDTEXTURE    = 11;
static const int TRAMP_GLTEXIMAGE2D     = 308;
static const int TRAMP_GLDELETETEXTURES = 66;

// === Function pointer typedefs ===
typedef void (WINAPI *PFNGLBINDTEXTURE)(GLenum, GLuint);
typedef void (WINAPI *PFNGLTEXIMAGE2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);
typedef void (WINAPI *PFNGLACTIVETEXTURE)(GLenum);
typedef void (WINAPI *PFNGLDELETETEXTURES)(GLsizei, const GLuint *);

// === glActiveTexture resolution ===
// glActiveTexture is GL 1.3+ / ARB_multitexture, not in opengl32.dll exports.
// Resolve lazily via wglGetProcAddress on first sibling bind.
static PFNGLACTIVETEXTURE ResolveActiveTexture()
{
    static PFNGLACTIVETEXTURE cached = NULL;
    static bool tried = false;
    if (tried) return cached;
    tried = true;

    HMODULE hOpenGL = GetModuleHandleA("opengl32.dll");
    if (!hOpenGL) return NULL;

    typedef PROC (WINAPI *PFNWGLGETPROCADDRESS)(LPCSTR);
    PFNWGLGETPROCADDRESS getProc =
        (PFNWGLGETPROCADDRESS)GetProcAddress(hOpenGL, "wglGetProcAddress");
    if (!getProc) return NULL;

    cached = (PFNGLACTIVETEXTURE)getProc("glActiveTexture");
    if (!cached) cached = (PFNGLACTIVETEXTURE)getProc("glActiveTextureARB");
    return cached;
}

// === Fallback white 1x1 texture for unbound PBR slots ===
// Sampling an unbound TMU is undefined per spec — bind white as safe default.
static GLuint EnsureWhiteTexture()
{
    static GLuint cached = 0;
    if (cached) return cached;

    PFNGLTEXIMAGE2D real = (PFNGLTEXIMAGE2D)p[TRAMP_GLTEXIMAGE2D];
    PFNGLBINDTEXTURE bind = (PFNGLBINDTEXTURE)p[TRAMP_GLBINDTEXTURE];

    GLuint id = 0;
    glGenTextures(1, &id);
    if (!id) return 0;

    bind(GL_TEXTURE_2D, id);
    unsigned char px[4] = { 255, 255, 255, 255 };
    real(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    cached = id;
    return id;
}

// === Bind sibling textures to extra TMUs ===
// TMU 8/9/10 used — Aspyr K2 driver wrapper rejects glActiveTexture(TEXTURE11+).
// AO and Emissive packed into alpha channels of normal/rough TMUs by pbr_state.
// Pass nullptr to clear (binds white everywhere) — prevents stale-bind leak
// from previous PBR draw bleeding into untracked textures.
static void BindSiblings(const SiblingSet *s, GLenum savedActiveTMU)
{
    PFNGLACTIVETEXTURE activeTexture = ResolveActiveTexture();
    if (!activeTexture) return;

    PFNGLBINDTEXTURE bind = (PFNGLBINDTEXTURE)p[TRAMP_GLBINDTEXTURE];
    GLuint white = EnsureWhiteTexture();

    GLuint n = (s && s->normalId) ? s->normalId : white;
    GLuint r = (s && s->roughId)  ? s->roughId  : white;
    GLuint m = (s && s->metalId)  ? s->metalId  : white;

    // TMU 8/9/10 are mod-exclusive (engine binds nothing past ~TMU3; Aspyr rejects
    // 11+), so once set they persist until we change them. This runs on every
    // diffuse bind (hundreds/frame); consecutive draws of one material — or any
    // materials sharing white siblings — request the identical n/r/m, so skip the
    // 3 binds + 4 activeTexture switches when nothing changed. (Active TMU is always
    // TEXTURE0 here — the caller only proceeds for TMU0 binds — so an early return
    // leaves it where the engine expects it.)
    static GLuint lastN = 0, lastR = 0, lastM = 0;
    if (n == lastN && r == lastR && m == lastM) return;
    lastN = n; lastR = r; lastM = m;

    activeTexture(GL_TEXTURE8);  bind(GL_TEXTURE_2D, n);
    activeTexture(GL_TEXTURE9);  bind(GL_TEXTURE_2D, r);
    activeTexture(GL_TEXTURE10); bind(GL_TEXTURE_2D, m);

    activeTexture(savedActiveTMU);
}

// === Scene-identifier logging ============================================
// Track current envmap (cube map) and skybox-candidate (first 2D bound to
// TMU 0 each frame, after glClear). Logged once per change for the user
// to correlate location → envmap/skybox names.
static GLuint g_lastEnvmapId         = 0;
static GLuint g_lastSkyboxId         = 0;
static bool   g_frameFirst2DCaptured = false;

extern "C" void PbrHooks_OnFrameClear()
{
    g_frameFirst2DCaptured = false;
}

static void LogScene(const char *kind, GLuint id)
{
    const char *name = PbrGetTextureName(id);
    char line[256];
    snprintf(line, sizeof(line), "LOCATION|%s|id=%u|name=%s\n",
             kind, id, name ? name : "?");
    PbrLogLine(line);
}

extern "C" void __stdcall my_glBindTexturePBR(GLenum target, GLuint texture)
{
    PFNGLBINDTEXTURE realBind = (PFNGLBINDTEXTURE)p[TRAMP_GLBINDTEXTURE];
    realBind(target, texture);

    // Skip during own sibling-upload re-entry.
    if (g_pbrLoadingInProgress) return;

    // Envmap detect: cube bind change
    if (target == GL_TEXTURE_CUBE_MAP && texture != 0 && texture != g_lastEnvmapId) {
        g_lastEnvmapId = texture;
        LogScene("envmap", texture);
    }

    // Only diffuse-slot 2D textures trigger PBR pipeline.
    if (target != GL_TEXTURE_2D || texture == 0) return;
    GLint activeTMU = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTMU);
    if ((GLenum)activeTMU != GL_TEXTURE0) return;

    // Skybox candidate: first 2D texture bound to TMU 0 after a glClear.
    // K2 typically draws skybox/background first, so this picks up that.
    if (!g_frameFirst2DCaptured) {
        g_frameFirst2DCaptured = true;
        if (texture != g_lastSkyboxId) {
            g_lastSkyboxId = texture;
            LogScene("skybox_candidate", texture);
        }
    }

    SiblingSet *s = PbrGetSiblings(texture);
    if (!s)
    {
        // Texture id not tracked (font / GUI / engine-internal).
        // Clear sibling TMUs to white — prevents previous PBR draw's siblings
        // from leaking onto this untracked texture in TMU 8/9/10.
        BindSiblings(NULL, (GLenum)activeTMU);
        PbrPushDefaultEnvParams();
        return;
    }

    // Always re-bind sibling TMUs (white if absent) so no stale textures leak.
    // Sidecar-only case (no sibling .tga but .txi/.pbr present) still gets
    // white binds here; PbrPushEnvParams applies sidecar params via env[20..23].
    BindSiblings(s, (GLenum)activeTMU);

    PbrPushEnvParams(texture);
}

extern "C" void __stdcall my_glTexImage2DPBR(GLenum target, GLint level, GLint internalformat,
                                              GLsizei width, GLsizei height, GLint border,
                                              GLenum format, GLenum type, const GLvoid *pixels)
{
    PFNGLTEXIMAGE2D real = (PFNGLTEXIMAGE2D)p[TRAMP_GLTEXIMAGE2D];

    // Cube face upload — enable auto-mipmap on level-0, set mipmap-aware filter.
    // If engine provides higher levels explicitly (TPC with mips), those will
    // overwrite auto-generated levels on subsequent upload calls.
    bool isCubeFace = (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                       target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z);
    if (isCubeFace && level == 0 && !g_pbrLoadingInProgress)
    {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_GENERATE_MIPMAP, GL_TRUE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }

    real(target, level, internalformat, width, height, border, format, type, pixels);

    if (g_pbrLoadingInProgress) return;

    // 2D diffuse association — only first mip-level uploads count.
    if (level == 0 && target == GL_TEXTURE_2D)
    {
        GLint boundId = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundId);
        if (boundId != 0)
            PbrAssociateBoundTexture((GLuint)boundId);
    }
}

extern "C" void __stdcall my_glDeleteTexturesPBR(GLsizei n, const GLuint *textures)
{
    PFNGLDELETETEXTURES real = (PFNGLDELETETEXTURES)p[TRAMP_GLDELETETEXTURES];
    real(n, textures);

    // Clear id→name cache so the engine reusing this id doesn't apply stale
    // PBR siblings from a previously-loaded texture.
    if (!textures || g_pbrLoadingInProgress) return;
    for (GLsizei i = 0; i < n; ++i)
        PbrForgetTexture(textures[i]);
}

void PbrHooksInit()
{
    PbrInit();
}
