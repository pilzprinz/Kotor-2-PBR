/*
Directional sun shadow map. Caster pass renders the world from the sun's POV
into a depth FBO via double-draw + a light-space caster VP; main FPs sample
that depth to gate the sun term. Design + matrices: docs/SHADOW_MAP.md.
*/

#include "shadow_map.h"
#include "depth_capture.h"   // FBO enums + typedefs + DEPTH_COMPONENT* enums
#include "glFunctions.h"     // orig_wglGetProcAddress, GetProgramName
#include "pbr_tune.h"        // DiagLog
#include "pbr_state.h"       // PbrGetTextureName (diag: label draws by texture)
#include "platform.h"
#include <windows.h>
#include <GL/gl.h>
#include <map>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdint.h>
#include <math.h>
#include <string.h>

#ifndef GL_FRAGMENT_PROGRAM_BINDING_ARB
#define GL_FRAGMENT_PROGRAM_BINDING_ARB  0x8873
#endif
#ifndef GL_VERTEX_PROGRAM_BINDING_ARB
#define GL_VERTEX_PROGRAM_BINDING_ARB    0x864A
#endif

#ifndef GL_FRAMEBUFFER_COMPLETE_EXT
#define GL_FRAMEBUFFER_COMPLETE_EXT                          0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT             0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT     0x8CD7
#define GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT             0x8CD9
#define GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT                0x8CDA
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT            0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT            0x8CDC
#define GL_FRAMEBUFFER_UNSUPPORTED_EXT                       0x8CDD
#endif

typedef GLenum (WINAPI *PFNGLCHECKFRAMEBUFFERSTATUS)(GLenum);
typedef void   (WINAPI *PFNGLGENPROGRAMS)(GLsizei, GLuint*);
typedef void   (WINAPI *PFNGLGETPROGRAMENVPARAMETERFV)(GLenum, GLuint, GLfloat*);

#ifndef GL_PROGRAM_FORMAT_ASCII_ARB
#define GL_PROGRAM_FORMAT_ASCII_ARB  0x8875
#endif
#ifndef GL_ALPHA_TEST
#define GL_ALPHA_TEST                0x0BC0
#endif
#ifndef GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING
#define GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING 0x889A
#endif
#ifndef GL_VERTEX_PROGRAM_ARB
#define GL_VERTEX_PROGRAM_ARB        0x8620
#endif
#ifndef GL_FRAGMENT_PROGRAM_ARB
#define GL_FRAGMENT_PROGRAM_ARB      0x8804
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE             0x812F
#endif
#ifndef GL_TEXTURE_COMPARE_MODE
#define GL_TEXTURE_COMPARE_MODE      0x884C
#endif
#ifndef GL_DEPTH_TEXTURE_MODE
#define GL_DEPTH_TEXTURE_MODE        0x884B
#endif
#ifndef GL_LUMINANCE
#define GL_LUMINANCE                 0x1909
#endif
#ifndef GL_NONE
#define GL_NONE                      0
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                  0x84C0
#endif

typedef void (WINAPI *PFNGLACTIVETEXTUREPROC)(GLenum);
static PFNGLACTIVETEXTUREPROC s_glActiveTexture = NULL;

// --- Geometry cache (stable caster set) GL plumbing ---
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER                 0x8892
#define GL_ELEMENT_ARRAY_BUFFER         0x8893
#define GL_ARRAY_BUFFER_BINDING         0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_VERTEX_ARRAY_BUFFER_BINDING  0x8896
#endif
#ifndef GL_VERTEX_ARRAY
#define GL_VERTEX_ARRAY                 0x8074
#define GL_VERTEX_ARRAY_SIZE            0x807A
#define GL_VERTEX_ARRAY_TYPE            0x807B
#define GL_VERTEX_ARRAY_STRIDE          0x807C
#define GL_VERTEX_ARRAY_POINTER         0x808E
#endif
#ifndef GL_FRAMEBUFFER_BINDING_EXT
#define GL_FRAMEBUFFER_BINDING_EXT      0x8CA6
#endif

typedef void      (WINAPI *PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef GLboolean (WINAPI *PFNGLISBUFFERPROC)(GLuint);
typedef void      (WINAPI *PFNGLDRAWELEMENTSPROC)(GLenum, GLsizei, GLenum, const GLvoid*);
typedef void      (WINAPI *PFNGLDRAWARRAYSPROC)(GLenum, GLint, GLsizei);
static PFNGLBINDBUFFERPROC   s_glBindBuffer      = NULL;
static PFNGLISBUFFERPROC     s_glIsBuffer        = NULL;
static PFNGLDRAWELEMENTSPROC s_realDrawElements  = NULL;
static PFNGLDRAWARRAYSPROC   s_realDrawArrays    = NULL;

// Two views of the SAME ping-pong pair, assigned by RECEIVER type:
//   TMU6 (texture[6]) = COMPLETE map = LAST frame's finished tex. World receivers
//     (fp_worldtex*, fp_door) sample it with env[100..103] = matched-K (last frame's
//     K). Cast shadows on the static world stay correct; a moving caster's shadow on
//     the ground trails 1 frame (barely visible on static surfaces).
//   TMU5 (texture[5]) = SELF map = THIS frame's in-progress tex. Model/char receivers
//     (fp_model*, skinned) sample it with env[104..106] = current K. Their OWN depth is
//     written (BeforeDraw) immediately before their color draw, so self-shadow is
//     SAME-FRAME → no creep on movement. Static is already complete in it (cache renders
//     at frame start), so environment shadows on models are current too. Only missing:
//     shadowing by another model drawn LATER this frame (minor). This is why the old
//     "model path" failed before — static used to draw inline; now it's cache-complete.
#define SHADOW_TMU       6
#define SHADOW_TMU_ENUM  (GL_TEXTURE0 + SHADOW_TMU)
#define SHADOW_SELF_TMU      5
#define SHADOW_SELF_TMU_ENUM (GL_TEXTURE0 + SHADOW_SELF_TMU)

// Live shadow-map resolution. Mutable: the "Shadow res" slider requests a new
// size via ShadowMap_SetResolution; BeginCasterPass applies it (recreate FBO).
static int kShadowMapSize    = 4096;
static int s_reqShadowMapSize = 4096;

void ShadowMap_SetResolution(int px)
{
    if (px < 1024) px = 1024;
    if (px > 8000) px = 8000;
    px = (px + 32) & ~63;          // round to multiple of 64
    s_reqShadowMapSize = px;
}

// Per-frame diagnostics toggle (see ShadowMap_SetDiag). When on, BuildLightK and
// the first dynamic caster of each frame log to pbr_tune_diag.log so jitter can
// be measured. s_diagCasterLogged gates to one caster per frame.
static bool   s_diag = false;
static bool   s_diagCasterLogged = false;
static GLuint s_diagLockVbo = 0;   // lock onto ONE mesh so we track the same object
static bool   s_diagLockIsChar = false; // whether the locked mesh was a CC_CHAR (diag log)
void ShadowMap_SetDiag(int on) {
    s_diag = (on != 0);
    if (on) { s_diagLockVbo = 0; s_diagCasterLogged = false; s_diagLockIsChar = false; }
}

// Depth probe: after the locked caster's draw writes into the shadow tex, read the
// stored depth back at its projected texel and compare centre-vs-surroundings. This
// is the ground truth separating a write-side bug (caster stores no depth contrast →
// neighbours ≈ centre) from a receive-side bug (map is correct but the receiver FP
// samples wrong UV / mismatched K → ground stays lit). Filled in PushLightMVPForDraw,
// consumed in ShadowMap_AfterDraw while the shadow FBO is still bound.
static double s_probeTexelU = 0.0;
static double s_probeTexelV = 0.0;
static GLuint s_probeVbo    = 0;
// Body texel: projection of the locked caster elevated ~1.6 world units along
// world-up (+Z in the light basis). At near-zenith sun the feet texel reads bare
// floor (spread≈0 even when the caster writes) because the body's ground footprint
// shifts only ~0.5 units off origin — but the torso is much nearer the sun than the
// floor it hides, so an elevated probe shows real depth contrast. Filled in
// PushLightMVPForDraw, consumed in ShadowMap_AfterDraw.
static double s_probeBodyU = 0.0;
static double s_probeBodyV = 0.0;
static double s_probeBodyValid = 0.0;
// World-up vector the light basis was built from (BuildLightK), for the elevated
// body probe. Re-deriving it in PushLightMVPForDraw would risk a different sun/up.
static float s_worldUp[3] = { 0.0f, 0.0f, 1.0f };

// Slope-scaled polygon offset units for the caster pass. The world size of one
// texel grows with the light box ("Shadow range"), so a fixed offset under-biases
// at large range → acne reappears as a hard double edge. Scale units with extent
// so high range stays acne-free without over-biasing (peter-pan) at small range.
static float ShadowPolyUnits(float extent)
{
    float k = extent / 60.0f;
    if (k < 1.0f) k = 1.0f;
    return 1.5f * k;
}

static bool   s_available    = false;
static HGLRC  s_initCtx       = NULL;   // GL context Init ran on (detect reload)
static bool   s_inCaster     = false;
static bool   s_useCache     = true;   // geometry cache on/off (env[31].x toggle)
static bool   s_castGeom     = true;   // level geometry casts (fp_worldtex_*) env[31].y
static bool   s_castModel    = true;   // models/placeables cast (fp_model non-skinned) env[31].z
static bool   s_castChar     = true;   // characters cast (skinned)            env[31].w
static int    s_frameNum     = 0;
static int    s_drawsInPass  = 0;
static int    s_skipsInPass  = 0;
// Per-class LIVE caster tallies for the diag: [CC_SKIP, CC_GEOM, CC_MODEL, CC_CHAR].
// Counts casters rendered live this frame (CC_GEOM goes via the cache, not here).
static int    s_castLiveN[4] = { 0, 0, 0, 0 };
static int    s_castCulledN  = 0;      // live casters box-culled this frame (origin outside the ortho footprint → write no depth)

// Ping-pong FBO/tex pair. Frame N fills s_fbo[s_writeIdx]; the main pass that
// frame samples s_depthTex[1-s_writeIdx] — last frame's COMPLETE map. Without
// this, the double-draw fills and samples one tex within the same frame, so an
// object is only shadowed by objects drawn before it and the sampled content
// shifts frame to frame (visible jitter). Toggled at each BeginCaster.
static GLuint s_fbo[2]              = {0, 0};
static GLuint s_depthTex[2]         = {0, 0};
static int    s_writeIdx           = 0;
static GLuint s_shadowVpId          = 0;   // static caster
static GLuint s_shadowVpSkinnedId   = 0;   // skinned (bone-blend) caster
static GLuint s_shadowFpAlphaId     = 0;   // alpha-test caster FP (punchthrough cutouts)

// Per-frame histogram: maps (fpId, vpId) → draw count. Logged at EndCaster on
// log frames to identify which shader pair is the K2 player shadow.
static std::map<std::string, int> s_progHist;   // diag: "fp=.. vp=.. tex=.. aTest=.." → count

// Per-draw save (no caching across draws — K2 changes viewport for sub-renders
// and colormask for stencil/alpha tricks, so we must re-query each time).
static GLint    s_drawVp[4]      = {0,0,0,0};
static GLboolean s_drawColorMask[4] = {0,0,0,0};
static GLint    s_drawSavedVp    = 0;     // engine VP id (0 = no VP bound)
static GLint    s_drawSavedFp    = 0;     // engine FP id (restored when we bind the alpha caster FP)
static GLboolean s_drawSavedFpEn = GL_FALSE;
static GLboolean s_drawSavedVpEn = GL_FALSE;
static GLboolean s_drawSavedCull = GL_FALSE;  // engine GL_CULL_FACE state
static bool     s_drawBoundAlphaFp = false;   // did this draw bind the alpha caster FP?

// Cached entry points (resolved at Init).
typedef void      (WINAPI *PFNGLDELETEFRAMEBUFFERS)(GLsizei, const GLuint*);
typedef GLboolean (WINAPI *PFNGLISFRAMEBUFFER)(GLuint);
typedef void      (WINAPI *PFNGLBLITFRAMEBUFFER)(GLint,GLint,GLint,GLint,
                                                 GLint,GLint,GLint,GLint,
                                                 GLbitfield,GLenum);
static PFNGLGENFRAMEBUFFERS             s_glGenFB         = NULL;
static PFNGLBINDFRAMEBUFFER             s_glBindFB        = NULL;
static PFNGLDELETEFRAMEBUFFERS          s_glDeleteFB      = NULL;
static PFNGLISFRAMEBUFFER               s_glIsFB          = NULL;
static PFNGLFRAMEBUFFERTEXTURE2D        s_glFBTex2D       = NULL;
static PFNGLCHECKFRAMEBUFFERSTATUS      s_glCheckFB       = NULL;
static PFNGLGETPROGRAMENVPARAMETERFV    s_glGetEnvFv      = NULL;
static PFNGLBLITFRAMEBUFFER             s_glBlitFB        = NULL;

#ifndef GL_READ_FRAMEBUFFER_EXT
#define GL_READ_FRAMEBUFFER_EXT 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER_EXT
#define GL_DRAW_FRAMEBUFFER_EXT 0x8CA9
#endif

static int    s_cacheVersion       = 0;       // bumped on cache add/evict (diag only)

// Log: first 5 frames (catches init/menu pattern) + every 60th frame after
// (catches in-game scene pattern without log spam).
static const int kLogEarly  = 5;
static const int kLogPeriod = 60;

template<typename FN> static FN Resolve(const char *name)
{
    if (!orig_wglGetProcAddress) return NULL;
    return (FN)orig_wglGetProcAddress(name);
}

bool ShadowMap_IsAvailable() { return s_available; }

static bool ShouldLogFrame()
{
    // Diag on → log EVERY frame: gives dense per-frame caster activity (for the
    // tactical-pause investigation) + the per-draw inventory (to identify which
    // FP/VP/texture grass, steam, etc. use). Keep diag on only briefly — verbose.
    return s_diag || s_frameNum <= kLogEarly || (s_frameNum % kLogPeriod) == 0;
}

static void BuildLightK(int logFrame);
static void BindShadowMaps();
static void EvictCache();
static void RenderCacheInto(GLuint targetFbo);

void ShadowMap_BeginCasterPass()
{
    // Self-heal: an area/context reload destroys our FBO but leaves s_available
    // set, so the caster pass would render into a dead FBO (err 0x0506, NaN
    // depth → broken shadows). Detect via context-handle change (recreation) or
    // a now-invalid depth tex (deletion on the same context), then rebuild
    // everything (ShadowMap_Init also wipes the now-stale geometry cache).
    if (s_available) {
        HGLRC cur = wglGetCurrentContext();
        bool lost = (cur != s_initCtx)
                 || !glIsTexture(s_depthTex[0])
                 || (s_glIsFB && !s_glIsFB(s_fbo[0]));
        if (lost) {
            DiagLog("[shadow] context/FBO reload detected → reinit");
            ShadowMap_Init();
        }
        // Apply a pending resolution change (slider). Init recreates FBO/tex at
        // the new kShadowMapSize and wipes the now-mismatched geometry cache.
        else if (s_reqShadowMapSize != kShadowMapSize) {
            DiagLog("[shadow] resolution %d → %d, recreate", kShadowMapSize, s_reqShadowMapSize);
            kShadowMapSize = s_reqShadowMapSize;
            ShadowMap_Init();
        }
    }
    if (!s_available) return;
    if (s_inCaster) return;  // already in pass (multiple glClears per frame)
    s_inCaster    = true;
    s_drawsInPass = 0;
    s_skipsInPass = 0;
    s_castLiveN[0] = s_castLiveN[1] = s_castLiveN[2] = s_castLiveN[3] = 0;
    s_castCulledN = 0;
    s_diagCasterLogged = false;
    s_frameNum++;

    // FRAGMENT env[31]: x=cache on/off, y=geometry casts, z=models cast,
    // w=characters cast ("Shadow cache/geometry/models/characters" sliders).
    if (s_glGetEnvFv) {
        float e31[4] = { 1, 1, 1, 1 };
        s_glGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 31, e31);
        s_useCache  = (e31[0] >= 0.5f);
        s_castGeom  = (e31[1] >= 0.5f);
        s_castModel = (e31[2] >= 0.5f);
        s_castChar  = (e31[3] >= 0.5f);
    }
    // Hard requirement: without these the cache can't replay → captured statics
    // would vanish. Fall back to the live double-draw path instead.
    if (!s_glBindBuffer || !s_realDrawElements || !s_realDrawArrays)
        s_useCache = false;

    // Ping-pong: this frame writes s_depthTex[s_writeIdx]; the main pass samples last
    // frame's complete map (the other tex) on TMU6. Avoids sampling a half-built map
    // (casters are captured inline during the main pass), at the cost of 1 frame lag.
    s_writeIdx = 1 - s_writeIdx;

    // Build the light box (K) and push the sample matrix to the receiver FPs.
    BuildLightK(ShouldLogFrame() ? 1 : 0);
    if (s_useCache) EvictCache();

    // Clear this frame's write tex, then replay the cached level geometry into it
    // (every frame — measured cheap). Live characters/models add to the same tex
    // during the main pass via BeforeDraw. RenderCacheInto no-ops if the cache is
    // off/empty, leaving the cleared map for the live casters alone.
    s_glBindFB(GL_FRAMEBUFFER_EXT, s_fbo[s_writeIdx]);
    glClear(GL_DEPTH_BUFFER_BIT);
    s_glBindFB(GL_FRAMEBUFFER_EXT, 0);
    RenderCacheInto(s_fbo[s_writeIdx]);

    // Bind last frame's complete map for this frame's main pass (TMU6).
    BindShadowMaps();

    if (ShouldLogFrame())
        DiagLog("[shadow] BeginCaster frame=%d cache=%d", s_frameNum, s_useCache ? 1 : 0);
}

