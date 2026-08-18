/*
GL fixed-function state capture implementation. See header for overview.
*/

#include "gl_state_capture.h"
#include "iat_hook.h"
#include "file_logger.h"
#include "depth_capture.h"
#include "shadow_map.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// Enums not always present in mingw gl.h.
#ifndef GL_MODELVIEW
#define GL_MODELVIEW             0x1700
#define GL_PROJECTION            0x1701
#define GL_TEXTURE               0x1702
#endif
#ifndef GL_DEPTH_BUFFER_BIT
#define GL_DEPTH_BUFFER_BIT      0x00000100
#endif
#ifndef GL_LIGHT0
#define GL_LIGHT0                0x4000
#endif
#ifndef GL_POSITION
#define GL_POSITION              0x1203
#define GL_AMBIENT               0x1200
#define GL_DIFFUSE               0x1201
#define GL_SPECULAR              0x1202
#define GL_CONSTANT_ATTENUATION  0x1207
#define GL_LINEAR_ATTENUATION    0x1208
#define GL_QUADRATIC_ATTENUATION 0x1209
#endif

// === Function pointer typedefs =====================================
typedef void (WINAPI *PFNGLMATRIXMODE)(GLenum);
typedef void (WINAPI *PFNGLLOADIDENTITY)(void);
typedef void (WINAPI *PFNGLLOADMATRIXF)(const GLfloat*);
typedef void (WINAPI *PFNGLLOADMATRIXD)(const GLdouble*);
typedef void (WINAPI *PFNGLMULTMATRIXF)(const GLfloat*);
typedef void (WINAPI *PFNGLMULTMATRIXD)(const GLdouble*);
typedef void (WINAPI *PFNGLPUSHMATRIX)(void);
typedef void (WINAPI *PFNGLPOPMATRIX)(void);
typedef void (WINAPI *PFNGLDEPTHMASK)(GLboolean);
typedef void (WINAPI *PFNGLCLEAR)(GLbitfield);
typedef void (WINAPI *PFNGLLIGHTFV)(GLenum, GLenum, const GLfloat*);
typedef void (WINAPI *PFNGLLIGHTF)(GLenum, GLenum, GLfloat);
typedef void (WINAPI *PFNGLENABLE)(GLenum);
typedef void (WINAPI *PFNGLDISABLE)(GLenum);

// === Captured originals ============================================
static PFNGLMATRIXMODE   o_MatrixMode   = NULL;
static PFNGLLOADIDENTITY o_LoadIdentity = NULL;
static PFNGLLOADMATRIXF  o_LoadMatrixf  = NULL;
static PFNGLLOADMATRIXD  o_LoadMatrixd  = NULL;
static PFNGLMULTMATRIXF  o_MultMatrixf  = NULL;
static PFNGLMULTMATRIXD  o_MultMatrixd  = NULL;
static PFNGLPUSHMATRIX   o_PushMatrix   = NULL;
static PFNGLPOPMATRIX    o_PopMatrix    = NULL;
static PFNGLDEPTHMASK    o_DepthMask    = NULL;
static PFNGLCLEAR        o_Clear        = NULL;
static PFNGLLIGHTFV      o_Lightfv      = NULL;
static PFNGLLIGHTF       o_Lightf       = NULL;
static PFNGLENABLE       o_Enable       = NULL;
static PFNGLDISABLE      o_Disable      = NULL;

// === Mirrored state ================================================
// GL spec guarantees ≥ 32 modelview stack slots; mirror enough for any
// engine we care about.
#define MAT_STACK_DEPTH 32
struct MatStack { float m[MAT_STACK_DEPTH][16]; int sp; };

static GLenum   s_matrixMode = GL_MODELVIEW;
static MatStack s_mvStack;
static MatStack s_projStack;

static GLboolean s_depthMask         = GL_TRUE;
static int       s_swapSnapshotDone  = 0;
static int       s_charSnapshotDone  = 0;

static GlLightState s_lights[8];

// === Matrix helpers ================================================
static void MatIdentity(float *m)
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void MatCopy(float *dst, const float *src) { memcpy(dst, src, 64); }

// Column-major mult: result = a * b (post-mul like glMultMatrix).
static void MatMul(float *out, const float *a, const float *b)
{
    float r[16];
    for (int c = 0; c < 4; c++)
    for (int row = 0; row < 4; row++)
        r[c*4 + row] =
            a[0*4 + row] * b[c*4 + 0] +
            a[1*4 + row] * b[c*4 + 1] +
            a[2*4 + row] * b[c*4 + 2] +
            a[3*4 + row] * b[c*4 + 3];
    memcpy(out, r, 64);
}

static MatStack *CurrentStack()
{
    switch (s_matrixMode) {
        case GL_PROJECTION: return &s_projStack;
        case GL_MODELVIEW:
        default:            return &s_mvStack;
    }
    // Texture matrix mode is not mirrored — unused by current PBR FPs.
}