// Sample shadow tex content via glReadPixels for diagnostic. Reads a small
// center region, computes min/max/avg. Confirms verts actually rasterize
// into the depth tex.
static void SampleShadowTex()
{
    if (!s_glBindFB) return;
    const int N = 32;
    GLfloat px[N * N];
    s_glBindFB(GL_FRAMEBUFFER_EXT, s_fbo[s_writeIdx]);   // just-filled tex
    int cx = (kShadowMapSize - N) / 2;
    int cy = (kShadowMapSize - N) / 2;
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(cx, cy, N, N, GL_DEPTH_COMPONENT, GL_FLOAT, px);
    GLenum err = glGetError();
    s_glBindFB(GL_FRAMEBUFFER_EXT, 0);

    float mn = 1e30f, mx = -1e30f, sum = 0.0f;
    for (int i = 0; i < N * N; i++) {
        float v = px[i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
        sum += v;
    }
    DiagLog("[shadow]   tex center(%dx%d): min=%.4f max=%.4f avg=%.4f err=0x%04x",
            N, N, mn, mx, sum / (N * N), err);
}

void ShadowMap_EndCasterPass()
{
    if (!s_available) return;
    if (!s_inCaster) return;
    s_inCaster = false;

    // Unconditional diagnostic for the first 10 frames: shows whether characters/droids
    // are being classified + drawn as casters, and whether the shadow tex has content.
    // This is the ground truth for "no char shadows" — if CHAR=0 the caster path is dead.
    {
        static int s_bootLogFrames = 0;
        if (s_bootLogFrames < 10) {
            s_bootLogFrames++;
            DiagLog("[shadow][boot] frame=%d draws=%d skips=%d culled=%d live: GEOM=%d MODEL=%d CHAR=%d vpId=%u vpSkinned=%u",
                    s_frameNum, s_drawsInPass, s_skipsInPass, s_castCulledN,
                    s_castLiveN[1], s_castLiveN[2], s_castLiveN[3],
                    (unsigned)s_shadowVpId, (unsigned)s_shadowVpSkinnedId);
        }
    }

    if (ShouldLogFrame()) {
        // Index by enum value (CC_GEOM=1, CC_MODEL=2, CC_CHAR=3); enum declared below.
        DiagLog("[shadow] EndCaster   frame=%d draws=%d skips=%d culled=%d  live: GEOM=%d MODEL=%d CHAR=%d",
                s_frameNum, s_drawsInPass, s_skipsInPass, s_castCulledN,
                s_castLiveN[1], s_castLiveN[2], s_castLiveN[3]);
        for (std::map<std::string, int>::iterator it = s_progHist.begin();
             it != s_progHist.end(); ++it) {
            DiagLog("[shadow]   %s count=%d", it->first.c_str(), it->second);
        }
        s_progHist.clear();
        SampleShadowTex();
    }
    // Binding for the next frame's sampling is done in BeginCasterPass (both maps).
}

// ============================================================================
// Light-space matrix math (Phase 2.1b)
//
//   modelview_engine = view * model
//   view_inv * modelview_engine = model
//   lightMVP_draw = lightProj * lightView * model
//                = lightProj * lightView * view_inv * modelview_engine
//                = K_world * modelview_engine
//
// We precompute K_world = lightProj * lightView * view_inv once per frame
// from sun direction (env[27].xyz on FP target) and engine view-inverse
// (env[90..92] on VP target, K2's camera-to-world basis).
//
// Convention: row-major 4×4. m[r*4 + c] = element row r, col c.
// Shader DP4 pattern matches: result.x = DP4(row0, vec) = clip space x.
// ============================================================================

struct Mat4 { float m[16]; };

static bool s_lightKValid = false;   // this frame's light box K (s_KmainForCache) is built + valid

// ============================================================================
// Geometry cache — a persistent set of STATIC caster meshes in model space +
// their world transform, so the shadow map can be rebuilt every frame from a
// COMPLETE, stable caster set instead of replaying whatever the engine's
// camera-frustum cull happened to submit. Fixes shadows popping/disappearing on
// rotation and the per-frame caster-set shimmer. Skinned/dynamic casters are
// NOT cached (bone state changes per frame) — they keep the live double-draw.
// ============================================================================
struct CachedDraw {
    GLuint        arrayVbo;   // VBO holding vertex positions
    const GLvoid *vptr;       // byte offset into arrayVbo
    GLint         vsize;      // components per position (2/3/4)
    GLenum        vtype;      // GL_FLOAT etc.
    GLsizei       vstride;
    GLuint        elemVbo;    // index VBO (0 = glDrawArrays)
    const GLvoid *indices;    // byte offset into elemVbo
    GLsizei       count;
    GLenum        itype;      // index type
    GLint         first;      // glDrawArrays first
    bool          isArrays;
    Mat4          model;      // world transform (view_inv * modelview at capture)
    float         center[3];  // world origin, for distance eviction
    int           lastSeen;   // frame number last submitted by the engine
    uint64_t      key;        // dedup key (kept so EvictCache can rebuild the map)
    // Punchthrough: when the engine had GL_ALPHA_TEST on, the replay must alpha-test
    // (KIL transparent texels) so cached grates/leaves cast cutout shadows, not
    // solid blocks. Needs the diffuse + its UV array. Solid alpha-test surfaces
    // (alpha=1) cast solid anyway — KIL is a no-op there.
    bool          alphaTest;  // engine had GL_ALPHA_TEST enabled at capture
    GLuint        diffuseTex;  // TMU0 texture (for the alpha-test caster FP)
    GLuint        tcVbo;       // VBO holding texcoord0 (0 = none)
    const GLvoid *tcptr;       // byte offset into tcVbo
    GLint         tcsize;      // components per texcoord (2/3/4)
    GLenum        tctype;      // GL_FLOAT etc.
    GLsizei       tcstride;
};
static Mat4                    s_KmainForCache;        // this frame's light box K (cache replay + live casters write)
static Mat4                    s_KmainSample;          // K_main of the frame that FILLED the now-bound map (receiver sample)
static bool                    s_KmainSampleValid = false;
static std::vector<CachedDraw> s_cache;
static std::unordered_map<uint64_t,int>  s_cacheKey;   // dedup key → index in s_cache
static float                   s_anchorWorld[3] = {0,0,0};
static float                   s_extentWorld = 60.0f;  // current light half-extent (for eviction + caster cull)
static float                   s_lightR[3] = {1,0,0};  // light-space right basis (caster box-cull)
static float                   s_lightU[3] = {0,1,0};  // light-space up basis    (caster box-cull)
static const size_t            kMaxCache   = 6000;
static const int               kCacheStale = 240;      // frames before evicting unseen mesh
static const float             kCacheEvictDist = 280.0f;

static void M4Identity(Mat4 &o)
{
    for (int i = 0; i < 16; i++) o.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

static void M4Mul(Mat4 &out, const Mat4 &a, const Mat4 &b)
{
    Mat4 t;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[r*4 + k] * b.m[k*4 + c];
            t.m[r*4 + c] = s;
        }
    out = t;
}

static void M4Ortho(Mat4 &o, float L, float R, float B, float T, float N, float F)
{
    M4Identity(o);
    o.m[0]  = 2.0f / (R - L);
    o.m[3]  = -(R + L) / (R - L);
    o.m[5]  = 2.0f / (T - B);
    o.m[7]  = -(T + B) / (T - B);
    o.m[10] = -2.0f / (F - N);
    o.m[11] = -(F + N) / (F - N);
}

static void V3Norm(float v[3])
{
    float n = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (n > 1e-6f) { v[0]/=n; v[1]/=n; v[2]/=n; }
}

static void V3Cross(float o[3], const float a[3], const float b[3])
{
    o[0] = a[1]*b[2] - a[2]*b[1];
    o[1] = a[2]*b[0] - a[0]*b[2];
    o[2] = a[0]*b[1] - a[1]*b[0];
}

static float V3Dot(const float a[3], const float b[3])
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

// Build view matrix looking from `eye` toward `target` with `up` reference.
static void M4LookAt(Mat4 &o, const float eye[3], const float target[3], const float up[3])
{
    float f[3] = { target[0]-eye[0], target[1]-eye[1], target[2]-eye[2] };
    V3Norm(f);
    float upn[3] = { up[0], up[1], up[2] };
    V3Norm(upn);
    float r[3]; V3Cross(r, f, upn); V3Norm(r);
    float u[3]; V3Cross(u, r, f);

    M4Identity(o);
    o.m[0]  =  r[0]; o.m[1]  =  r[1]; o.m[2]  =  r[2]; o.m[3]  = -V3Dot(r, eye);
    o.m[4]  =  u[0]; o.m[5]  =  u[1]; o.m[6]  =  u[2]; o.m[7]  = -V3Dot(u, eye);
    o.m[8]  = -f[0]; o.m[9]  = -f[1]; o.m[10] = -f[2]; o.m[11] =  V3Dot(f, eye);
}

// Build engine's camera-to-world (view_inv) matrix from env[90..92] of the
// VP target. K2 stores .xyz of each slot as a row of view-to-world rotation
// and .w as a component of camera world position (column 3 of result):
//   env[92] → row 0 + cam.x
//   env[91] → row 1 + cam.y
//   env[90] → row 2 + cam.z
static bool ReadEngineViewInv(Mat4 &out)
{
    if (!s_glGetEnvFv) return false;
    GLfloat r92[4], r91[4], r90[4];
    s_glGetEnvFv(GL_VERTEX_PROGRAM_ARB, 92, r92);
    s_glGetEnvFv(GL_VERTEX_PROGRAM_ARB, 91, r91);
    s_glGetEnvFv(GL_VERTEX_PROGRAM_ARB, 90, r90);
    M4Identity(out);
    out.m[0]=r92[0]; out.m[1]=r92[1]; out.m[2]=r92[2]; out.m[3]=r92[3];
    out.m[4]=r91[0]; out.m[5]=r91[1]; out.m[6]=r91[2]; out.m[7]=r91[3];
    out.m[8]=r90[0]; out.m[9]=r90[1]; out.m[10]=r90[2]; out.m[11]=r90[3];
    return true;
}

// Read tunable env[27].xyz from FP target (PbrTune pushes sun there).
static bool ReadSunDir(float dir[3])
{
    if (!s_glGetEnvFv) return false;
    GLfloat e27[4];
    s_glGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 27, e27);
    dir[0] = e27[0]; dir[1] = e27[1]; dir[2] = e27[2];
    V3Norm(dir);
    return true;
}

// Compose K_main = lightProj * lightView (world→light clip). Both the main FP
// (worldPos P5 → clip) and the caster lightMVP build from it: the caster does
// K_main * (view_inv * mv) = K_main * world, with view_inv read at DRAW time so
// it cancels mv's camera exactly (see PushLightMVPForDraw).
//
// Light-axis texel snap: project camPos onto the light's lateral r/u axes,
// snap to texel-grid increments there, reconstruct. Each camera step shifts
// the light frame by exactly one texel along r or u → no "swimming" shadows.
static void BuildLightK(int logFrame)
{
    Mat4 viewInv;
    if (!ReadEngineViewInv(viewInv)) {
        s_lightKValid = false;
        if (logFrame) DiagLog("[shadow] BuildLightK: no glGetEnvFv");
        return;
    }
    float sun[3];
    if (!ReadSunDir(sun)) {
        s_lightKValid = false;
        if (logFrame) DiagLog("[shadow] BuildLightK: no sun");
        return;
    }

    // Ortho extent = HALF the lateral coverage of the light frustum (world
    // units). This is the single biggest quality lever: at ±150 a 1024 map gives
    // 0.29 u/texel — far too coarse, so shadows are blobby, alias into oval
    // smears, and crawl on any motion. Tightening to a player-sized box raises
    // effective resolution (±60 → 0.117 u/texel, 2.5× sharper) which fixes the
    // blobs AND the depth precision (less acne / peter-panning). Cost: occluders
    // beyond the box don't cast. Tunable live via FRAGMENT env[30].z
    // ("Shadow range"); fallback 60. The half-extent is always exactly 512
    // texels (extent / (2*extent/1024)), so texel-snap alignment holds for any
    // value.
    float kOrthoExtent = 60.0f;
    if (s_glGetEnvFv) {
        float e30[4] = { 0, 0, 0, 0 };
        s_glGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 30, e30);
        if (e30[2] > 1.0f) kOrthoExtent = e30[2];
    }
    const float kTexel = (2.0f * kOrthoExtent) / (float)kShadowMapSize;

    // --- Anchor = the camera's look-at point (the player) ---
    // K2's chase cam ORBITS the player on rotation, so anchoring at the camera
    // makes shadows swim. The player is the orbit centre. We recover it exactly,
    // without knowing the zoom distance, from two consecutive camera rays: both
    // pass through the player, so their closest-approach midpoint IS the player.
    // This is rotation-invariant by construction — when you only yaw, every
    // frame's ray-pair gives the same fixed point → the light box is frozen →
    // no shake, no shadows sliding out of the [0,1] box.
    //
    // The radius (t,s ≈ camera→player distance) is also learned from the ray
    // solve and low-pass filtered into s_learnedR, used as the fallback when the
    // rays are near-parallel (pure translation / standing still — denom→0). The
    // "Shadow orbit R" slider just seeds the initial guess.
    float rawCam[3] = { viewInv.m[3], viewInv.m[7], viewInv.m[11] };
    float fwd[3]    = { -viewInv.m[2], -viewInv.m[6], -viewInv.m[10] };
    V3Norm(fwd);

    static float s_learnedR    = 0.0f;
    static float s_prevCam[3]  = { 0, 0, 0 };
    static float s_prevFwd[3]  = { 0, 0, 0 };
    static bool  s_prevValid   = false;
    if (s_learnedR == 0.0f) {
        s_learnedR = 6.0f;
        if (s_glGetEnvFv) {
            float e30[4] = { 0, 0, 0, 0 };
            s_glGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 30, e30);
            if (e30[1] > 0.0f) s_learnedR = e30[1];
        }
    }

    // Use the two-ray solve ONLY to LEARN the orbit radius — never as the anchor
    // position directly. The solved centre is noisy (t,s blow up near parallel),
    // and feeding it to the anchor made the box jump past the hysteresis → the
    // "swim back and forth". Instead we keep the deterministic, smooth formula
    //   anchor = camPos + fwd * learnedR
    // which is rotation-INVARIANT once learnedR == the true radius (then
    // camPos+fwd*R == player for every yaw). learnedR is low-pass filtered, and
    // only updated from well-conditioned solves, so it converges smoothly with
    // no per-frame jitter leaking into the anchor.
    if (s_prevValid) {
        float b = V3Dot(s_prevFwd, fwd);          // cos(yaw step); a=c=1
        float denom = 1.0f - b * b;
        if (denom > 0.02f) {                        // require a clear yaw step
            float rr[3] = { s_prevCam[0]-rawCam[0], s_prevCam[1]-rawCam[1], s_prevCam[2]-rawCam[2] };
            float d = V3Dot(s_prevFwd, rr);
            float e = V3Dot(fwd, rr);
            float t = (b*e - d) / denom;
            float s = (e - b*d) / denom;
            if (t > 1.0f && t < 80.0f && s > 1.0f && s < 80.0f) {
                s_learnedR += 0.1f * (0.5f*(t+s) - s_learnedR);   // slow low-pass
                if (s_learnedR < 1.0f)  s_learnedR = 1.0f;
                if (s_learnedR > 80.0f) s_learnedR = 80.0f;
            } else if (logFrame && (t > 1.0f || s > 1.0f)) {
                // Solve rejected — t,s outside [1,80]. Indicates camera distance
                // exceeds the bound (likely on large areas like 804dro).
                DiagLog("[shadow] BuildLightK ray-solve REJECTED t=%.1f s=%.1f learnedR=%.1f",
                        t, s, s_learnedR);
            }
        }
    }
    float anchor[3] = {
        rawCam[0] + fwd[0]*s_learnedR,
        rawCam[1] + fwd[1]*s_learnedR,
        rawCam[2] + fwd[2]*s_learnedR
    };
    s_prevCam[0] = rawCam[0]; s_prevCam[1] = rawCam[1]; s_prevCam[2] = rawCam[2];
    s_prevFwd[0] = fwd[0];    s_prevFwd[1] = fwd[1];    s_prevFwd[2] = fwd[2];
    s_prevValid  = true;

    // Hysteresis latch — absorbs sub-texel anchor noise (ray-solve jitter) when
    // the player is essentially still, so the box stays byte-identical and the
    // static map isn't re-rendered every frame. MUST stay small: when the latch
    // releases it snaps the box by up to kHyst at once, and the texel-snap below
    // re-aligns the grid — but a LARGE kHyst means a large visible jump of every
    // dynamic-object shadow on each release (measured: a 6u deadzone produced
    // 6-unit shadow "скачут" while walking). 2 texels is below perceptible while
    // still killing per-frame re-render when standing still. (The earlier
    // extent-scaled 6u value was a regression — diag proved the box, not
    // precision, was the wobble source.)
    static float s_latch[3]   = { 0, 0, 0 };
    static bool  s_latchValid = false;
    float kHyst = 2.0f * kTexel;
    if (s_latchValid) {
        float dx = anchor[0]-s_latch[0], dy = anchor[1]-s_latch[1], dz = anchor[2]-s_latch[2];
        if (dx*dx + dy*dy + dz*dz <= kHyst*kHyst) {
            anchor[0] = s_latch[0]; anchor[1] = s_latch[1]; anchor[2] = s_latch[2];
        }
    }
    s_latch[0] = anchor[0]; s_latch[1] = anchor[1]; s_latch[2] = anchor[2];
    s_latchValid = true;

    // Texel-grid snap along the light's lateral axes — the canonical stable-
    // shadow technique. Quantum = 1 texel; the ortho half-extent is always an
    // integer number of texels (extent / (2*extent/res) = res/2), so snapping the
    // frustum centre to whole texels makes every world point land at a CONSTANT
    // sub-texel offset regardless of camera motion → no shimmer/crawl. The frame
    // tracks the anchor smoothly in texel steps. r/u are world-fixed (derived
    // from the constant sun + up), so this is a genuine world-space grid.
    const float kSnapTexels = 1.0f;
    const float kSnap       = kTexel * kSnapTexels;
    float f[3]       = { sun[0], sun[1], sun[2] };
    V3Norm(f);
    // up must NOT be parallel to the light direction f, else lookAt + the r/u
    // basis degenerate (cross→0) → garbage matrix → "shadows by polygon" acne at
    // sun straight down (z≈1). Fall back to world-Y when the sun is near vertical.
    float up[3]      = { 0.0f, 0.0f, 1.0f };
    if (fabsf(f[2]) > 0.985f) { up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f; }
    s_worldUp[0] = up[0]; s_worldUp[1] = up[1]; s_worldUp[2] = up[2];
    float r[3];      V3Cross(r, f, up); V3Norm(r);
    float u[3];      V3Cross(u, r, f);
    s_lightR[0]=r[0]; s_lightR[1]=r[1]; s_lightR[2]=r[2];   // stash basis for the per-caster box cull
    s_lightU[0]=u[0]; s_lightU[1]=u[1]; s_lightU[2]=u[2];

    float camR       = V3Dot(r, anchor);
    float camU       = V3Dot(u, anchor);
    float camF       = V3Dot(f, anchor);
    camR             = floorf(camR / kSnap + 0.5f) * kSnap;
    camU             = floorf(camU / kSnap + 0.5f) * kSnap;
    float camPos[3]  = {
        camR*r[0] + camU*u[0] + camF*f[0],
        camR*r[1] + camU*u[1] + camF*f[1],
        camR*r[2] + camU*u[2] + camF*f[2]
    };

    // Diagnostic: box stability per frame. Stand still + rotate camera; if
    // dCamSnap stays ~0 the box is stable (rotation-invariant) and any remaining
    // dynamic wobble is NOT the box; if it spikes, the anchor isn't rotation-
    // invariant. learnedR is the orbit radius the two-ray solver converged on.
    if (s_diag) {
        static float s_dPrev[3] = {0,0,0}; static bool s_dValid = false;
        float dC = 0.0f;
        if (s_dValid) {
            float dx=camPos[0]-s_dPrev[0], dy=camPos[1]-s_dPrev[1], dz=camPos[2]-s_dPrev[2];
            dC = sqrtf(dx*dx+dy*dy+dz*dz);
        }
        s_dPrev[0]=camPos[0]; s_dPrev[1]=camPos[1]; s_dPrev[2]=camPos[2]; s_dValid=true;
        DiagLog("[diag] f=%d rawCam=(%.2f,%.2f,%.2f) R=%.2f anchor=(%.2f,%.2f,%.2f) "
                "camSnap=(%.3f,%.3f,%.3f) dCamSnap=%.4f ext=%.0f texel=%.4f",
                s_frameNum, rawCam[0],rawCam[1],rawCam[2], s_learnedR,
                anchor[0],anchor[1],anchor[2], camPos[0],camPos[1],camPos[2], dC,
                kOrthoExtent, kTexel);
    }

    // sun = direction-TO-source in K2 shader convention (N·sun > 0 means
    // surface faces sun). Light eye = source position = camPos + sun*kDist.
    // Earlier minus sign placed eye on opposite side → no occluders between
    // light and scene → shadow tex empty of useful blocker depth.
    const float kDist = 100.0f;
    float eye[3]    = { camPos[0] + sun[0]*kDist, camPos[1] + sun[1]*kDist, camPos[2] + sun[2]*kDist };
    float target[3] = { camPos[0], camPos[1], camPos[2] };

    Mat4 lightView; M4LookAt(lightView, eye, target, up);
    // Depth range brackets the eye→geometry span (eye sits kDist along the sun;
    // occluders within ±extent of the target). far scales with extent so a wide
    // box still reaches ground-level geometry below the target.
    const float kFar = kDist + 2.0f * kOrthoExtent + 50.0f;
    Mat4 lightProj; M4Ortho(lightProj, -kOrthoExtent, kOrthoExtent, -kOrthoExtent, kOrthoExtent, 1.0f, kFar);

    Mat4 K_main; M4Mul(K_main,   lightProj, lightView);
    // Live casters now multiply K_main by a DRAW-TIME viewInv (see
    // PushLightMVPForDraw), not this frame-start one. Folding viewInv in here
    // (the old s_lightK) skewed live casters by one frame of camera motion:
    // K_main*viewInv(clear)*mv(draw) leaves a residual camera-delta when the
    // camera rotates between clear and the draw, so self-shadows crept onto lit
    // areas. K_main*(viewInv(draw)*mv(draw)) == K_main*world, matching the
    // receiver's K_main*P5_world exactly. s_lightKValid just gates "K_main ready".
    s_lightKValid    = true;
    s_KmainForCache  = K_main;           // light box K: cache replay + live casters + receiver sample
    s_extentWorld    = kOrthoExtent;     // for distance-based cache eviction + caster cull

    // PCF tap step = 1 texel of the CURRENT shadow map, pushed to FRAGMENT
    // env[32].xy so the FPs stay resolution-agnostic (no hardcoded 1/1024).
    if (orig_glProgramEnvParameter4d) {
        double t = 1.0 / (double)kShadowMapSize;
        orig_glProgramEnvParameter4d(GL_FRAGMENT_PROGRAM_ARB, 32, t, t, 0.0, 0.0);
    }

    // Track the player anchor for cache eviction; a big jump = area change → wipe
    // the cache so stale geometry from the previous module isn't replayed.
    {
        float jx = camPos[0]-s_anchorWorld[0], jy = camPos[1]-s_anchorWorld[1], jz = camPos[2]-s_anchorWorld[2];
        if (jx*jx + jy*jy + jz*jz > 200.0f*200.0f) {
            s_cache.clear();
            s_cacheKey.clear();
        }
        s_anchorWorld[0] = camPos[0]; s_anchorWorld[1] = camPos[1]; s_anchorWorld[2] = camPos[2];
    }

    // Two receiver sample matrices (FRAGMENT env), one per map view:
    //   env[100..103] = matched-K (LAST frame's K_main). The COMPLETE map on TMU6 was
    //     filled last frame with this K, so WORLD receivers must project with it (not the
    //     current K — that mismatch made static shadows float on camera move/run and
    //     misprojected everything into occlusion on a fast burst = the dark "blink").
    //   env[104..106] = current K_main. The SELF map on TMU5 is THIS frame's tex, filled
    //     this frame with the current K, so MODEL/CHAR receivers project with the current
    //     K → their own depth lines up exactly → same-frame self-shadow, no creep.
    if (orig_glProgramEnvParameter4d) {
        const Mat4 &Ksamp = s_KmainSampleValid ? s_KmainSample : K_main;
        for (int row = 0; row < 4; row++) {
            orig_glProgramEnvParameter4d(GL_FRAGMENT_PROGRAM_ARB, 100 + row,
                (double)Ksamp.m[row*4 + 0], (double)Ksamp.m[row*4 + 1],
                (double)Ksamp.m[row*4 + 2], (double)Ksamp.m[row*4 + 3]);
            orig_glProgramEnvParameter4d(GL_FRAGMENT_PROGRAM_ARB, 104 + row,
                (double)K_main.m[row*4 + 0], (double)K_main.m[row*4 + 1],
                (double)K_main.m[row*4 + 2], (double)K_main.m[row*4 + 3]);
        }
    }
    // Stash this frame's K_main; next frame it matches the tex being sampled (next
    // frame's read = this frame's write, which the cache replay + live casters fill
    // with exactly this K_main).
    s_KmainSample      = K_main;
    s_KmainSampleValid = true;

    if (logFrame) {
        float e29[4] = {0,0,0,0}, e30[4] = {0,0,0,0};
        if (s_glGetEnvFv) {
            s_glGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 29, e29);   // str, viz, floor, bias
            s_glGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 30, e30);   // darken, orbitR, range, nrmBias
        }
        // shadow_bias (e29.w) is in light-NCDC units: at near=1/far=~200 ortho depth,
        // 0.0138 ≈ 2-3 WORLD UNITS of depth slack → clears self-shadow around every
        // model ("sphere"). Keep < ~0.002.
        DiagLog("[shadow] BuildLightK cam=(%.3f,%.3f,%.3f) sun=(%.3f,%.3f,%.3f) texel=%.4f "
                "anchorW=(%.2f,%.2f,%.2f) rawCam=(%.2f,%.2f,%.2f) R=%.2f ext=%.0f "
                "bias=%.4f nrmBias=%.2f str=%.2f darken=%.2f",
                camPos[0], camPos[1], camPos[2], sun[0], sun[1], sun[2], kTexel,
                s_anchorWorld[0], s_anchorWorld[1], s_anchorWorld[2],
                rawCam[0], rawCam[1], rawCam[2], s_learnedR, kOrthoExtent,
                e29[3], e30[3], e29[0], e30[0]);
    }
}