static float *CurrentMatrix()
{
    MatStack *st = CurrentStack();
    return st->m[st->sp];
}

// === Public API ====================================================
const float *GlState_Modelview()        { return s_mvStack.m[s_mvStack.sp]; }
int          GlState_DidSnapshotThisFrame() { return s_swapSnapshotDone || s_charSnapshotDone; }

void GlState_TriggerCharSnapshotIfNeeded()
{
    if (s_charSnapshotDone) return;
    DepthCapture_SnapshotDefaultDepth();
    s_charSnapshotDone = 1;
}

const GlLightState *GlState_Light(int i)
{
    if (i < 0 || i > 7) return NULL;
    return &s_lights[i];
}

// === Hook wrappers =================================================
static void WINAPI MyMatrixMode(GLenum mode)
{
    if (o_MatrixMode) o_MatrixMode(mode);
    s_matrixMode = mode;
}

static void WINAPI MyLoadIdentity(void)
{
    if (o_LoadIdentity) o_LoadIdentity();
    MatIdentity(CurrentMatrix());
}

static void WINAPI MyLoadMatrixf(const GLfloat *m)
{
    if (o_LoadMatrixf) o_LoadMatrixf(m);
    if (m) MatCopy(CurrentMatrix(), m);
}

static void WINAPI MyLoadMatrixd(const GLdouble *m)
{
    if (o_LoadMatrixd) o_LoadMatrixd(m);
    if (!m) return;
    float *dst = CurrentMatrix();
    for (int i = 0; i < 16; i++) dst[i] = (float)m[i];
}

static void WINAPI MyMultMatrixf(const GLfloat *m)
{
    if (o_MultMatrixf) o_MultMatrixf(m);
    if (m) MatMul(CurrentMatrix(), CurrentMatrix(), m);
}

static void WINAPI MyMultMatrixd(const GLdouble *m)
{
    if (o_MultMatrixd) o_MultMatrixd(m);
    if (!m) return;
    float mf[16];
    for (int i = 0; i < 16; i++) mf[i] = (float)m[i];
    MatMul(CurrentMatrix(), CurrentMatrix(), mf);
}

// Snapshot of the modelview matrix at the first non-identity glPushMatrix in
// MODELVIEW. Uses glGetFloatv to read REAL GL state (not our mirror) so it
// catches matrices built via glRotate/glTranslate which we don't hook.
// Skips identity-rotation matrices (HUD/menu passes).
static float s_savedViewMv[16];
static int   s_viewMvSaved = 0;

static bool IsRotIdentity(const float *m, float tol = 0.01f)
{
    return  fabsf(m[0]  - 1) < tol && fabsf(m[5]  - 1) < tol &&
            fabsf(m[10] - 1) < tol &&
            fabsf(m[1]) < tol && fabsf(m[2]) < tol && fabsf(m[4]) < tol &&
            fabsf(m[6]) < tol && fabsf(m[8]) < tol && fabsf(m[9]) < tol;
}

static void WINAPI MyPushMatrix(void)
{
    if (o_PushMatrix) o_PushMatrix();
    MatStack *st = CurrentStack();
    if (st == &s_mvStack && !s_viewMvSaved) {
        GLfloat real[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, real);
        if (!IsRotIdentity(real)) {
            MatCopy(s_savedViewMv, real);
            s_viewMvSaved = 1;
        }
    }
    if (st->sp < MAT_STACK_DEPTH - 1) {
        MatCopy(st->m[st->sp + 1], st->m[st->sp]);
        st->sp++;
    }
}

extern "C" void GlState_ResetFrameView()
{
    s_viewMvSaved = 0;
}

extern "C" const float *GlState_FrameViewMatrix()
{
    return s_viewMvSaved ? s_savedViewMv : NULL;
}

static void WINAPI MyPopMatrix(void)
{
    if (o_PopMatrix) o_PopMatrix();
    MatStack *st = CurrentStack();
    if (st->sp > 0) st->sp--;
}

static void WINAPI MyDepthMask(GLboolean flag)
{
    if (o_DepthMask) o_DepthMask(flag);
    // Opaque→transparent transition is the moment to snapshot — depth buffer
    // holds the full opaque scene and transparency typically doesn't write
    // depth, so what we capture here is the final usable scene depth.
    if (s_depthMask == GL_TRUE && flag == GL_FALSE && !s_swapSnapshotDone) {
        DepthCapture_SnapshotDefaultDepth();
        s_swapSnapshotDone = 1;
        // NOTE: caster-pass end is NOT here. K2 toggles DepthMask FALSE for
        // skybox setup BEFORE opaque world, so this fires too early. End the
        // caster pass at SwapBuffers instead (covers opaque + transparency;
        // transparency typically draws with depth-write off so doesn't cast).
    }
    s_depthMask = flag;
}