// Bind both views of the ping-pong pair for this frame's main pass, by receiver type:
//   TMU6 = LAST frame's COMPLETE tex (1-frame lag) — world receivers (cast-on-ground).
//   TMU5 = THIS frame's IN-PROGRESS write tex — model/char receivers (same-frame self).
// The write tex (s_depthTex[s_writeIdx]) is bound to TMU5 AND is the depth attachment
// when caster depth-writes happen during the pass — but the caster FP never samples
// TMU5, so there is no read feedback loop; model color draws sample it with FBO 0 bound.
static void BindShadowMaps()
{
    if (!s_glActiveTexture) return;
    GLint savedActive = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &savedActive);
    GLuint completeTex = s_depthTex[1 - s_writeIdx];   // last frame, finished
    GLuint selfTex     = s_depthTex[s_writeIdx];        // this frame, being written
    if (completeTex) {
        s_glActiveTexture(SHADOW_TMU_ENUM);
        glBindTexture(GL_TEXTURE_2D, completeTex);
    }
    if (selfTex) {
        s_glActiveTexture(SHADOW_SELF_TMU_ENUM);
        glBindTexture(GL_TEXTURE_2D, selfTex);
    }
    s_glActiveTexture((GLenum)savedActive);
}

// Push lightMVP = K_main * (viewInv_draw * mv) to env[100..103] of the caster VP.
// Read the engine's WORLD matrix for the current draw (viewInv * modelview), plus
// the modelview itself (the diag needs eyeZ). Cheap — 1 glGet + 1 glGet inside
// ReadEngineViewInv + a 4x4 mul — so it runs for every live-caster candidate, ahead
// of the heavy per-caster GL churn, to feed the box cull. viewInv read at DRAW time
// matches mv's camera exactly: world = viewInv*mv is camera-invariant for a static
// object, so lmvp = K*world lands on the same texel the receiver computes as K*P5_world
// (no camera-rotation skew, matching the cache replay path). Returns false if viewInv
// is unavailable (caller skips the draw).
static bool ComputeCasterWorld(Mat4 &model, Mat4 &mv)
{
    // glGetFloatv returns column-major. Transpose into row-major for our math.
    GLfloat mv_col[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv_col);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            mv.m[r*4 + c] = mv_col[c*4 + r];
    Mat4 viewInv;
    if (!ReadEngineViewInv(viewInv)) return false;
    M4Mul(model, viewInv, mv);
    return true;
}

// True if a live caster at world origin wp can write into the current light box: its
// light-space footprint (origin projected onto the r/u basis) is within the ortho
// extent, plus a generous object-size margin. A caster outside the footprint is
// clipped by the ortho projection → writes no depth → casts no visible shadow, so
// culling it on the CPU loses nothing and skips the per-caster FBO bind + viewport +
// VP swap + draw (the dominant dynamic-caster cost). Errs toward keeping: a wrongly
// culled caster = a lost shadow (quality), a wrongly kept one = a few wasted texels.
static bool CasterInLightBox(const float wp[3])
{
    float ox = wp[0]-s_anchorWorld[0], oy = wp[1]-s_anchorWorld[1], oz = wp[2]-s_anchorWorld[2];
    float pr = ox*s_lightR[0] + oy*s_lightR[1] + oz*s_lightR[2];
    float pu = ox*s_lightU[0] + oy*s_lightU[1] + oz*s_lightU[2];
    const float kCasterMargin = 20.0f;          // object half-size slack; bump if edge objects lose their shadow
    float lim = s_extentWorld + kCasterMargin;
    return fabsf(pr) <= lim && fabsf(pu) <= lim;
}

// Push K_dyn * world as the caster's light-space MVP (VERTEX env[100..103]). Takes
// the already-computed world/modelview from ComputeCasterWorld (no re-read).
static void PushLightMVPForDraw(const Mat4 &model, const Mat4 &mv)
{
    if (!s_lightKValid || !orig_glProgramEnvParameter4d) return;
    Mat4 lmvp;  M4Mul(lmvp, s_KmainForCache, model);

    for (int row = 0; row < 4; row++) {
        orig_glProgramEnvParameter4d(GL_VERTEX_PROGRAM_ARB, 100 + row,
            (double)lmvp.m[row*4 + 0],
            (double)lmvp.m[row*4 + 1],
            (double)lmvp.m[row*4 + 2],
            (double)lmvp.m[row*4 + 3]);
    }

    // Diagnostic: track ONE locked mesh (first caster seen after diag enabled) so
    // we follow the SAME object frame to frame. Log its WORLD position (model col 3)
    // AND where its origin lands in the shadow map. Stand still, then ORBIT camera:
    // both steady = caster fine; texel drifts = box/projection; world drifts = FP.
    if (s_diag && !s_diagCasterLogged) {
        GLint vbo0 = 0; glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &vbo0);
        if (s_diagLockVbo == 0 && vbo0 != 0) s_diagLockVbo = (GLuint)vbo0;
    }
    // Track the locked caster EVERY frame (not just the first log): its mesh moves
    // and the light box shifts, so the depth probe must follow its current texel.
    if (s_diag && s_diagLockVbo != 0) {
        GLint vbo = 0; glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &vbo);
        if ((GLuint)vbo == s_diagLockVbo) {
            double ow = lmvp.m[15];
            double ndcx = (ow != 0.0) ? lmvp.m[3]/ow : 0.0;
            double ndcy = (ow != 0.0) ? lmvp.m[7]/ow : 0.0;
            s_probeTexelU = (ndcx*0.5 + 0.5) * (double)kShadowMapSize;
            s_probeTexelV = (ndcy*0.5 + 0.5) * (double)kShadowMapSize;
            s_probeVbo    = (GLuint)vbo;
            // Elevated body probe: the caster's world origin (model col 3) + ~1.6
            // world-up. Near-zenith sun → body ground footprint shifts only slightly
            // from origin, but the torso is much nearer the sun than the floor, so a
            // probe here shows write-side depth contrast the feet texel hides.
            if (s_lightKValid) {
                double bx = model.m[3] + s_worldUp[0]*1.6;
                double by = model.m[7] + s_worldUp[1]*1.6;
                double bz = model.m[11] + s_worldUp[2]*1.6;
                const Mat4 &K = s_KmainForCache;
                double bw = K.m[3]*bx + K.m[7]*by + K.m[11]*bz + K.m[15];
                double bn = K.m[0]*bx + K.m[4]*by + K.m[8]*bz + K.m[12];
                double be = K.m[1]*bx + K.m[5]*by + K.m[9]*bz + K.m[13];
                if (bw != 0.0) {
                    s_probeBodyU = (bn/bw*0.5 + 0.5) * (double)kShadowMapSize;
                    s_probeBodyV = (be/bw*0.5 + 0.5) * (double)kShadowMapSize;
                    s_probeBodyValid = 1.0;
                } else {
                    s_probeBodyValid = 0.0;
                }
            }
            if (!s_diagCasterLogged) {
                s_diagCasterLogged = true;
                DiagLog("[diag]   lockVbo=%u world=(%.4f,%.4f,%.4f) texel=(%.3f,%.3f) eyeZ=%.2f kind=%s",
                        (unsigned)s_diagLockVbo, model.m[3], model.m[7], model.m[11],
                        s_probeTexelU, s_probeTexelV, mv.m[11],
                        s_diagLockIsChar ? "CHAR" : "other");
            }
        }
    }
}

// Caster category, from the bound FP/VP + viewport. Post-process / UI FPs draw
// fullscreen quads at the near plane (depth ≈ 0) that would poison the shadow
// tex, so only world/model FPs at full viewport classify as casters.
enum CasterClass { CC_SKIP = 0, CC_GEOM, CC_MODEL, CC_CHAR };

// Per-draw classification is shared between the two hook entry points: the draw
// hook calls ShadowMap_TryCaptureCaster first, then ShadowMap_BeforeDraw only if
// that returned false — same draw, no GL state changes in between. TryCaptureCaster
// stashes the class here so BeforeDraw reuses it instead of re-running ClassifyCaster
// (~5 glGet + 2 map lookups). This path runs for EVERY draw, including ~30k
// fixed-function grass draws/frame, so the double classify was a real CPU/driver hit.
static CasterClass s_pendingClass = CC_SKIP;   // class of the current draw
static GLint       s_classVp      = 0;         // engine VP id read while classifying (valid when class != CC_SKIP)

static CasterClass ClassifyCaster()
{
    GLint vp4[4]; glGetIntegerv(GL_VIEWPORT, vp4);
    if (vp4[2] < 1280 || vp4[3] < 720) return CC_SKIP;

    GLint fp = 0; glGetIntegerv(GL_FRAGMENT_PROGRAM_BINDING_ARB, &fp);
    if (fp == 0) return CC_SKIP;                 // fixed-function (grass, UI) is never a caster — skip the name lookup
    const char *fpN = GetProgramName((GLuint)fp);
    if (!fpN || !fpN[0]) return CC_SKIP;

    // VP test FIRST: a skinned (bone-animated) VP is always a MOVING mesh, no
    // matter which FP draws it. Droid visors/metal parts use a reflective
    // worldtex FP (fp_worldtex_bump_env) on a skinned VP — classifying by FP
    // alone tagged them CC_GEOM and cached them as static, leaving a trail of
    // stale shadow blobs along their path. Skinned ⇒ CC_CHAR ⇒ live, never cached.
    GLint vpId = 0; glGetIntegerv(GL_VERTEX_PROGRAM_BINDING_ARB, &vpId);
    s_classVp = vpId;                            // BeforeDraw reuses this for the VP save/restore
    const char *vpN = GetProgramName((GLuint)vpId);
    if (vpN && vpN[0] && strncmp(vpN, "vp_skinned", 10) == 0) return CC_CHAR;

    if (strncmp(fpN, "fp_worldtex", 11) == 0) return CC_GEOM;     // static level geometry
    if (strncmp(fpN, "fp_model", 8) != 0)     return CC_SKIP;
    return CC_MODEL;                                              // rigid placeable / object
}

static bool CategoryEnabled(CasterClass c)
{
    switch (c) {
        case CC_GEOM:  return s_castGeom;
        case CC_MODEL: return s_castModel;
        case CC_CHAR:  return s_castChar;
        default:       return false;
    }
}

// Bind shadow FBO + viewport + color mask for a LIVE caster draw. Used for
// models/characters (and geometry only when the cache is off or the mesh isn't
// cacheable). Returns false when the draw should not cast.
bool ShadowMap_BeforeDraw()
{
    if (!s_available || !s_inCaster) return false;

    // Reuse the classification the preceding TryCaptureCaster already computed for
    // this same draw (it ran ClassifyCaster + CategoryEnabled before returning false).
    CasterClass cc = s_pendingClass;
    if (cc == CC_SKIP || !CategoryEnabled(cc)) { s_skipsInPass++; return false; }

    // Box-cull BEFORE any state change. Read the caster's world placement (cheap) and
    // reject it if its footprint falls outside the ortho box — it would be clipped and
    // write no depth anyway, so this is free image-quality-wise and skips the whole
    // per-caster GL churn below (the dominant dynamic-caster cost in crowded zones).
    Mat4 casterModel, casterMv;
    if (!ComputeCasterWorld(casterModel, casterMv)) { s_skipsInPass++; return false; }
    {
        const float wp[3] = { casterModel.m[3], casterModel.m[7], casterModel.m[11] };
        bool inBox = CasterInLightBox(wp);
        // Log every CHAR caster on log-frames: world pos, anchor delta, box test.
        // Identifies whether character stands inside the light frustum on 804dro.
        if (cc == CC_CHAR && ShouldLogFrame()) {
            float dx = wp[0]-s_anchorWorld[0], dy = wp[1]-s_anchorWorld[1], dz = wp[2]-s_anchorWorld[2];
            float dist = sqrtf(dx*dx+dy*dy+dz*dz);
            // Also log where this caster's ORIGIN lands in the shadow tex (light
            // NDC → texel). If its shadow never shows on the receiver, this reveals
            // whether the caster is projecting onto the expected texel (mismatched
            // K) or off the [0,N] map (clamp → no shadow). Diagnosing 602dan.
            float tu = -1.0f, tv = -1.0f;
            if (s_lightKValid) {
                Mat4 lmvp;  M4Mul(lmvp, s_KmainForCache, casterModel);
                double ow = lmvp.m[15];
                if (ow != 0.0) {
                    float nx = (float)(lmvp.m[3]/ow), ny = (float)(lmvp.m[7]/ow);
                    tu = (nx*0.5f + 0.5f) * (float)kShadowMapSize;
                    tv = (ny*0.5f + 0.5f) * (float)kShadowMapSize;
                }
            }
            DiagLog("[shadow] caster CHAR world=(%.2f,%.2f,%.2f) anchor=(%.2f,%.2f,%.2f) d=%.1f inBox=%d ext=%.0f texel=(%.0f,%.0f)/%d",
                    wp[0], wp[1], wp[2], s_anchorWorld[0], s_anchorWorld[1], s_anchorWorld[2],
                    dist, inBox ? 1 : 0, s_extentWorld, tu, tv, kShadowMapSize);
        }
        if (!inBox) { s_castCulledN++; return false; }
    }

    // We are going to cast → now save the engine viewport + color mask so AfterDraw
    // can restore them. (Deferred past the skip check: the ~30k non-casting draws/
    // frame must not pay these two glGet queries.)
    glGetIntegerv(GL_VIEWPORT, s_drawVp);
    glGetBooleanv(GL_COLOR_WRITEMASK, s_drawColorMask);

    GLint curVp = s_classVp;   // VP id ClassifyCaster already read for this draw

    // Skinned caster VP for characters, static otherwise.
    GLuint casterId = (cc == CC_CHAR && s_shadowVpSkinnedId != 0)
                    ? s_shadowVpSkinnedId : s_shadowVpId;
    if (casterId == 0 || !orig_glBindProgram) {
        s_skipsInPass++;
        return false;
    }

    s_glBindFB(GL_FRAMEBUFFER_EXT, s_fbo[s_writeIdx]);
    glViewport(0, 0, kShadowMapSize, kShadowMapSize);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    // Slope-scaled polygon offset on caster side: pushes stored depth away
    // from light by (slope * dz/dx + constant * eps). Replaces fragile
    // constant FP-side bias; auto-handles steep angle surfaces (floor at
    // grazing sun → big bias, wall normal-to-sun → small bias).
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, ShadowPolyUnits(s_extentWorld));

    // Disable back-face culling for the caster pass. The engine culls faces
    // back-facing to the CAMERA, but the shadow map must capture every face that
    // blocks the SUN. A face hidden from the player yet lit by the sun was being
    // culled → it cast no shadow, and self-shadowing of the lit-but-hidden side
    // broke (dark side flips to lit on motion). Render both sides into depth.
    s_drawSavedCull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    // Diag: lock the FIRST CHARACTER for the depth probe, not whatever random
    // rigid model/placeable happens to be the first live caster. A static prop's
    // origin lands on a near-flat, low-contrast neighbourhood, so the probe reads
    // centre≈neighbours (spread≈0) and fakes a "no shadow stored" verdict while
    // the real character is never examined. Locking on the first CC_CHAR gives
    // the probe an actually diagnostic target (limbs at widely varying light
    // depth around the origin texel). A CHAR OVERRIDES an earlier generic lock:
    // PushLightMVPForDraw may have grabbed + logged the first caster (a MODEL)
    // before any character was classified, so gate on !s_diagLockIsChar (not
    // casterLogged) — the first CHAR re-points the lock and clears the log flag
    // so PushLightMVPForDraw re-logs it as a character this same draw.
    if (s_diag && !s_diagLockIsChar && cc == CC_CHAR) {
        GLint vbo0 = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &vbo0);
        if (vbo0 != 0) {
            s_diagLockVbo = (GLuint)vbo0;
            s_diagLockIsChar = true;
            s_diagCasterLogged = false;   // re-log as CHAR (PushLightMVPForDraw does it)
        }
    }

    PushLightMVPForDraw(casterModel, casterMv);

    // Substitute K2's VP with chosen caster. Save all program state precisely so
    // AfterDraw restores K2 exactly (fixed-function, enabled-or-not, id).
    s_drawSavedVp   = curVp;
    s_drawSavedVpEn = glIsEnabled(GL_VERTEX_PROGRAM_ARB);
    s_drawSavedFpEn = glIsEnabled(GL_FRAGMENT_PROGRAM_ARB);
    orig_glBindProgram(GL_VERTEX_PROGRAM_ARB, casterId);
    glEnable(GL_VERTEX_PROGRAM_ARB);

    // Punchthrough cutouts (grates, foliage): when the engine has GL_ALPHA_TEST on
    // for this draw, it's an alpha-masked material — bind the alpha-test caster FP
    // so transparent texels are KIL'd and the shadow follows the cutout (leaf
    // shapes, not solid rectangles). The diffuse the FP samples (texture[0]) is the
    // one the engine just bound at TMU 0. Opaque draws (incl. reflective env, where
    // low diffuse alpha means MIRROR not cutout) keep the FP disabled → solid depth.
    s_drawBoundAlphaFp = false;
    if (s_shadowFpAlphaId != 0 && glIsEnabled(GL_ALPHA_TEST)) {
        s_drawSavedFp = 0;
        glGetIntegerv(GL_FRAGMENT_PROGRAM_BINDING_ARB, &s_drawSavedFp);
        orig_glBindProgram(GL_FRAGMENT_PROGRAM_ARB, s_shadowFpAlphaId);
        glEnable(GL_FRAGMENT_PROGRAM_ARB);
        s_drawBoundAlphaFp = true;
    } else {
        glDisable(GL_FRAGMENT_PROGRAM_ARB);
    }
    if (cc >= 0 && cc < 4) s_castLiveN[cc]++;   // live caster rendered this draw
    return true;
}