static void WINAPI MyClear(GLbitfield mask)
{
    if (mask & GL_DEPTH_BUFFER_BIT) {
        s_swapSnapshotDone = 0;
        s_charSnapshotDone = 0;
        ShadowMap_BeginCasterPass();  // frame start
    }
    if (o_Clear) o_Clear(mask);
}

static void WINAPI MyLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    if (o_Lightfv) o_Lightfv(light, pname, params);
    if (!params) return;
    int idx = (int)(light - GL_LIGHT0);
    if (idx < 0 || idx > 7) return;
    GlLightState &L = s_lights[idx];
    switch (pname) {
        case GL_POSITION: for (int i = 0; i < 4; i++) L.position[i] = params[i]; break;
        case GL_DIFFUSE:  for (int i = 0; i < 4; i++) L.diffuse[i]  = params[i]; break;
        case GL_AMBIENT:  for (int i = 0; i < 4; i++) L.ambient[i]  = params[i]; break;
        case GL_SPECULAR: for (int i = 0; i < 4; i++) L.specular[i] = params[i]; break;
        default: break;
    }
}

static void WINAPI MyLightf(GLenum light, GLenum pname, GLfloat param)
{
    if (o_Lightf) o_Lightf(light, pname, param);
    int idx = (int)(light - GL_LIGHT0);
    if (idx < 0 || idx > 7) return;
    GlLightState &L = s_lights[idx];
    if      (pname == GL_CONSTANT_ATTENUATION)  L.attenuation[0] = param;
    else if (pname == GL_LINEAR_ATTENUATION)    L.attenuation[1] = param;
    else if (pname == GL_QUADRATIC_ATTENUATION) L.attenuation[2] = param;
}

static void WINAPI MyEnable(GLenum cap)
{
    if (o_Enable) o_Enable(cap);
    if (cap >= GL_LIGHT0 && cap <= (GL_LIGHT0 + 7))
        s_lights[cap - GL_LIGHT0].enabled = 1;
}

static void WINAPI MyDisable(GLenum cap)
{
    if (o_Disable) o_Disable(cap);
    if (cap >= GL_LIGHT0 && cap <= (GL_LIGHT0 + 7))
        s_lights[cap - GL_LIGHT0].enabled = 0;
}

// === Install =======================================================
void GlStateCapture_Init()
{
    for (int s = 0; s < MAT_STACK_DEPTH; s++) {
        MatIdentity(s_mvStack.m[s]);
        MatIdentity(s_projStack.m[s]);
    }
    s_mvStack.sp = s_projStack.sp = 0;
    memset(s_lights, 0, sizeof(s_lights));

    HMODULE hExe = GetModuleHandleA(NULL);

    struct Entry { const char *name; void *fn; void **orig; };
    Entry tab[] = {
        { "glMatrixMode",   (void*)MyMatrixMode,   (void**)&o_MatrixMode   },
        { "glLoadIdentity", (void*)MyLoadIdentity, (void**)&o_LoadIdentity },
        { "glLoadMatrixf",  (void*)MyLoadMatrixf,  (void**)&o_LoadMatrixf  },
        { "glLoadMatrixd",  (void*)MyLoadMatrixd,  (void**)&o_LoadMatrixd  },
        { "glMultMatrixf",  (void*)MyMultMatrixf,  (void**)&o_MultMatrixf  },
        { "glMultMatrixd",  (void*)MyMultMatrixd,  (void**)&o_MultMatrixd  },
        { "glPushMatrix",   (void*)MyPushMatrix,   (void**)&o_PushMatrix   },
        { "glPopMatrix",    (void*)MyPopMatrix,    (void**)&o_PopMatrix    },
        { "glDepthMask",    (void*)MyDepthMask,    (void**)&o_DepthMask    },
        { "glClear",        (void*)MyClear,        (void**)&o_Clear        },
        { "glLightfv",      (void*)MyLightfv,      (void**)&o_Lightfv      },
        { "glLightf",       (void*)MyLightf,       (void**)&o_Lightf       },
        { "glEnable",       (void*)MyEnable,       (void**)&o_Enable       },
        { "glDisable",      (void*)MyDisable,      (void**)&o_Disable      },
    };

    int hooked = 0;
    int skipped = 0;
    char skippedNames[512] = {0};
    for (size_t i = 0; i < sizeof(tab)/sizeof(tab[0]); i++) {
        FARPROC orig = IatHook(hExe, "opengl32.dll", tab[i].name, (FARPROC)tab[i].fn);
        *tab[i].orig = (void*)orig;
        if (orig) {
            hooked++;
        } else {
            skipped++;
            size_t cur = strlen(skippedNames);
            snprintf(skippedNames + cur, sizeof(skippedNames) - cur,
                     "%s%s", cur ? "," : "", tab[i].name);
        }
    }

    char line[800];
    snprintf(line, sizeof(line),
             "[state] gl_state_capture init: hooked=%d skipped=%d (%s)\n",
             hooked, skipped, skippedNames);
    PbrLogLine(line);
}