void ShadowMap_AfterDraw()
{
    // Only ever called after BeforeDraw returned true, so a caster VP was
    // bound and orig_glBindProgram is valid.

    // --- Depth probe: shadow FBO is still bound (unbind next line). If this is
    // the locked caster, read back what depth it actually stored and compare the
    // centre texel (the caster) vs a small neighbourhood (the floor around it).
    // A caster that really writes depth shows centre ≪ neighbours (it's nearer the
    // sun than the ground under it). centre ≈ neighbours = it stored nothing → the
    // map never got its depth, pointing at the write side (VP/FP/depth-mask/offset).
    if (s_diag && s_diagCasterLogged && s_probeVbo == s_diagLockVbo &&
        ShouldLogFrame() && s_probeTexelU > 1.0 && s_probeTexelV > 1.0 &&
        s_probeTexelU < kShadowMapSize - 1.0 && s_probeTexelV < kShadowMapSize - 1.0) {
        // Near-zenith sun: the feet texel reads bare floor (spread≈0) even when the
        // caster writes — the body's ground footprint shifts only ~0.5 units off
        // origin. The elevated body probe (world-up ~1.6 units) lands on the torso,
        // which is much nearer the sun than the floor → clear write-side contrast.
        // Helper: read NxN at (u,v), return center/min/max/spread.
        struct Cell { float ctr, mn, mx; };
        auto probeAt = [&](double u, double v) -> Cell {
            const int N = 5;
            GLfloat px[N * N];
            int px0 = (int)u - N / 2;
            int py0 = (int)v - N / 2;
            if (px0 < 0) px0 = 0;
            if (py0 < 0) py0 = 0;
            while (glGetError() != GL_NO_ERROR) {}
            glReadPixels(px0, py0, N, N, GL_DEPTH_COMPONENT, GL_FLOAT, px);
            GLenum err = glGetError();
            Cell c;
            c.ctr = px[(N/2)*N + (N/2)];
            c.mn = 1e30f; c.mx = -1e30f;
            for (int i = 0; i < N * N; i++) {
                float v = px[i];
                if (v < c.mn) c.mn = v;
                if (v > c.mx) c.mx = v;
            }
            (void)err;
            return c;
        };
        Cell feet = probeAt(s_probeTexelU, s_probeTexelV);
        DiagLog("[diag]   casterDepth FEET texel=(%.1f,%.1f) center=%.4f min=%.4f "
                "max=%.4f spread=%.4f",
                s_probeTexelU, s_probeTexelV, feet.ctr, feet.mn, feet.mx,
                feet.mx - feet.mn);
        if (s_probeBodyValid > 0.5 && s_probeBodyU > 1.0 && s_probeBodyV > 1.0 &&
            s_probeBodyU < kShadowMapSize - 1.0 && s_probeBodyV < kShadowMapSize - 1.0) {
            Cell body = probeAt(s_probeBodyU, s_probeBodyV);
            // Negative center-minus-floor spread = the torso stored real shadow depth.
            DiagLog("[diag]   casterDepth BODY texel=(%.1f,%.1f) center=%.4f min=%.4f "
                    "max=%.4f spread=%.4f (feet_center=%.4f)",
                    s_probeBodyU, s_probeBodyV, body.ctr, body.mn, body.mx,
                    body.mx - body.mn, feet.ctr);
        }
    }

    s_glBindFB(GL_FRAMEBUFFER_EXT, 0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    glViewport(s_drawVp[0], s_drawVp[1], s_drawVp[2], s_drawVp[3]);
    glColorMask(s_drawColorMask[0], s_drawColorMask[1],
                s_drawColorMask[2], s_drawColorMask[3]);
    // Restore regardless of saved id — including 0 (= unbind / fixed-function).
    orig_glBindProgram(GL_VERTEX_PROGRAM_ARB, (GLuint)s_drawSavedVp);
    if (s_drawSavedVpEn) glEnable(GL_VERTEX_PROGRAM_ARB);
    else                 glDisable(GL_VERTEX_PROGRAM_ARB);
    // If we bound the alpha caster FP, restore the engine's FP binding before
    // restoring its enabled state (we overwrote the FP binding, not just enable).
    if (s_drawBoundAlphaFp)
        orig_glBindProgram(GL_FRAGMENT_PROGRAM_ARB, (GLuint)s_drawSavedFp);
    if (s_drawSavedFpEn) glEnable(GL_FRAGMENT_PROGRAM_ARB);
    else                 glDisable(GL_FRAGMENT_PROGRAM_ARB);
    if (s_drawSavedCull) glEnable(GL_CULL_FACE);
}

// ---- Geometry cache: capture + replay --------------------------------------

void ShadowMap_SetRealDrawFns(void *drawElements, void *drawArrays)
{
    s_realDrawElements = (PFNGLDRAWELEMENTSPROC)drawElements;
    s_realDrawArrays   = (PFNGLDRAWARRAYSPROC)drawArrays;
}

static uint64_t HashMix(uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

// Capture a static caster mesh into the cache instead of live double-drawing it.
// Returns true when the draw was handled by the cache (caller skips the live
// shadow draw). Returns false for non-cacheable draws (skinned, client arrays,
// non-triangles, sub-window, non-world FP, cache off) → caller's live path runs.
bool ShadowMap_TryCaptureCaster(GLenum mode, GLsizei count, GLenum type,
                                const GLvoid *indices, int isArrays, GLint first)
{
    if (!s_available || !s_inCaster) return false;

    CasterClass cc = ClassifyCaster();
    s_pendingClass = cc;                       // BeforeDraw (next, if we return false) reuses this
    if (cc == CC_SKIP)         return false;   // not a caster → live path also skips
    if (!CategoryEnabled(cc))  return true;    // category off → suppress (no shadow)

    // ONLY level geometry (fp_worldtex) is cached. Models/characters/held items
    // move, but many are drawn with a static VP — caching them would leave a
    // trail of stale shadows at their past positions. They go down the live
    // path (re-drawn fresh every frame) instead.
    if (cc != CC_GEOM || !s_useCache) return false;
    if (mode != GL_TRIANGLES || count < 3) return false;

    // Must be VBO-backed (we replay next frame; client-array memory is volatile).
    if (!glIsEnabled(GL_VERTEX_ARRAY)) return false;
    GLint arrayVbo = 0; glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &arrayVbo);
    if (arrayVbo == 0) return false;
    GLint elemVbo = 0;
    if (!isArrays) {
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elemVbo);
        if (elemVbo == 0) return false;
    }

    // model = view_inv * modelview (world transform of this draw).
    Mat4 viewInv;
    if (!ReadEngineViewInv(viewInv)) return true;   // can't place it; still "handled"
    GLfloat mvc[16]; glGetFloatv(GL_MODELVIEW_MATRIX, mvc);
    Mat4 mv;
    for (int rr = 0; rr < 4; rr++)
        for (int cc = 0; cc < 4; cc++)
            mv.m[rr*4 + cc] = mvc[cc*4 + rr];
    Mat4 model; M4Mul(model, viewInv, mv);

    void *vptr = NULL; glGetPointerv(GL_VERTEX_ARRAY_POINTER, &vptr);

    // Dedup key: same VBO/range AND same world placement (instances of one mesh
    // at different spots get distinct entries via quantised translation).
    uint64_t key = 1469598103934665603ULL;
    key = HashMix(key, (uint64_t)(uint32_t)arrayVbo);
    key = HashMix(key, (uint64_t)(uintptr_t)vptr);
    key = HashMix(key, (uint64_t)(uint32_t)elemVbo);
    key = HashMix(key, (uint64_t)(uintptr_t)indices);
    key = HashMix(key, (uint64_t)(uint32_t)count);
    key = HashMix(key, (uint64_t)(int32_t)floorf(model.m[3]  * 4.0f));
    key = HashMix(key, (uint64_t)(int32_t)floorf(model.m[7]  * 4.0f));
    key = HashMix(key, (uint64_t)(int32_t)floorf(model.m[11] * 4.0f));

    std::unordered_map<uint64_t,int>::iterator it = s_cacheKey.find(key);
    if (it != s_cacheKey.end()) {
        s_cache[it->second].lastSeen = s_frameNum;     // refresh; already cached
        return true;
    }
    if (s_cache.size() >= kMaxCache) return true;       // full: handled, not stored

    CachedDraw cd;
    cd.arrayVbo = (GLuint)arrayVbo;
    cd.vptr     = vptr;
    GLint vs = 3, vt = GL_FLOAT, vstr = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE,   &vs);
    glGetIntegerv(GL_VERTEX_ARRAY_TYPE,   &vt);
    glGetIntegerv(GL_VERTEX_ARRAY_STRIDE, &vstr);
    cd.vsize = vs; cd.vtype = (GLenum)vt; cd.vstride = vstr;
    cd.elemVbo  = (GLuint)elemVbo;
    cd.indices  = indices;
    cd.count    = count;
    cd.itype    = type;
    cd.first    = first;
    cd.isArrays = (isArrays != 0);
    cd.model    = model;
    cd.center[0] = model.m[3]; cd.center[1] = model.m[7]; cd.center[2] = model.m[11];
    cd.lastSeen = s_frameNum;
    cd.key      = key;

    // Punchthrough: if the engine alpha-tests this draw, record the diffuse + its
    // UV array so the replay can KIL transparent texels (cutout shadows). Solid
    // alpha-test surfaces (alpha=1) cast solid anyway. Only enable the alpha path
    // when a usable texcoord0 array exists; otherwise fall back to depth-only.
    cd.alphaTest  = false;
    cd.diffuseTex = 0; cd.tcVbo = 0; cd.tcptr = NULL;
    cd.tcsize = 2; cd.tctype = GL_FLOAT; cd.tcstride = 0;
    if (glIsEnabled(GL_ALPHA_TEST) && s_shadowFpAlphaId != 0 && glIsEnabled(GL_TEXTURE_COORD_ARRAY)) {
        GLint t0 = 0;
        if (s_glActiveTexture) {
            GLint act = GL_TEXTURE0; glGetIntegerv(GL_ACTIVE_TEXTURE, &act);
            s_glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &t0);
            s_glActiveTexture((GLenum)act);
        } else {
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &t0);
        }
        void *tp = NULL; glGetPointerv(GL_TEXTURE_COORD_ARRAY_POINTER, &tp);
        GLint ts = 2, tt = GL_FLOAT, tstr = 0, tvbo = 0;
        glGetIntegerv(GL_TEXTURE_COORD_ARRAY_SIZE,   &ts);
        glGetIntegerv(GL_TEXTURE_COORD_ARRAY_TYPE,   &tt);
        glGetIntegerv(GL_TEXTURE_COORD_ARRAY_STRIDE, &tstr);
        glGetIntegerv(GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING, &tvbo);
        if (t0 != 0 && tvbo != 0) {   // need a real diffuse + VBO-backed UVs to replay safely
            cd.alphaTest = true;
            cd.diffuseTex = (GLuint)t0;
            cd.tcVbo = (GLuint)tvbo; cd.tcptr = tp;
            cd.tcsize = ts; cd.tctype = (GLenum)tt; cd.tcstride = tstr;
        }
    }

    s_cacheKey[key] = (int)s_cache.size();
    s_cache.push_back(cd);
    s_cacheVersion++;          // static map must re-render to include this mesh
    return true;
}

// Drop meshes the engine hasn't re-submitted recently or that are far from the
// player (handles area changes + bounds memory). Rebuilds the key index.
static void EvictCache()
{
    if (s_cache.empty()) return;
    // Keep geometry out to the light box + margin (scales with "Shadow range")
    // so raising range doesn't evict casters that should still shadow.
    float evict = s_extentWorld * 2.0f + 100.0f;
    if (evict < kCacheEvictDist) evict = kCacheEvictDist;
    std::vector<CachedDraw> keep;
    keep.reserve(s_cache.size());
    for (size_t i = 0; i < s_cache.size(); i++) {
        const CachedDraw &cd = s_cache[i];
        if (s_frameNum - cd.lastSeen > kCacheStale) continue;
        float dx = cd.center[0]-s_anchorWorld[0];
        float dy = cd.center[1]-s_anchorWorld[1];
        float dz = cd.center[2]-s_anchorWorld[2];
        if (dx*dx + dy*dy + dz*dz > evict*evict) continue;
        keep.push_back(cd);
    }
    if (keep.size() != s_cache.size()) {
        s_cache.swap(keep);
        s_cacheKey.clear();
        for (size_t i = 0; i < s_cache.size(); i++)
            s_cacheKey[s_cache[i].key] = (int)i;
        s_cacheVersion++;      // static map must re-render without evicted meshes
    }
}

// Render the whole static cache into targetFbo from the sun's POV. Called at
// frame start so the map is COMPLETE before the main pass. With the split active
// targetFbo is the retained static FBO (rendered only when dirty); otherwise it
// is this frame's work tex (fallback, redrawn every frame).
static void RenderCacheInto(GLuint targetFbo)
{
    if (!s_useCache || s_cache.empty() || !s_realDrawElements || !s_realDrawArrays ||
        !s_glBindBuffer || !s_glBindFB || !orig_glBindProgram ||
        !orig_glProgramEnvParameter4d || s_shadowVpId == 0)
        return;

    // --- save engine state we touch ---
    GLint sFbo = 0;  glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &sFbo);
    GLint sVp = 0;   glGetIntegerv(GL_VERTEX_PROGRAM_BINDING_ARB, &sVp);
    GLboolean sVpEn = glIsEnabled(GL_VERTEX_PROGRAM_ARB);
    GLboolean sFpEn = glIsEnabled(GL_FRAGMENT_PROGRAM_ARB);
    GLint sArr = 0;  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &sArr);
    GLint sElem = 0; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &sElem);
    GLboolean sVA = glIsEnabled(GL_VERTEX_ARRAY);
    GLint sVabuf = 0, sVAsz = 4, sVAtype = GL_FLOAT, sVAstr = 0;
    void *sVAptr = NULL;
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &sVabuf);
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE,   &sVAsz);
    glGetIntegerv(GL_VERTEX_ARRAY_TYPE,   &sVAtype);
    glGetIntegerv(GL_VERTEX_ARRAY_STRIDE, &sVAstr);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &sVAptr);
    GLint sViewport[4]; glGetIntegerv(GL_VIEWPORT, sViewport);
    GLboolean sCM[4];   glGetBooleanv(GL_COLOR_WRITEMASK, sCM);
    GLboolean sDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean sCull = glIsEnabled(GL_CULL_FACE);
    // Texcoord-array + TMU0 state we touch for alpha-test (punchthrough) replay.
    GLint sFp = 0; glGetIntegerv(GL_FRAGMENT_PROGRAM_BINDING_ARB, &sFp);
    GLboolean sTC = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
    GLint sTCbuf = 0, sTCsz = 2, sTCtype = GL_FLOAT, sTCstr = 0;
    void *sTCptr = NULL;
    glGetIntegerv(GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING, &sTCbuf);
    glGetIntegerv(GL_TEXTURE_COORD_ARRAY_SIZE,   &sTCsz);
    glGetIntegerv(GL_TEXTURE_COORD_ARRAY_TYPE,   &sTCtype);
    glGetIntegerv(GL_TEXTURE_COORD_ARRAY_STRIDE, &sTCstr);
    glGetPointerv(GL_TEXTURE_COORD_ARRAY_POINTER, &sTCptr);
    GLint sTex0 = 0;
    if (s_glActiveTexture) {
        GLint a = GL_TEXTURE0; glGetIntegerv(GL_ACTIVE_TEXTURE, &a);
        s_glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &sTex0);
        s_glActiveTexture((GLenum)a);
    }
    bool curAlpha = false;          // replay starts depth-only (FP disabled below)
    bool touchedAlpha = false;      // did ANY entry use the alpha path? (gates restore)

    // --- set up the caster pass ---
    s_glBindFB(GL_FRAMEBUFFER_EXT, targetFbo);
    glViewport(0, 0, kShadowMapSize, kShadowMapSize);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);   // capture every sun-facing face, not just camera-facing
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, ShadowPolyUnits(s_extentWorld));
    orig_glBindProgram(GL_VERTEX_PROGRAM_ARB, s_shadowVpId);
    glEnable(GL_VERTEX_PROGRAM_ARB);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glEnableClientState(GL_VERTEX_ARRAY);

    // Per-frame replay cull: the cache holds geometry out to the eviction radius
    // (≥280u) so the player can turn/backtrack without re-capturing, but only
    // meshes that can cast into the current light box (half-width = extent) need
    // to be drawn THIS frame. Cull by horizontal (XY) distance from the anchor —
    // Z-up world, so this won't over-cull tall objects sitting at the box edge.
    // Distant cached meshes stay cached (zero re-capture cost) but skip the draw.
    const float replayR  = s_extentWorld * 1.6f + 32.0f;
    const float replayR2 = replayR * replayR;

    int drawn = 0, culled = 0;
    Mat4 lastModel; bool haveLastModel = false;
    GLuint lastArrayVbo = 0, lastElemVbo = 0;
    for (size_t i = 0; i < s_cache.size(); i++) {
        const CachedDraw &cd = s_cache[i];
        float dx = cd.center[0]-s_anchorWorld[0];
        float dy = cd.center[1]-s_anchorWorld[1];
        if (dx*dx + dy*dy > replayR2) { culled++; continue; }
        // Safety: skip meshes whose VBO was deleted (area change between the
        // capture and now). Prevents drawing a dead/reused buffer.
        if (s_glIsBuffer && !s_glIsBuffer(cd.arrayVbo)) continue;
        // lightMVP = K_main * model → caster VP does lightMVP * vertex.position.
        // Most level geometry shares one model transform (often identity), so
        // only recompute + push the 4 matrix rows when it actually changes —
        // saves ~4 GL calls per mesh across hundreds of meshes.
        if (!haveLastModel || memcmp(&cd.model, &lastModel, sizeof(Mat4)) != 0) {
            Mat4 lmvp; M4Mul(lmvp, s_KmainForCache, cd.model);   // cache replays into this frame's light box
            for (int row = 0; row < 4; row++)
                orig_glProgramEnvParameter4d(GL_VERTEX_PROGRAM_ARB, 100 + row,
                    (double)lmvp.m[row*4+0], (double)lmvp.m[row*4+1],
                    (double)lmvp.m[row*4+2], (double)lmvp.m[row*4+3]);
            lastModel = cd.model; haveLastModel = true;
        }

        if (cd.arrayVbo != lastArrayVbo) {
            s_glBindBuffer(GL_ARRAY_BUFFER, cd.arrayVbo);
            lastArrayVbo = cd.arrayVbo;
        }

        // Punchthrough replay: alpha-test entries bind the cutout FP + their diffuse
        // + UV array so transparent texels are KIL'd (leaf/grate-shaped shadows);
        // others stay depth-only. Switch FP/array-enable only when the mode changes.
        bool useAlpha = cd.alphaTest;
        if (useAlpha != curAlpha) {
            if (useAlpha) {
                orig_glBindProgram(GL_FRAGMENT_PROGRAM_ARB, s_shadowFpAlphaId);
                glEnable(GL_FRAGMENT_PROGRAM_ARB);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            } else {
                glDisable(GL_FRAGMENT_PROGRAM_ARB);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            }
            curAlpha = useAlpha;
        }
        if (useAlpha) {
            touchedAlpha = true;
            if (s_glActiveTexture) s_glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, cd.diffuseTex);
            s_glBindBuffer(GL_ARRAY_BUFFER, cd.tcVbo);
            glTexCoordPointer(cd.tcsize, cd.tctype, cd.tcstride, cd.tcptr);
            s_glBindBuffer(GL_ARRAY_BUFFER, cd.arrayVbo);   // restore for glVertexPointer
            lastArrayVbo = cd.arrayVbo;
        }
        glVertexPointer(cd.vsize, cd.vtype, cd.vstride, cd.vptr);
        if (cd.isArrays) {
            s_realDrawArrays(GL_TRIANGLES, cd.first, cd.count);
        } else {
            if (cd.elemVbo != lastElemVbo) {
                s_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cd.elemVbo);
                lastElemVbo = cd.elemVbo;
            }
            s_realDrawElements(GL_TRIANGLES, cd.count, cd.itype, cd.indices);
        }
        drawn++;
    }

    // --- restore engine state ---
    // Restore the texcoord0 array + TMU0 binding if the alpha-test path touched them.
    if (touchedAlpha) {
        s_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)sTCbuf);
        glTexCoordPointer(sTCsz, (GLenum)sTCtype, sTCstr, sTCptr);
        if (!sTC) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        if (s_glActiveTexture) s_glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)sTex0);
    }
    s_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)sVabuf);
    glVertexPointer(sVAsz, (GLenum)sVAtype, sVAstr, sVAptr);
    s_glBindBuffer(GL_ARRAY_BUFFER, (GLuint)sArr);
    s_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)sElem);
    if (!sVA) glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    if (!sDepth) glDisable(GL_DEPTH_TEST);
    if (sCull) glEnable(GL_CULL_FACE);
    orig_glBindProgram(GL_VERTEX_PROGRAM_ARB, (GLuint)sVp);
    orig_glBindProgram(GL_FRAGMENT_PROGRAM_ARB, (GLuint)sFp);   // restore engine FP (we may have bound the alpha caster FP)
    if (sVpEn) glEnable(GL_VERTEX_PROGRAM_ARB); else glDisable(GL_VERTEX_PROGRAM_ARB);
    if (sFpEn) glEnable(GL_FRAGMENT_PROGRAM_ARB); else glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glViewport(sViewport[0], sViewport[1], sViewport[2], sViewport[3]);
    glColorMask(sCM[0], sCM[1], sCM[2], sCM[3]);
    s_glBindFB(GL_FRAMEBUFFER_EXT, (GLuint)sFbo);

    if (ShouldLogFrame())
        DiagLog("[shadow] RenderCache: meshes=%u drawn=%d culled=%d (replayR=%.0f)",
                (unsigned)s_cache.size(), drawn, culled, replayR);
}

void ShadowMap_OnDraw()
{
    if (!s_inCaster) return;
    s_drawsInPass++;
    if (!ShouldLogFrame()) return;

    // OnDraw fires at the START of each engine draw (before the caster swap), so
    // the bound FP/VP and TMU0 texture are the ENGINE's for this draw — i.e. the
    // RECEIVER. Build a per-draw inventory keyed by fp/vp/diffuse-name so the user
    // can identify what grass, steam, etc. are drawn with (then we can add shadow
    // receive to that FP/VP). aTest flags punchthrough (alpha-tested) materials.
    GLint fp = 0, vp = 0, tex0 = 0, tex1 = 0;
    glGetIntegerv(GL_FRAGMENT_PROGRAM_BINDING_ARB, &fp);
    glGetIntegerv(GL_VERTEX_PROGRAM_BINDING_ARB,   &vp);
    if (s_glActiveTexture) {
        GLint act = GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &act);
        s_glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
        s_glActiveTexture(GL_TEXTURE1);                 // lightmap slot — presence ⇒ interior/lit geometry
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex1);
        s_glActiveTexture((GLenum)act);
    } else {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
    }
    const char *fpN  = GetProgramName((GLuint)fp);
    const char *vpN  = GetProgramName((GLuint)vp);
    const char *texN = PbrGetTextureName((GLuint)tex0);
    const char *lmN  = PbrGetTextureName((GLuint)tex1);   // shader-usage census: which surfaces are lightmapped
    int at = glIsEnabled(GL_ALPHA_TEST) ? 1 : 0;
    int bl = glIsEnabled(GL_BLEND) ? 1 : 0;   // blend enabled broadly by engine — weak signal alone
    GLboolean dm = 1; glGetBooleanv(GL_DEPTH_WRITEMASK, &dm);  // particles/steam write NO depth (dMask=0) — opaque world = 1
    // env[23].y = billboardShadow flag the shader actually sees at THIS draw.
    float e23[4] = { -9, -9, -9, -9 };
    if (s_glGetEnvFv) s_glGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 23, e23);
    char key[320];
    snprintf(key, sizeof(key), "fp=%-26s vp=%-22s aTest=%d blend=%d dMask=%d bb=%.1f lm=%-12s tex=%s",
             (fpN && fpN[0]) ? fpN : "?",
             (vpN && vpN[0]) ? vpN : "?", at, bl, (int)dm, e23[1],
             (lmN && lmN[0]) ? lmN : "-",
             (texN && texN[0]) ? texN : "?");
    s_progHist[key]++;
}

// Static caster: lightMVP * model-space vertex.position.
// Also passes the diffuse UV through to texcoord[0] so the alpha-test caster FP
// (kShadowFpAlphaSource) can sample the diffuse and KIL transparent texels —
// gives leaf/grate cutouts real shadows instead of solid rectangles. Harmless
// extra varying for the opaque depth-only path (FP disabled there).
static const char kShadowVpSource[] =
    "!!ARBvp1.0\n"
    "PARAM lightMVP[4] = { program.env[100..103] };\n"
    "MOV result.texcoord[0], vertex.texcoord[0];\n"
    "DP4 result.position.x, lightMVP[0], vertex.position;\n"
    "DP4 result.position.y, lightMVP[1], vertex.position;\n"
    "DP4 result.position.z, lightMVP[2], vertex.position;\n"
    "DP4 result.position.w, lightMVP[3], vertex.position;\n"
    "END\n";

// Skinned caster: 4-bone palette blend, then lightMVP. Bone array at
// env[18..68] (51 slots = 17 bones × 3 rows) set by K2 per draw.
// vertex.attrib[1] = weights, vertex.attrib[4] = bone indices × env[16].z.
// Pattern matches vp_skinned_env_lit.txt; see deploy copy for spaced-out version.
static const char kShadowVpSkinnedSource[] =
    "!!ARBvp1.0\n"
    "PARAM lightMVP[4]   = { program.env[100..103] };\n"
    "PARAM boneArray[51] = { program.env[18..68] };\n"
    "ATTRIB vWeight      = vertex.attrib[1];\n"
    "ADDRESS A0;\n"
    "TEMP vReg0, vReg1, vReg2, vReg3, vReg4, vReg9;\n"
    "MUL vReg0, vertex.attrib[4], program.env[16].zzzz;\n"
    "ARL A0.x, vReg0.x;\n"
    "DP4 vReg1.x, vertex.position, boneArray[A0.x + 0];\n"
    "DP4 vReg1.y, vertex.position, boneArray[A0.x + 1];\n"
    "DP4 vReg1.z, vertex.position, boneArray[A0.x + 2];\n"
    "ARL A0.x, vReg0.y;\n"
    "DP4 vReg2.x, vertex.position, boneArray[A0.x + 0];\n"
    "DP4 vReg2.y, vertex.position, boneArray[A0.x + 1];\n"
    "DP4 vReg2.z, vertex.position, boneArray[A0.x + 2];\n"
    "ARL A0.x, vReg0.z;\n"
    "DP4 vReg3.x, vertex.position, boneArray[A0.x + 0];\n"
    "DP4 vReg3.y, vertex.position, boneArray[A0.x + 1];\n"
    "DP4 vReg3.z, vertex.position, boneArray[A0.x + 2];\n"
    "ARL A0.x, vReg0.w;\n"
    "DP4 vReg4.x, vertex.position, boneArray[A0.x + 0];\n"
    "DP4 vReg4.y, vertex.position, boneArray[A0.x + 1];\n"
    "DP4 vReg4.z, vertex.position, boneArray[A0.x + 2];\n"
    "MUL vReg9, vWeight.x, vReg1;\n"
    "MAD vReg9, vReg2, vWeight.y, vReg9;\n"
    "MAD vReg9, vReg3, vWeight.z, vReg9;\n"
    "MAD vReg9, vReg4, vWeight.w, vReg9;\n"
    "MOV vReg9.w, program.env[16].w;\n"
    "MOV result.texcoord[0], vertex.texcoord[0];\n"
    "DP4 result.position.x, lightMVP[0], vReg9;\n"
    "DP4 result.position.y, lightMVP[1], vReg9;\n"
    "DP4 result.position.z, lightMVP[2], vReg9;\n"
    "DP4 result.position.w, lightMVP[3], vReg9;\n"
    "END\n";

// Alpha-test caster FP: sample the diffuse (TMU 0, bound by the engine for this
// draw) and KIL fragments below the cutout threshold. Bound by ShadowMap_BeforeDraw
// ONLY when the engine has GL_ALPHA_TEST enabled (its own punchthrough signal) —
// so reflective env materials (low diffuse alpha = mirror, NOT a cutout) are never
// punched. result.color is unused (colormask off); only the KIL + depth matter.
static const char kShadowFpAlphaSource[] =
    "!!ARBfp1.0\n"
    "TEMP c;\n"
    "TEX c, fragment.texcoord[0], texture[0], 2D;\n"
    "SUB c.x, c.a, 0.5;\n"   // alpha - 0.5
    "KIL c.x;\n"             // discard where alpha < 0.5 (transparent cutout)
    "MOV result.color, c;\n"
    "END\n";

// Allocate + configure a shadow depth texture (work texes + the static-split
// tex share identical params). LINEAR filtering: NEAREST gives clean per-texel
// depth but the 4-tap manual PCF then shows hard discrete levels = visible
// stripes on large flat floors. LINEAR bilerps so the PCF levels blend into a
// smooth penumbra; the sub-texel "swim" it adds is minor once acne is handled
// receiver-side by the FP normal-offset. ARBfp1.0 TEX needs depth as .r
// luminance (COMPARE_MODE NONE), not a shadow-compare result.
static void ConfigureShadowDepthTex(GLuint tex)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE,   GL_LUMINANCE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 kShadowMapSize, kShadowMapSize, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
}

// Compile an ARB VP source to a fresh program id. Returns 0 on failure.
static GLuint CompileVp(const char *src, GLsizei len, const char *tag)
{
    PFNGLGENPROGRAMS glGenProg = Resolve<PFNGLGENPROGRAMS>("glGenProgramsARB");
    if (!glGenProg) {
        DiagLog("[shadow] %s: glGenProgramsARB missing", tag);
        return 0;
    }
    if (!orig_glBindProgram || !orig_glProgramString) {
        DiagLog("[shadow] %s: orig_glBindProgram/orig_glProgramString missing", tag);
        return 0;
    }

    while (glGetError() != GL_NO_ERROR) {}

    GLuint id = 0;
    glGenProg(1, &id);
    if (id == 0) {
        DiagLog("[shadow] %s: glGenProgramsARB returned 0", tag);
        return 0;
    }

    GLint savedVp = 0;
    glGetIntegerv(GL_VERTEX_PROGRAM_BINDING_ARB, &savedVp);

    orig_glBindProgram(GL_VERTEX_PROGRAM_ARB, id);
    orig_glProgramString(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, len, src);
    GLenum err = glGetError();
    orig_glBindProgram(GL_VERTEX_PROGRAM_ARB, (GLuint)savedVp);

    if (err == GL_NO_ERROR) {
        DiagLog("[shadow] %s loaded id=%u", tag, id);
        return id;
    }
    DiagLog("[shadow] %s compile failed err=0x%04x", tag, err);
    return 0;
}

// Compile an ARB FP source to a fresh program id. Mirror of CompileVp on the
// fragment target. Returns 0 on failure.
static GLuint CompileFp(const char *src, GLsizei len, const char *tag)
{
    PFNGLGENPROGRAMS glGenProg = Resolve<PFNGLGENPROGRAMS>("glGenProgramsARB");
    if (!glGenProg || !orig_glBindProgram || !orig_glProgramString) {
        DiagLog("[shadow] %s: missing GL entry points", tag);
        return 0;
    }
    while (glGetError() != GL_NO_ERROR) {}
    GLuint id = 0;
    glGenProg(1, &id);
    if (id == 0) { DiagLog("[shadow] %s: glGenProgramsARB returned 0", tag); return 0; }

    GLint savedFp = 0;
    glGetIntegerv(GL_FRAGMENT_PROGRAM_BINDING_ARB, &savedFp);
    orig_glBindProgram(GL_FRAGMENT_PROGRAM_ARB, id);
    orig_glProgramString(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, len, src);
    GLenum err = glGetError();
    orig_glBindProgram(GL_FRAGMENT_PROGRAM_ARB, (GLuint)savedFp);

    if (err == GL_NO_ERROR) { DiagLog("[shadow] %s loaded id=%u", tag, id); return id; }
    DiagLog("[shadow] %s compile failed err=0x%04x", tag, err);
    return 0;
}

static void LoadShadowVps()
{
    s_shadowVpId        = CompileVp(kShadowVpSource,        (GLsizei)(sizeof(kShadowVpSource)        - 1), "static caster VP");
    s_shadowVpSkinnedId = CompileVp(kShadowVpSkinnedSource, (GLsizei)(sizeof(kShadowVpSkinnedSource) - 1), "skinned caster VP");
    s_shadowFpAlphaId   = CompileFp(kShadowFpAlphaSource,   (GLsizei)(sizeof(kShadowFpAlphaSource)   - 1), "alpha-test caster FP");
}

void ShadowMap_Init()
{
    if (!orig_wglGetProcAddress) {
        DiagLog("[shadow] init skipped: orig_wglGetProcAddress=NULL");
        return;
    }

    s_glGenFB     = Resolve<PFNGLGENFRAMEBUFFERS>("glGenFramebuffersEXT");
    s_glBindFB    = Resolve<PFNGLBINDFRAMEBUFFER>("glBindFramebufferEXT");
    s_glDeleteFB  = Resolve<PFNGLDELETEFRAMEBUFFERS>("glDeleteFramebuffersEXT");
    s_glIsFB      = Resolve<PFNGLISFRAMEBUFFER>("glIsFramebufferEXT");
    s_glFBTex2D   = Resolve<PFNGLFRAMEBUFFERTEXTURE2D>("glFramebufferTexture2DEXT");
    s_glCheckFB   = Resolve<PFNGLCHECKFRAMEBUFFERSTATUS>("glCheckFramebufferStatusEXT");
    s_glGetEnvFv  = Resolve<PFNGLGETPROGRAMENVPARAMETERFV>("glGetProgramEnvParameterfvARB");
    s_glBlitFB    = Resolve<PFNGLBLITFRAMEBUFFER>("glBlitFramebufferEXT");
    if (!s_glBlitFB)
        s_glBlitFB = Resolve<PFNGLBLITFRAMEBUFFER>("glBlitFramebuffer");
    s_glActiveTexture = Resolve<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
    if (!s_glActiveTexture)
        s_glActiveTexture = Resolve<PFNGLACTIVETEXTUREPROC>("glActiveTextureARB");
    s_glBindBuffer = Resolve<PFNGLBINDBUFFERPROC>("glBindBuffer");
    if (!s_glBindBuffer)
        s_glBindBuffer = Resolve<PFNGLBINDBUFFERPROC>("glBindBufferARB");
    s_glIsBuffer = Resolve<PFNGLISBUFFERPROC>("glIsBuffer");
    if (!s_glIsBuffer)
        s_glIsBuffer = Resolve<PFNGLISBUFFERPROC>("glIsBufferARB");
    DiagLog("[shadow] geom cache: glBindBuffer=%p realDrawElem=%p realDrawArr=%p",
            (void*)s_glBindBuffer, (void*)s_realDrawElements, (void*)s_realDrawArrays);

    // Context/area was (re)created → every cached VBO id belongs to the old
    // context and is now invalid. Wipe the cache so RenderCache can't replay
    // stale buffers (garbage shadows + corrupted GL state for other passes).
    s_cache.clear();
    s_cacheKey.clear();

    if (!s_glGenFB || !s_glBindFB || !s_glFBTex2D || !s_glCheckFB) {
        DiagLog("[shadow] FBO entry points missing: gen=%p bind=%p fbtex=%p check=%p",
                s_glGenFB, s_glBindFB, s_glFBTex2D, s_glCheckFB);
        return;
    }

    // Re-init path (context/area reload): drop any prior FBO/tex. Stale ids from
    // a destroyed context are silently ignored by glDelete*; live ids (Init
    // called on the same context) are freed so we don't leak.
    s_available = false;
    s_KmainSampleValid = false;   // last frame's map/K belong to the old context → first frame falls back
    if (s_glDeleteFB && (s_fbo[0] || s_fbo[1]))      s_glDeleteFB(2, s_fbo);
    if (s_depthTex[0] || s_depthTex[1])              glDeleteTextures(2, s_depthTex);
    s_fbo[0] = s_fbo[1] = 0;
    s_depthTex[0] = s_depthTex[1] = 0;

    while (glGetError() != GL_NO_ERROR) {}

    // Create both ping-pong FBO/depth-tex pairs. All-or-nothing: any failure
    // leaves s_available false.
    GLenum status = 0;
    for (int i = 0; i < 2; i++) {
        s_glGenFB(1, &s_fbo[i]);

        glGenTextures(1, &s_depthTex[i]);
        ConfigureShadowDepthTex(s_depthTex[i]);

        s_glBindFB(GL_FRAMEBUFFER_EXT, s_fbo[i]);
        s_glFBTex2D(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
                    GL_TEXTURE_2D, s_depthTex[i], 0);
        status = s_glCheckFB(GL_FRAMEBUFFER_EXT);
        // Clear to far=1.0 (=lit in the SGE test) so frame 1 sampling the as-yet-
        // unwritten read tex returns "lit", not uninitialised garbage (dark blink).
        if (status == GL_FRAMEBUFFER_COMPLETE_EXT) glClear(GL_DEPTH_BUFFER_BIT);
        s_glBindFB(GL_FRAMEBUFFER_EXT, 0);

        GLenum err = glGetError();
        DiagLog("[shadow] FBO[%d] probe: fbo=%u tex=%u status=0x%04x err=0x%04x",
                i, s_fbo[i], s_depthTex[i], status, err);
        if (status != GL_FRAMEBUFFER_COMPLETE_EXT) break;
    }

    s_available = (status == GL_FRAMEBUFFER_COMPLETE_EXT);
    s_initCtx   = wglGetCurrentContext();   // bind resources to this context

    if (!s_available) {
        const char *reason = "unknown";
        switch (status) {
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:         reason = "incomplete attachment"; break;
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT: reason = "missing attachment";    break;
            case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:         reason = "incomplete dimensions"; break;
            case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:            reason = "incomplete formats";    break;
            case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:        reason = "incomplete draw buf";   break;
            case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:        reason = "incomplete read buf";   break;
            case GL_FRAMEBUFFER_UNSUPPORTED_EXT:                   reason = "unsupported combo";     break;
        }
        DiagLog("[shadow] FBO probe FAILED: %s", reason);
        return;
    }

    LoadShadowVps();
}
