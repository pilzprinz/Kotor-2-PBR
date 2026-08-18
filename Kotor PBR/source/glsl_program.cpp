/*
GLSL subsystem — Stage 0 probe implementation. See glsl_program.h.

Resolves the GL 2.0 / ARB_shader_objects entry points through the engine's
orig_wglGetProcAddress (same path the FBO + ARB-program functions use), logs the
GL/SL version + texture/draw-buffer limits, and proves a trivial program links.
Direct GL 1.1 calls (glGetString/glGetIntegerv) route through this proxy's own
export trampolines to the real driver, exactly like the rest of the mod.
*/

#include "platform.h"
#include "glsl_program.h"
#include "glFunctions.h"   // orig_wglGetProcAddress, GetProgramName
#include "pbr_tune.h"      // DiagLog
#include "pbr_state.h"     // PbrGetTextureName (per-draw census)
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <unordered_set>

// --- GL enums not in the old GL/gl.h ---------------------------------------
#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C
#endif
#ifndef GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_MAX_TEXTURE_IMAGE_UNITS        0x8872
#endif
#ifndef GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS 0x8B4D
#endif
#ifndef GL_MAX_DRAW_BUFFERS
#define GL_MAX_DRAW_BUFFERS               0x8824
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER                  0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER                0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS                 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS                    0x8B82
#endif
#ifndef GL_FRAGMENT_PROGRAM_BINDING_ARB
#define GL_FRAGMENT_PROGRAM_BINDING_ARB   0x8873
#endif
#ifndef GL_VERTEX_PROGRAM_BINDING_ARB
#define GL_VERTEX_PROGRAM_BINDING_ARB     0x864A
#endif
#ifndef GL_VERTEX_PROGRAM_ARB
#define GL_VERTEX_PROGRAM_ARB             0x8620
#endif
#ifndef GL_FRAGMENT_PROGRAM_ARB
#define GL_FRAGMENT_PROGRAM_ARB           0x8804
#endif
#ifndef GL_FOG
#define GL_FOG                            0x0B60
#endif
#ifndef GL_FOG_START
#define GL_FOG_START                      0x0B63
#endif
#ifndef GL_FOG_END
#define GL_FOG_END                        0x0B64
#endif
#ifndef GL_FOG_COLOR
#define GL_FOG_COLOR                      0x0B66
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM                0x8B8D
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE                 0x84E0
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0                       0x84C0
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D             0x8069
#endif

typedef char GLchar;

// --- GL 2.0 entry-point typedefs (WINAPI = __stdcall, as wglGetProcAddress gives) ---
typedef GLuint (WINAPI *PFN_glCreateShader)(GLenum);
typedef void   (WINAPI *PFN_glShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (WINAPI *PFN_glCompileShader)(GLuint);
typedef void   (WINAPI *PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef void   (WINAPI *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (WINAPI *PFN_glCreateProgram)(void);
typedef void   (WINAPI *PFN_glAttachShader)(GLuint, GLuint);
typedef void   (WINAPI *PFN_glLinkProgram)(GLuint);
typedef void   (WINAPI *PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void   (WINAPI *PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void   (WINAPI *PFN_glUseProgram)(GLuint);
typedef GLint  (WINAPI *PFN_glGetUniformLocation)(GLuint, const GLchar*);
typedef void   (WINAPI *PFN_glUniform1i)(GLint, GLint);
typedef void   (WINAPI *PFN_glUniform1f)(GLint, GLfloat);
typedef void   (WINAPI *PFN_glUniform3fv)(GLint, GLsizei, const GLfloat*);
typedef void   (WINAPI *PFN_glUniform4fv)(GLint, GLsizei, const GLfloat*);
typedef void   (WINAPI *PFN_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void   (WINAPI *PFN_glDeleteShader)(GLuint);
typedef void   (WINAPI *PFN_glDeleteProgram)(GLuint);
typedef void   (WINAPI *PFN_glBindAttribLocation)(GLuint, GLuint, const GLchar*);
typedef void   (WINAPI *PFN_glGetEnvFv)(GLenum, GLuint, GLfloat*);   // glGetProgramEnvParameterfvARB
typedef void   (WINAPI *PFN_glActiveTexture)(GLenum);                 // census: read TMU0 binding

static PFN_glCreateShader        pglCreateShader        = 0;
static PFN_glShaderSource        pglShaderSource        = 0;
static PFN_glCompileShader       pglCompileShader       = 0;
static PFN_glGetShaderiv         pglGetShaderiv         = 0;
static PFN_glGetShaderInfoLog    pglGetShaderInfoLog    = 0;
static PFN_glCreateProgram       pglCreateProgram       = 0;
static PFN_glAttachShader        pglAttachShader        = 0;
static PFN_glLinkProgram         pglLinkProgram         = 0;
static PFN_glGetProgramiv        pglGetProgramiv        = 0;
static PFN_glGetProgramInfoLog   pglGetProgramInfoLog   = 0;
static PFN_glUseProgram          pglUseProgram          = 0;
static PFN_glGetUniformLocation  pglGetUniformLocation  = 0;
static PFN_glUniform1i           pglUniform1i           = 0;
static PFN_glUniform1f           pglUniform1f           = 0;
static PFN_glUniform3fv          pglUniform3fv          = 0;
static PFN_glUniform4fv          pglUniform4fv          = 0;
static PFN_glUniformMatrix4fv    pglUniformMatrix4fv    = 0;
static PFN_glDeleteShader        pglDeleteShader        = 0;
static PFN_glDeleteProgram       pglDeleteProgram       = 0;
static PFN_glBindAttribLocation  pglBindAttribLocation  = 0;
static PFN_glGetEnvFv            pglGetEnvFv            = 0;   // reads engine/mod env params
static PFN_glActiveTexture       pglActiveTexture       = 0;   // census only

static bool s_available = false;

bool Glsl_Available() { return s_available; }

// DLL directory (units live next to opengl32.dll → its logs/ and shaders_override/).
// Local copy (static in this TU) so this file doesn't depend on pbr_config.cpp's
// private helper. Mirrors pbr_config.cpp / pbr_tune.cpp.
static void GetDllDirPath(char *out, size_t outSize)
{
    HMODULE hMod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&GetDllDirPath, &hMod);
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(hMod, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    if (slash) slash[1] = 0;
    else path[0] = 0;
    strncpy(out, path, outSize - 1);
    out[outSize - 1] = 0;
}

// Dedicated GLSL error channel → <dll-dir>/logs/glsl_errors.txt. Keeps shader
// compiler/linker messages out of the per-frame DiagLog (which also captures
// shadow/tune noise) so a shader break is greppable in one file. Errors only.
static void GlslErrLog(const char *fmt, ...)
{
    char dir[MAX_PATH];
    GetDllDirPath(dir, sizeof(dir));
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%slogs%cglsl_errors.txt", dir, '\\');
    FILE *f = fopen(path, "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

#define RESOLVE(var, name) do { \
    (var) = (__typeof__(var))(orig_wglGetProcAddress ? orig_wglGetProcAddress(name) : 0); \
    if (!(var)) { DiagLog("[glsl] MISSING entrypoint: %s", name); allOk = false; } \
} while (0)

static const char *kTestVS =
    "void main(){ gl_Position = gl_Vertex; }\n";
static const char *kTestFS =
    "void main(){ gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";

static GLuint CompileStage(GLenum type, const char *src, const char *tag)
{
    GLuint sh = pglCreateShader(type);
    if (!sh) { DiagLog("[glsl] glCreateShader returned 0 (%s)", tag); return 0; }
    const GLchar *arr[1] = { (const GLchar *)src };
    pglShaderSource(sh, 1, arr, 0);
    pglCompileShader(sh);
    GLint ok = 0; pglGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]; GLsizei n = 0;
        pglGetShaderInfoLog(sh, (GLsizei)sizeof(log) - 1, &n, log);
        log[(n >= 0 && n < (GLsizei)sizeof(log)) ? n : (int)sizeof(log) - 1] = 0;
        DiagLog("[glsl] compile FAIL (%s): %s", tag, log);
        GlslErrLog("[glsl] compile FAIL (%s): %s", tag, log);
        pglDeleteShader(sh);
        return 0;
    }
    return sh;
}

// Compile + link a VS+FS into a program. Returns 0 (and logs) on any failure.
// The shaders are flagged for delete after attach (freed with the program).
static GLuint GlslLinkProgram(const char *vsSrc, const char *fsSrc, const char *tag)
{
    GLuint vs = CompileStage(GL_VERTEX_SHADER,   vsSrc, tag);
    GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fsSrc, tag);
    if (!vs || !fs) { if (vs) pglDeleteShader(vs); if (fs) pglDeleteShader(fs); return 0; }
    GLuint prog = pglCreateProgram();
    if (!prog) { pglDeleteShader(vs); pglDeleteShader(fs); return 0; }
    pglAttachShader(prog, vs);
    pglAttachShader(prog, fs);
    // Bind generic vertex attribs to named attributes BEFORE link.
    // KOTOR2 uses glVertexAttribPointerARB with generic slots 1 (weights) and 4 (bone indices).
    // GLSL 1.20 has NO built-in for generic attribs — must bind explicitly or the shader
    // reads garbage → "hedgehog" (vertices fly to random bone positions).
    if (pglBindAttribLocation) {
        pglBindAttribLocation(prog, 1, "aWeight");
        pglBindAttribLocation(prog, 4, "aBoneIdx");
    }
    pglLinkProgram(prog);
    pglDeleteShader(vs);
    pglDeleteShader(fs);
    GLint linked = 0; pglGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024]; GLsizei n = 0;
        pglGetProgramInfoLog(prog, (GLsizei)sizeof(log) - 1, &n, log);
        log[(n >= 0 && n < (GLsizei)sizeof(log)) ? n : (int)sizeof(log) - 1] = 0;
        DiagLog("[glsl] link FAIL (%s): %s", tag, log);
        GlslErrLog("[glsl] link FAIL (%s): %s", tag, log);
        pglDeleteProgram(prog);
        return 0;
    }
    return prog;
}

void GlslProbe_Init()
{
    static bool done = false;
    if (done) return;
    done = true;

    const char *ver = (const char *)glGetString(GL_VERSION);
    const char *sl  = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    const char *ren = (const char *)glGetString(GL_RENDERER);
    const char *ven = (const char *)glGetString(GL_VENDOR);
    DiagLog("[glsl] GL_VERSION=%s", ver ? ver : "(null)");
    DiagLog("[glsl] GL_SHADING_LANGUAGE_VERSION=%s", sl ? sl : "(null)");
    DiagLog("[glsl] GL_RENDERER=%s  GL_VENDOR=%s", ren ? ren : "?", ven ? ven : "?");

    GLint v;
    v = 0; glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &v);          DiagLog("[glsl] MAX_TEXTURE_IMAGE_UNITS=%d", v);
    v = 0; glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &v); DiagLog("[glsl] MAX_COMBINED_TEXTURE_IMAGE_UNITS=%d", v);
    v = 0; glGetIntegerv(GL_MAX_DRAW_BUFFERS, &v);                 DiagLog("[glsl] MAX_DRAW_BUFFERS=%d", v);

    bool allOk = true;
    RESOLVE(pglCreateShader,       "glCreateShader");
    RESOLVE(pglShaderSource,       "glShaderSource");
    RESOLVE(pglCompileShader,      "glCompileShader");
    RESOLVE(pglGetShaderiv,        "glGetShaderiv");
    RESOLVE(pglGetShaderInfoLog,   "glGetShaderInfoLog");
    RESOLVE(pglCreateProgram,      "glCreateProgram");
    RESOLVE(pglAttachShader,       "glAttachShader");
    RESOLVE(pglLinkProgram,        "glLinkProgram");
    RESOLVE(pglGetProgramiv,       "glGetProgramiv");
    RESOLVE(pglGetProgramInfoLog,  "glGetProgramInfoLog");
    RESOLVE(pglUseProgram,         "glUseProgram");
    RESOLVE(pglGetUniformLocation, "glGetUniformLocation");
    RESOLVE(pglUniform1i,          "glUniform1i");
    RESOLVE(pglUniform1f,          "glUniform1f");
    RESOLVE(pglUniform3fv,         "glUniform3fv");
    RESOLVE(pglUniform4fv,         "glUniform4fv");
    RESOLVE(pglUniformMatrix4fv,   "glUniformMatrix4fv");
    RESOLVE(pglDeleteShader,       "glDeleteShader");
    RESOLVE(pglDeleteProgram,      "glDeleteProgram");
    pglBindAttribLocation = (PFN_glBindAttribLocation)(orig_wglGetProcAddress ? orig_wglGetProcAddress("glBindAttribLocation") : 0);
    // Non-fatal if missing: only skinned programs need it.

    if (!allOk) {
        DiagLog("[glsl] NOT AVAILABLE: GLSL entry points missing from this wrapper.");
        return;
    }

    GLuint prog = GlslLinkProgram(kTestVS, kTestFS, "probe");
    if (!prog) {
        DiagLog("[glsl] NOT AVAILABLE: trivial program failed to compile/link.");
        return;
    }
    pglDeleteProgram(prog);   // throwaway; the entry points stay resolved

    // Env-param getter for material uniform capture. Non-fatal to the probe verdict
    // (only the material path needs it); without it GlslMaterial_Apply no-ops.
    pglGetEnvFv = (PFN_glGetEnvFv)(orig_wglGetProcAddress
        ? orig_wglGetProcAddress("glGetProgramEnvParameterfvARB") : 0);
    if (!pglGetEnvFv)
        DiagLog("[glsl] WARN: glGetProgramEnvParameterfvARB missing — GLSL material disabled.");

    pglActiveTexture = (PFN_glActiveTexture)(orig_wglGetProcAddress ? orig_wglGetProcAddress("glActiveTexture") : 0);
    if (!pglActiveTexture)
        pglActiveTexture = (PFN_glActiveTexture)(orig_wglGetProcAddress ? orig_wglGetProcAddress("glActiveTextureARB") : 0);

    s_available = true;
    DiagLog("[glsl] AVAILABLE: entry points resolved + trivial program linked OK.");
}

// ============================================================================
// Faithful GLSL material replacement.
// Replaces the engine's (vp_static_lit_fog + fp_worldtex_diffuse_main) ARB pair
// — the primary diffuse world surface / catch-all — with a GLSL program loaded
// from shaders_override/fp_worldtex_diffuse_main.{vs,fs}.glsl that ports both ARB
// stages instruction-for-instruction (3-light LIT lighting, PBR, sun + shadow PCF).
// Every uniform is captured from the SAME program.env slot the ARB read, via
// glGetProgramEnvParameterfvARB, so the GLSL inputs are identical to the ARB path.
//
// Also supports the (vp_skinned_env_lit + fp_model_env_reflective) skinned
// character/object pair (fp_model_env_reflective.{vs,fs}.glsl) — same toggle.
//
// Gated by a runtime toggle (FRAGMENT env[26].w >= 0.5, the "GLSL material" tune
// slider) so it is OFF by default and can be A/B'd against the ARB path in-game.
// ============================================================================
static GLuint s_progDiffuseMain   = 0;
static bool   s_triedDiffuseMain  = false;
static GLuint s_progEnvRefl       = 0;
static bool   s_triedEnvRefl      = false;
static GLuint s_progBumpEnvSpec   = 0;
static bool   s_triedBumpEnvSpec  = false;
// Simple model shaders (use vp_skinned_lit_fog.vs.glsl)
static GLuint s_progDiffSimple    = 0;
static bool   s_triedDiffSimple   = false;
static GLuint s_progDiffNolm      = 0;
static bool   s_triedDiffNolm     = false;
static GLuint s_progHeadgear      = 0;
static bool   s_triedHeadgear     = false;
// Door (uses vp_static_env_fog.vs.glsl)
static GLuint s_progDoor          = 0;
static bool   s_triedDoor         = false;
// Armor legacy (uses vp_skinned_env_lit.vs.glsl)
static GLuint s_progArmorLegacy   = 0;
static bool   s_triedArmorLegacy  = false;
// World env shaders (use vp_static_env_fog.vs.glsl)
static GLuint s_progWorldEnvRefl  = 0;
static bool   s_triedWorldEnvRefl = false;
static GLuint s_progWorldEnvReflT2  = 0;   // linked with vp_worldtex_env_fog_t2.vs.glsl (lightmap = gl_MultiTexCoord1)
static bool   s_triedWorldEnvReflT2 = false;
static GLuint s_progWorldLmEnv    = 0;
static bool   s_triedWorldLmEnv   = false;
static GLuint s_progWorldLmEnvT2  = 0;
static bool   s_triedWorldLmEnvT2 = false;
// World bump env (uses vp_static_env_fog.vs.glsl — no tex0/tex1, pure reflection)
static GLuint s_progWorldBumpEnv       = 0;
static bool   s_triedWorldBumpEnv      = false;
static GLuint s_progWorldBumpEnvGamma  = 0;
static bool   s_triedWorldBumpEnvGamma = false;
static long   s_matApplied        = 0;
static GLuint  s_stableProg        = 0;   // program id whose per-frame (stable) uniforms are current

// Uniform locations, resolved ONCE at link. Avoids per-draw glGetUniformLocation +
// std::map<string> lookups (a measurable per-draw CPU cost with ~40 uniforms).
// NOTE: only s_u is still used — diffuse_main's upload path resolves via pre-cached
// locs. env/world/bump groups use SetEnv4ForActive(name) instead (rescues per-program),
// so they cache no struct.
static struct {
    GLint vi0, vi1, vi2, vi3;
    GLint l0pos, l0amb, l0dif, l0att;
    GLint l1pos, l1amb, l1dif, l1att;
    GLint l2pos, l2amb, l2dif, l2att;
    GLint amb, difScale, ambScale;
    GLint pbr, fl, ns, ux, tn, tnB, tnC, tnD, tnE, tnF, tnG, pcf;
    GLint fl0pos, fl0dif, camW, k0, k1, k2;
    GLint fogColor, fogParams;
    GLint camLight;
} s_u;

// Fog: both the GLSL gl_Fog built-in AND glGetFloatv(GL_FOG_START/END) are unreliable
// in this Aspyr wrapper (wrong span → the skybox fogged solid). We instead use the mod's
// own fogBehavior[] (computed in fogRecalculate from the engine's intercepted glFog calls)
// which matches what the ARB path effectively uses. Color + gate via glGetFloatv(GL_FOG_COLOR)
// PER-DRAW in UploadPerDraw: the engine zeroes fog color on the skybox/backdrop draw to kill
// fog there, exactly the signal the ARB VP reads per-draw (`SLT 0, state.fog.color`). A
// per-FRAME cache here sampled an arbitrary fog-ON draw, so it fogged the sky solid.

// Load a text file relative to CWD (game dir) — same path convention as the ARB overrides.
static bool LoadTextFile(const char *rel, std::string &out)
{
    HANDLE h = CreateFileA(rel, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    DWORD sz = GetFileSize(h, 0), rd = 0;
    if (sz != INVALID_FILE_SIZE && sz > 0) {
        char *buf = (char *)HeapAlloc(GetProcessHeap(), 0, sz);
        if (buf) {
            if (ReadFile(h, buf, sz, &rd, 0) && rd == sz) { out.assign(buf, sz); ok = true; }
            HeapFree(GetProcessHeap(), 0, buf);
        }
    }
    CloseHandle(h);
    return ok;
}

// Read program.env[idx] from `target` and push it to a (pre-resolved) uniform location.
static void SetEnv4(GLenum target, GLuint idx, GLint loc)
{
    if (loc < 0) return;
    GLfloat e[4] = { 0, 0, 0, 0 };
    pglGetEnvFv(target, idx, e);
    pglUniform4fv(loc, 1, e);
}

// Push env[N] to a GLSL uniform BY NAME. Resolves the uniform location from the
// CURRENTLY ACTIVE GLSL program. Eliminates the architectural bug where pre-cached
// loc structs (resolved for ONE program) were reused for others with different
// layouts — writing shadow-K into pbr, camLight into nothing, etc.
static void SetEnv4ForActive(GLenum target, GLuint envSlot, const char *name)
{
    GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog == 0) return;
    GLint loc = pglGetUniformLocation((GLuint)prog, name);
    if (loc < 0) return;
    GLfloat e[4] = { 0, 0, 0, 0 };
    pglGetEnvFv(target, envSlot, e);
    pglUniform4fv(loc, 1, e);
}

// --- Generic GLSL program linker. Reads VS+FS, links, binds sampler units (each
// skipped cleanly if the FS doesn't declare it), caches the program id in *outProg.
// Removes ~200 lines of identical loader boilerplate across the 14 programs. ---
struct GSampler { const char *name; int tmu; };
static bool LinkProgram(const char *logName, const char *vsFile, const char *fsFile,
                        GLuint *outProg, const GSampler *smp, int nSmp)
{
    std::string vs, fs;
    if (!LoadTextFile(vsFile, vs) || !LoadTextFile(fsFile, fs)) {
        DiagLog("[glsl] %s source missing (%s + %s)", logName, vsFile, fsFile);
        return false;
    }
    GLuint p = GlslLinkProgram(vs.c_str(), fs.c_str(), logName);
    if (!p) return false;
    *outProg = p;
    pglUseProgram(p);
    for (int i = 0; i < nSmp; i++) {
        GLint l = pglGetUniformLocation(p, smp[i].name);
        if (l >= 0) pglUniform1i(l, smp[i].tmu);
    }
    pglUseProgram(0);
    DiagLog("[glsl] material %s linked id=%u", logName, p);
    return true;
}

// Per-program sampler unit tables (TMUs the engine/mod already bound for the ARB path).
// Grouped: world_tex0 (diffuse+lightmap+env+pbr), world_bump (no diffuse — pure refl),
// skinned_env (SELF shadow=5), skinned_lit (no env), headgear (diffuse+lm), door.
static const GSampler kWld0[] = { {"tex0",0},{"tex1",1},{"texEnv",2},{"texNrm",8},{"texRgh",9},{"texMtl",10},{"texShadow",6} };
static const GSampler kWldBump[] = { {"texEnv",2},{"texNrm",8},{"texRgh",9},{"texMtl",10},{"texShadow",6} };
static const GSampler kSkEnv[] = { {"tex0",0},{"texEnv",1},{"texNrm",8},{"texRgh",9},{"texMtl",10},{"texShadow",5} };
static const GSampler kSkLt[] = { {"tex0",0},{"texShadow",5} };
static const GSampler kHeadgear[] = { {"tex0",0},{"tex1",1},{"texShadow",5} };
static const GSampler kDoor[] = { {"tex0",0},{"texEnv",2},{"texShadow",6} };

static GLint UL(const char *n) { return pglGetUniformLocation(s_progDiffuseMain, n); }

static bool LoadDiffuseMain()
{
    if (!LinkProgram("fp_worldtex_diffuse_main",
                     "shaders_override/fp_worldtex_diffuse_main.vs.glsl",
                     "shaders_override/fp_worldtex_diffuse_main.fs.glsl",
                     &s_progDiffuseMain, kWld0, (int)(sizeof(kWld0)/sizeof(kWld0[0]))))
        return false;
    // Cache value-uniform locations (diffuse_main's upload path uses pre-cached s_u).
    s_u.vi0 = UL("uVI0"); s_u.vi1 = UL("uVI1"); s_u.vi2 = UL("uVI2"); s_u.vi3 = UL("uVI3");
    s_u.l0pos = UL("uL0pos"); s_u.l0amb = UL("uL0amb"); s_u.l0dif = UL("uL0dif"); s_u.l0att = UL("uL0att");
    s_u.l1pos = UL("uL1pos"); s_u.l1amb = UL("uL1amb"); s_u.l1dif = UL("uL1dif"); s_u.l1att = UL("uL1att");
    s_u.l2pos = UL("uL2pos"); s_u.l2amb = UL("uL2amb"); s_u.l2dif = UL("uL2dif"); s_u.l2att = UL("uL2att");
    s_u.amb = UL("uAmb"); s_u.difScale = UL("uDifScale"); s_u.ambScale = UL("uAmbScale");
    s_u.pbr = UL("pbr"); s_u.fl = UL("fl"); s_u.ns = UL("ns"); s_u.ux = UL("ux");
    s_u.tn = UL("tn"); s_u.tnB = UL("tnB"); s_u.tnC = UL("tnC"); s_u.tnD = UL("tnD");
    s_u.tnE = UL("tnE"); s_u.tnF = UL("tnF"); s_u.tnG = UL("tnG"); s_u.pcf = UL("uPcf");
    s_u.fl0pos = UL("uFL0pos"); s_u.fl0dif = UL("uFL0dif"); s_u.camW = UL("uCamW");
    s_u.k0 = UL("uK0"); s_u.k1 = UL("uK1"); s_u.k2 = UL("uK2");
    s_u.fogColor = UL("uFogColor"); s_u.fogParams = UL("uFogParams");
    s_u.camLight = UL("uCamLight");
    return true;
}

// --- Env-reflective (skinned character) program ---------------------------------
static bool LoadEnvRefl()
{
    return LinkProgram("fp_model_env_reflective",
                       "shaders_override/vp_skinned_env_lit.vs.glsl",
                       "shaders_override/fp_model_env_reflective.fs.glsl",
                       &s_progEnvRefl, kSkEnv, (int)(sizeof(kSkEnv)/sizeof(kSkEnv[0])));
}

// Per-FRAME uniforms for the env_reflective group (env_reflective, bump_env_spec, armor_legacy).
// Uses SetEnv4ForActive — resolves uniform locations from the CURRENTLY ACTIVE program,
// so viewInv lights/K/camLight go to the correct slots regardless of which program is active.
static void UploadStableEnvRefl()
{
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB,  92, "uVI0");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB,  91, "uVI1");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB,  90, "uVI2");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB,  89, "uVI3");
    // Tune sliders + sun + shadow tune (only pushed if the FS declares them)
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 24, "tn");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 25, "tnB");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 26, "tnC");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 27, "tnD");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 28, "tnE");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 29, "tnF");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 30, "tnG");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 32, "uPcf");
    // Self-shadow K: env[104..106]
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 104, "uKS0");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 105, "uKS1");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 106, "uKS2");
    // Camera light
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 34, "uCamLight");
    // camW from FRAGMENT env[92..90].w
    {
        GLfloat cw[4] = {0,0,0,0};
        GLfloat e92[4]={0}, e91[4]={0}, e90[4]={0};
        pglGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 92, e92);
        pglGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 91, e91);
        pglGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 90, e90);
        cw[0]=e92[3]; cw[1]=e91[3]; cw[2]=e90[3];
        GLint prog=0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        if (prog != 0) {
            GLint loc = pglGetUniformLocation((GLuint)prog, "uCamW");
            if (loc >= 0) pglUniform4fv(loc, 1, cw);
        }
    }
}

// Per-DRAW uniforms for the env_reflective group (env_reflective, bump_env_spec, armor_legacy).
// Uses SetEnv4ForActive — uniform locations resolved from the active program per draw.
static void UploadPerDrawEnvRefl()
{
    // Global output alpha (ARB: PARAM c[0] = program.env[0]; MOV d.a, c[0].a)
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 0, "uEnv0");
    // --- Bone palette: env[18..68] = 51 vec4s → uBone[51] ---
    {
        GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        GLint boneLoc = (prog != 0) ? pglGetUniformLocation((GLuint)prog, "uBone[0]") : -1;
        if (boneLoc >= 0) {
            GLfloat boneData[51 * 4];
            for (int i = 0; i < 51; i++) {
                GLfloat e[4] = {0,0,0,0};
                pglGetEnvFv(GL_VERTEX_PROGRAM_ARB, (GLuint)(18 + i), e);
                boneData[i*4+0]=e[0]; boneData[i*4+1]=e[1];
                boneData[i*4+2]=e[2]; boneData[i*4+3]=e[3];
            }
            pglUniform4fv(boneLoc, 51, boneData);
        }
        // Bone config: env[16] VERTEX
        if (prog != 0) {
            GLint bcfg = pglGetUniformLocation((GLuint)prog, "uBoneCfg");
            if (bcfg >= 0) {
                GLfloat e[4] = {0,0,0,0};
                pglGetEnvFv(GL_VERTEX_PROGRAM_ARB, 16, e);
                pglUniform4fv(bcfg, 1, e);
            }
        }
    }

    // --- 3 vertex lights (VERTEX env) ---
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 87, "uL0pos");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 86, "uL0amb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 85, "uL0dif");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 83, "uL0att");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 82, "uL1pos");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 81, "uL1amb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 80, "uL1dif");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 78, "uL1att");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 77, "uL2pos");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 76, "uL2amb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 75, "uL2dif");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 73, "uL2att");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 93, "uAmb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 94, "uDifScale");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 95, "uAmbScale");

    // --- PBR params (FRAGMENT env) ---
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 20, "pbr");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 21, "fl");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 22, "ns");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 23, "ux");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 87, "uFL0pos");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 86, "uFL0dif");

    // --- Fog for VS ---
    {
        GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        if (prog != 0) {
            GLint fpLoc = pglGetUniformLocation((GLuint)prog, "uFogParams");
            if (fpLoc >= 0) {
                GLfloat fs = 0.0f, fe = 1.0f;
                glGetFloatv(GL_FOG_START, &fs); glGetFloatv(GL_FOG_END, &fe);
                GLfloat span = fe - fs;
                GLfloat invSpan = (span != 0.0f) ? (1.0f / span) : 0.0f;
                GLfloat fp[4] = { 0.0f, fs, 0.0f, invSpan };
                pglUniform4fv(fpLoc, 1, fp);
            }
            GLint fcLoc = pglGetUniformLocation((GLuint)prog, "uFogColor");
            if (fcLoc >= 0) {
                GLfloat fc[4] = {0,0,0,1};
                glGetFloatv(GL_FOG_COLOR, fc);
                pglUniform4fv(fcLoc, 1, fc);
            }
        }
    }
}

// --- Bump env spec (skinned characters with normalmap) — SELF shadow TMU5 ---
static bool LoadBumpEnvSpec()
{
    return LinkProgram("fp_model_bump_env_spec",
                       "shaders_override/vp_skinned_env_lit.vs.glsl",
                       "shaders_override/fp_model_bump_env_spec.fs.glsl",
                       &s_progBumpEnvSpec, kSkEnv, (int)(sizeof(kSkEnv)/sizeof(kSkEnv[0])));
}

// --- Simple model shaders (vp_skinned_lit_fog + diff_simple/diff_nolm/headgear/armor_legacy) ---
static bool LoadDiffSimple()
{
    return LinkProgram("fp_model_diff_simple",
                       "shaders_override/vp_skinned_lit_fog.vs.glsl",
                       "shaders_override/fp_model_diff_simple.fs.glsl",
                       &s_progDiffSimple, kSkLt, (int)(sizeof(kSkLt)/sizeof(kSkLt[0])));
}

static bool LoadDiffNolm()
{
    return LinkProgram("fp_model_diff_nolm",
                       "shaders_override/vp_skinned_lit_fog.vs.glsl",
                       "shaders_override/fp_model_diff_nolm.fs.glsl",
                       &s_progDiffNolm, kSkLt, (int)(sizeof(kSkLt)/sizeof(kSkLt[0])));
}

static bool LoadHeadgear()
{
    return LinkProgram("fp_model_headgear_legacy",
                       "shaders_override/vp_skinned_lit_fog.vs.glsl",
                       "shaders_override/fp_model_headgear_legacy.fs.glsl",
                       &s_progHeadgear, kHeadgear, (int)(sizeof(kHeadgear)/sizeof(kHeadgear[0])));
}

// Door (uses vp_static_env_fog + complete shadow TMU6)
static bool LoadDoor()
{
    return LinkProgram("fp_door",
                       "shaders_override/vp_static_env_fog.vs.glsl",
                       "shaders_override/fp_door.fs.glsl",
                       &s_progDoor, kDoor, (int)(sizeof(kDoor)/sizeof(kDoor[0])));
}

static bool LoadArmorLegacy()
{
    return LinkProgram("fp_model_armor_legacy",
                       "shaders_override/vp_skinned_env_lit.vs.glsl",
                       "shaders_override/fp_model_armor_legacy.fs.glsl",
                       &s_progArmorLegacy, kSkEnv, (int)(sizeof(kSkEnv)/sizeof(kSkEnv[0])));
}

// Shared upload for skinned_lit_fog-based programs (diff_simple, diff_nolm, headgear)
static void UploadStableSkinnedLit()
{
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 92, "uVI0");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 91, "uVI1");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 90, "uVI2");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 89, "uVI3");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 27, "tnD");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 28, "tnE");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 29, "tnF");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 30, "tnG");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 32, "uPcf");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 104, "uKS0");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 105, "uKS1");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 106, "uKS2");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 34, "uCamLight");
}

static void UploadPerDrawSkinnedLit()
{
    // Global output alpha (ARB: PARAM c[0] = program.env[0]; headgear: MOV r0.a, c[0])
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 0, "uEnv0");
    // Resolve on the CURRENTLY ACTIVE program (not a hardcoded fallback).
    // GlslMaterial_Apply calls glUseProgram(matchedProg) before this, so the
    // active program is the one that needs the bone palette. Resolving on a
    // different fallback program writes uBone to a location valid for THAT
    // program → the active program's uBone stays 0 → character collapses.
    // Same cross-program bug class fixed elsewhere via SetEnv4ForActive.
    GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    GLint boneLoc = (prog != 0) ? pglGetUniformLocation((GLuint)prog, "uBone[0]") : -1;
    if (boneLoc >= 0) {
        GLfloat boneData[51 * 4];
        for (int i = 0; i < 51; i++) {
            GLfloat e[4] = { 0, 0, 0, 0 };
            pglGetEnvFv(GL_VERTEX_PROGRAM_ARB, (GLuint)(18 + i), e);
            boneData[i * 4 + 0] = e[0]; boneData[i * 4 + 1] = e[1]; boneData[i * 4 + 2] = e[2]; boneData[i * 4 + 3] = e[3];
        }
        pglUniform4fv(boneLoc, 51, boneData);
    }
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 16, "uBoneCfg");   // resolve on active program (bone index scale + w=1)
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 87, "uL0pos");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 86, "uL0amb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 85, "uL0dif");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 83, "uL0att");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 82, "uL1pos");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 81, "uL1amb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 80, "uL1dif");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 78, "uL1att");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 77, "uL2pos");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 76, "uL2amb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 75, "uL2dif");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 73, "uL2att");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 93, "uAmb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 94, "uDifScale");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 95, "uAmbScale");
}

// --- World env shaders (vp_static_env_fog / vp_worldtex_env_fog_t2 + fp) ---
// Same FS can link with either VP — the t2 VS maps lightmap UV to gl_MultiTexCoord1,
// the static VS to gl_MultiTexCoord2. Callers select per s_vpT2 (see Apply).
static bool LoadWorldEnvRefl()
{
    return LinkProgram("fp_worldtex_env_reflective",
                       "shaders_override/vp_static_env_fog.vs.glsl",
                       "shaders_override/fp_worldtex_env_reflective.fs.glsl",
                       &s_progWorldEnvRefl, kWld0, (int)(sizeof(kWld0)/sizeof(kWld0[0])));
}

static bool LoadWorldLmEnv()
{
    return LinkProgram("fp_worldtex_lm_env",
                       "shaders_override/vp_static_env_fog.vs.glsl",
                       "shaders_override/fp_worldtex_lm_env.fs.glsl",
                       &s_progWorldLmEnv, kWld0, (int)(sizeof(kWld0)/sizeof(kWld0[0])));
}

// T2 variants: same FS linked with vp_worldtex_env_fog_t2.vs.glsl (lightmap = gl_MultiTexCoord1)
static bool LoadWorldEnvReflT2()
{
    return LinkProgram("fp_worldtex_env_reflective_t2",
                       "shaders_override/vp_worldtex_env_fog_t2.vs.glsl",
                       "shaders_override/fp_worldtex_env_reflective.fs.glsl",
                       &s_progWorldEnvReflT2, kWld0, (int)(sizeof(kWld0)/sizeof(kWld0[0])));
}

static bool LoadWorldLmEnvT2()
{
    return LinkProgram("fp_worldtex_lm_env_t2",
                       "shaders_override/vp_worldtex_env_fog_t2.vs.glsl",
                       "shaders_override/fp_worldtex_lm_env.fs.glsl",
                       &s_progWorldLmEnvT2, kWld0, (int)(sizeof(kWld0)/sizeof(kWld0[0])));
}

// --- World bump env shaders (vp_static_env_fog + fp_worldtex_bump_env / _gamma) ---
// Pure-reflective bumpmapped walls, no diffuse/lightmap textures (kWldBump).
static bool LoadWorldBumpEnv()
{
    return LinkProgram("fp_worldtex_bump_env",
                       "shaders_override/vp_static_env_fog.vs.glsl",
                       "shaders_override/fp_worldtex_bump_env.fs.glsl",
                       &s_progWorldBumpEnv, kWldBump, (int)(sizeof(kWldBump)/sizeof(kWldBump[0])));
}

static bool LoadWorldBumpEnvGamma()
{
    return LinkProgram("fp_worldtex_bump_env_gamma",
                       "shaders_override/vp_static_env_fog.vs.glsl",
                       "shaders_override/fp_worldtex_bump_env_gamma.fs.glsl",
                       &s_progWorldBumpEnvGamma, kWldBump, (int)(sizeof(kWldBump)/sizeof(kWldBump[0])));
}

// --- Post-pass programs (FP-only; use vp_post_fullscreen.vs.glsl) ---
// NOTE (2026-08): post GLSL port REVERTED. Logs show fp_post_composite_top is bound
// for 3D/UI draws too (49 draws/frame), not just the fullscreen quad — an FP-only
// match hijacked UI (white alpha) + 3D (sun spot through walls). Retry requires
// reliably distinguishing the true fullscreen post quad (see TODO).

static void UploadStableWorldEnv()
{
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB,  92, "uVI0");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB,  91, "uVI1");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB,  90, "uVI2");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB,  89, "uVI3");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 24, "tn");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 25, "tnB");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 26, "tnC");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 27, "tnD");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 28, "tnE");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 29, "tnF");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 30, "tnG");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 32, "uPcf");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 100, "uK0");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 101, "uK1");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 102, "uK2");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 34, "uCamLight");
    GLfloat cw[4] = { 0, 0, 0, 0 };
    {
        GLfloat e92[4] = {0}, e91[4] = {0}, e90[4] = {0};
        pglGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 92, e92);
        pglGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 91, e91);
        pglGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 90, e90);
        cw[0] = e92[3]; cw[1] = e91[3]; cw[2] = e90[3];
        GLint prog=0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        if (prog != 0) {
            GLint loc = pglGetUniformLocation((GLuint)prog, "uCamW");
            if (loc >= 0) pglUniform4fv(loc, 1, cw);
        }
    }
}

static void UploadPerDrawWorldEnv()
{
    // Global output alpha (ARB: PARAM c[0] = program.env[0]; MOV d.a, c[0].a)
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 0, "uEnv0");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 87, "uL0pos");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 86, "uL0amb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 85, "uL0dif");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 83, "uL0att");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 82, "uL1pos");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 81, "uL1amb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 80, "uL1dif");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 78, "uL1att");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 77, "uL2pos");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 76, "uL2amb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 75, "uL2dif");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 73, "uL2att");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 93, "uAmb");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 94, "uDifScale");
    SetEnv4ForActive(GL_VERTEX_PROGRAM_ARB, 95, "uAmbScale");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 20, "pbr");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 21, "fl");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 22, "ns");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 23, "ux");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 87, "uFL0pos");
    SetEnv4ForActive(GL_FRAGMENT_PROGRAM_ARB, 86, "uFL0dif");
    {
        GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        if (prog != 0) {
            GLint fpLoc = pglGetUniformLocation((GLuint)prog, "uFogParams");
            if (fpLoc >= 0) {
                GLfloat fs = 0.0f, fe = 1.0f;
                glGetFloatv(GL_FOG_START, &fs); glGetFloatv(GL_FOG_END, &fe);
                GLfloat span = fe - fs;
                GLfloat invSpan = (span != 0.0f) ? (1.0f / span) : 0.0f;
                GLfloat fp[4] = { 0.0f, fs, 0.0f, invSpan };
                pglUniform4fv(fpLoc, 1, fp);
            }
            GLint fcLoc = pglGetUniformLocation((GLuint)prog, "uFogColor");
            if (fcLoc >= 0) {
                GLfloat fc[4] = {0,0,0,1};
                glGetFloatv(GL_FOG_COLOR, fc);
                pglUniform4fv(fcLoc, 1, fc);
            }
        }
    }
}

// Per-FRAME uniforms: camera viewInv + camW, the global tune sliders, pcf step, and
// this frame's shadow K. All constant across the frame (the engine sets viewInv/camW
// from the camera; the sliders + shadow K are set once per frame), so they are uploaded
// once — the program retains uniform state across the per-draw use/unuse cycle.
static void UploadStable()
{
    SetEnv4(GL_VERTEX_PROGRAM_ARB,  92, s_u.vi0);
    SetEnv4(GL_VERTEX_PROGRAM_ARB,  91, s_u.vi1);
    SetEnv4(GL_VERTEX_PROGRAM_ARB,  90, s_u.vi2);
    SetEnv4(GL_VERTEX_PROGRAM_ARB,  89, s_u.vi3);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 24, s_u.tn);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 25, s_u.tnB);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 26, s_u.tnC);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 27, s_u.tnD);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 28, s_u.tnE);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 29, s_u.tnF);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 30, s_u.tnG);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 32, s_u.pcf);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 100, s_u.k0);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 101, s_u.k1);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 102, s_u.k2);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 34, s_u.camLight);
    GLfloat cw[4] = { 0, 0, 0, 0 };
    {
        GLfloat e92[4] = {0}, e91[4] = {0}, e90[4] = {0};
        pglGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 92, e92);
        pglGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 91, e91);
        pglGetEnvFv(GL_FRAGMENT_PROGRAM_ARB, 90, e90);
        cw[0] = e92[3]; cw[1] = e91[3]; cw[2] = e90[3];
        if (s_u.camW >= 0) pglUniform4fv(s_u.camW, 1, cw);
    }

    // Fog color + gate are pushed PER-DRAW in UploadPerDraw (the gate must track the
    // engine's per-draw fog-color toggle — see the note at s_progDiffuseMain). Only the
    // one-shot diag read stays here.
    GLfloat fc[4] = { 0, 0, 0, 1 };
    glGetFloatv(GL_FOG_COLOR, fc);
    // One-shot ground truth: log the wrapper's glGetFloatv fog read vs the mod's
    // intercepted values, so the right source is confirmed instead of guessed.
    {
        static int n = 0;
        if (n < 3) {
            n++;
            GLfloat gs = -1.0f, ge = -1.0f;
            glGetFloatv(GL_FOG_START, &gs);
            glGetFloatv(GL_FOG_END,   &ge);
            DiagLog("[glsl][fog] glGet start=%.2f end=%.2f color=(%.3f,%.3f,%.3f) | mod start=%.2f end=%.2f mode=%u bFogOn=%d fb2=%.5f fb3=%.5f | camW=(%.1f,%.1f,%.1f)",
                    gs, ge, fc[0], fc[1], fc[2],
                    (double)fogStart, (double)fogEnd, (unsigned)fogMode, (int)bFogOn,
                    fogBehavior[2], fogBehavior[3], cw[0], cw[1], cw[2]);
        }
    }
}

// Per-DRAW uniforms: the engine's 3 per-object vertex lights + ambient/scales, the
// per-material PBR params, and the fragment-stage light0. NOT cached — the engine
// varies them per object/material, so caching would change shading (quality loss).
static void UploadPerDraw()
{
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 87, s_u.l0pos);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 86, s_u.l0amb);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 85, s_u.l0dif);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 83, s_u.l0att);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 82, s_u.l1pos);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 81, s_u.l1amb);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 80, s_u.l1dif);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 78, s_u.l1att);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 77, s_u.l2pos);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 76, s_u.l2amb);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 75, s_u.l2dif);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 73, s_u.l2att);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 93, s_u.amb);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 94, s_u.difScale);
    SetEnv4(GL_VERTEX_PROGRAM_ARB, 95, s_u.ambScale);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 20, s_u.pbr);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 21, s_u.fl);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 22, s_u.ns);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 23, s_u.ux);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 87, s_u.fl0pos);
    SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 86, s_u.fl0dif);

    // Fog (PER-DRAW, faithful to the ARB VP's `PARAM fogP = state.fog.params`): the ARB
    // reads fog start/end LIVE from GL state on EVERY draw. The engine widens/relaxes the
    // fog range for the skybox/backdrop so it is NOT fogged (clouds + sun disc show); using
    // the mod's *global* fogBehavior (the last glFog range, e.g. 25/200) instead fogged the
    // sky to the fog color. Read GL_FOG_START/END here per-draw and derive the same linear
    // coeffs fogRecalculate uses (fb2=-1/span, fb3=end/span); gate on fog.color.r>0 like the
    // VP's `SLT 0, fog.color`. FS: fogAmt = clamp(1-(|eye|*x + y), 0,1) * gate.
    GLfloat fc[4] = { 0, 0, 0, 1 };
    glGetFloatv(GL_FOG_COLOR, fc);
    if (s_u.fogColor >= 0) pglUniform4fv(s_u.fogColor, 1, fc);
    if (s_u.fogParams >= 0) {
        GLfloat fs = 0.0f, fe = 1.0f;
        glGetFloatv(GL_FOG_START, &fs);
        glGetFloatv(GL_FOG_END,   &fe);
        GLfloat span = fe - fs;
        GLfloat fb2  = (span != 0.0f) ? (-1.0f / span) : 0.0f;   // = -1/(end-start)
        GLfloat fb3  = (span != 0.0f) ? (fe / span)    : 1.0f;   // = end/(end-start)
        // Gate on fog.color.r>0 (ARB `SLT 0, fog.color`) AND GL_FOG enabled. The ARB source
        // applies fog unconditionally, yet the engine draws the SKYBOX with GL_FOG disabled and
        // the ARB path shows no fog there (clouds + sun visible) — so in hardware the disable
        // does suppress it. Matching that here (skip fog when GL_FOG is off) un-washes the sky
        // and correctly leaves fog-off interiors unfogged.
        GLboolean fogEnabled = glIsEnabled(GL_FOG);
        GLfloat gate = (fc[0] > 0.0f && fogEnabled) ? 1.0f : 0.0f;
        GLfloat fp[4] = { fb2, fb3, gate, 0.0f };
        pglUniform4fv(s_u.fogParams, 1, fp);
    }
}

// One-shot per-draw census (diag only): log each DISTINCT TMU0 texture that reaches the
// matched (vp_static_lit_fog + fp_worldtex_diffuse_main) pair, with its per-draw depth
// mask + fog color. Tells us whether the skybox/grass actually go through the GLSL
// material and in what state — ground truth instead of guesswork. Capped + deduped, so
// the per-draw cost is one set lookup once the scene's textures have all been seen.
static void GlslCensus(GLboolean dmask)
{
    static std::unordered_set<std::string> seen;
    if (seen.size() >= 80) return;
    GLint tex0 = 0;
    if (pglActiveTexture) {
        GLint act = GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &act);
        pglActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
        pglActiveTexture((GLenum)act);
    } else {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
    }
    const char *nm = PbrGetTextureName((GLuint)tex0);
    std::string key = (nm && nm[0]) ? nm : "?";
    if (!seen.insert(key).second) return;
    GLfloat fc[4] = { 0, 0, 0, 1 };
    glGetFloatv(GL_FOG_COLOR, fc);
    GLfloat fs = -1.0f, fe = -1.0f;
    glGetFloatv(GL_FOG_START, &fs);
    glGetFloatv(GL_FOG_END,   &fe);
    int fogEn = glIsEnabled(GL_FOG) ? 1 : 0;
    int fpEn  = glIsEnabled(GL_FRAGMENT_PROGRAM_ARB) ? 1 : 0;
    int vpEn  = glIsEnabled(GL_VERTEX_PROGRAM_ARB) ? 1 : 0;
    DiagLog("[glsl][census] tex=%-22s dMask=%d fog=(%.3f,%.3f,%.3f) start=%.1f end=%.1f GL_FOG=%d fpEn=%d vpEn=%d",
            key.c_str(), (int)dmask, fc[0], fc[1], fc[2], fs, fe, fogEn, fpEn, vpEn);
}

// Per-frame ("stable") uniforms are read from the ARB env slots and uploaded to the
// ACTIVE GLSL program. Previously ONE bool guarded ALL programs, so whichever program
// applied first in the frame got the camera/slider/shadow-K upload and every other
// program skipped it → characters kept STALE shadow-K (env[104..106]) from frames ago
// → self-shadows projected with a dead matrix → no char shadows at all. Now each program
// gets its stable upload the first time it is used since the last frame (GlslMaterial_OnFrame
// resets s_stableProg every frame, preserving the per-frame refresh).
static void UploadStableForProg(GLuint prog)
{
    if (prog == s_stableProg) return;              // same program already uploaded this frame
    s_stableProg = prog;
    if (prog == s_progDiffuseMain)                 UploadStable();
    else if (prog == s_progEnvRefl || prog == s_progBumpEnvSpec || prog == s_progArmorLegacy)
                                                 UploadStableEnvRefl();
    else if (prog == s_progDiffSimple || prog == s_progDiffNolm || prog == s_progHeadgear)
                                                 UploadStableSkinnedLit();
    else if (prog == s_progDoor || prog == s_progWorldEnvRefl || prog == s_progWorldLmEnv
         || prog == s_progWorldEnvReflT2 || prog == s_progWorldLmEnvT2
         || prog == s_progWorldBumpEnv || prog == s_progWorldBumpEnvGamma)
                                                 UploadStableWorldEnv();
}

// Perf meter: accumulates GLSL Apply cost (uniform upload + glUseProgram) per frame.
static LARGE_INTEGER s_perfFreq;    // QPC frequency (0 until first init)
static bool   s_perfFreqOk = false;
static LARGE_INTEGER s_appStart;
static double s_applyUs = 0.0;      // μs spent in GlslMaterial_Apply this frame
static int    s_applyDraw = 0;      // how many GLSL material draws this frame
static int    s_fCount = 0;          // frames since last report

static void PerfApplyStart()
{
    if (!s_perfFreqOk) { s_perfFreqOk = QueryPerformanceFrequency(&s_perfFreq) != 0; }
    if (s_perfFreqOk) QueryPerformanceCounter(&s_appStart);
}

static void PerfApplyEnd()
{
    if (!s_perfFreqOk) return;
    LARGE_INTEGER e; QueryPerformanceCounter(&e);
    s_applyUs += (double)(e.QuadPart - s_appStart.QuadPart) * 1e6 / (double)s_perfFreq.QuadPart;
    s_applyDraw++;
}

void GlslMaterial_OnFrame()
{
    s_stableProg = 0;
    // Report every 60 frames: avg μs/draw for the GLSL path (uniform upload + use).
    if (++s_fCount >= 60) {
        double avgUs = s_applyDraw ? s_applyUs / s_applyDraw : 0.0;
        DiagLog("[glsl] perf: %d GLSL draws per frame, avg %.1fus/draw CPU cost", s_applyDraw, avgUs);
        s_applyUs = 0.0; s_applyDraw = 0; s_fCount = 0;
    }
}

// Enum for which matched pair is active (cached per FP id).
enum { MATCH_NONE = 0, MATCH_DIFFUSE_MAIN, MATCH_ENV_REFL, MATCH_BUMP_ENV_SPEC,
       MATCH_DIFF_SIMPLE, MATCH_DIFF_NOLM, MATCH_HEADGEAR, MATCH_DOOR, MATCH_ARMOR_LEGACY,
       MATCH_WORLD_ENV_REFL, MATCH_WORLD_LM_ENV, MATCH_WORLD_BUMP_ENV, MATCH_WORLD_BUMP_ENV_GAMMA };

bool GlslMaterial_Apply()
{
    if (!s_available || !pglGetEnvFv) return false;
    if (!PbrTune_GlslMaterialEnabled()) return false;   // CPU toggle — no glGet on the hot path

    // --- Match FP by name (cached per id) ---
    static GLint s_lastFp = -1; static int s_fpMatchType = MATCH_NONE;
    GLint fp = 0; glGetIntegerv(GL_FRAGMENT_PROGRAM_BINDING_ARB, &fp);
    if (fp != s_lastFp) {
        s_lastFp = fp;
        const char *n = GetProgramName((GLuint)fp);
        if (n && strcmp(n, "fp_model_env_reflective") == 0)
            s_fpMatchType = MATCH_ENV_REFL;
        else if (n && strcmp(n, "fp_model_bump_env_spec") == 0)
            s_fpMatchType = MATCH_BUMP_ENV_SPEC;
        else if (n && strcmp(n, "fp_model_diff_simple") == 0)
            s_fpMatchType = MATCH_DIFF_SIMPLE;
        else if (n && strcmp(n, "fp_model_diff_nolm") == 0)
            s_fpMatchType = MATCH_DIFF_NOLM;
        else if (n && strcmp(n, "fp_model_headgear_legacy") == 0)
            s_fpMatchType = MATCH_HEADGEAR;
        else if (n && strcmp(n, "fp_door") == 0)
            s_fpMatchType = MATCH_DOOR;
        else if (n && strcmp(n, "fp_model_armor_legacy") == 0)
            s_fpMatchType = MATCH_ARMOR_LEGACY;
        else if (n && strcmp(n, "fp_worldtex_diffuse_main") == 0)
            s_fpMatchType = MATCH_DIFFUSE_MAIN;
        else if (n && strcmp(n, "fp_worldtex_env_reflective") == 0)
            s_fpMatchType = MATCH_WORLD_ENV_REFL;
        else if (n && strcmp(n, "fp_worldtex_lm_env") == 0)
            s_fpMatchType = MATCH_WORLD_LM_ENV;
        else if (n && strcmp(n, "fp_worldtex_bump_env") == 0)
            s_fpMatchType = MATCH_WORLD_BUMP_ENV;
        else if (n && strcmp(n, "fp_worldtex_bump_env_gamma") == 0)
            s_fpMatchType = MATCH_WORLD_BUMP_ENV_GAMMA;
        else
            s_fpMatchType = MATCH_NONE;
    }
    if (s_fpMatchType == MATCH_NONE) return false;

    // --- Match VP by name (cached per id). s_vpT2 records which variant of the
    //     world-env VP is bound so the FS links with the matching lightmap-UV source
    //     (vp_worldtex_env_fog_t2 → gl_MultiTexCoord1, vp_static_env_fog → gl_MultiTexCoord2). ---
    static GLint s_lastVp = -1; static int s_vpMatchType = MATCH_NONE; static bool s_vpT2 = false;
    GLint vp = 0; glGetIntegerv(GL_VERTEX_PROGRAM_BINDING_ARB, &vp);
    if (vp != s_lastVp) {
        s_lastVp = vp;
        const char *n = GetProgramName((GLuint)vp);
        if (n && strcmp(n, "vp_skinned_env_lit") == 0)
            s_vpMatchType = (s_fpMatchType == MATCH_ENV_REFL || s_fpMatchType == MATCH_BUMP_ENV_SPEC || s_fpMatchType == MATCH_ARMOR_LEGACY) ? s_fpMatchType : MATCH_NONE;
        else if (n && strcmp(n, "vp_skinned_bump_env") == 0)
            s_vpMatchType = (s_fpMatchType == MATCH_BUMP_ENV_SPEC) ? s_fpMatchType : MATCH_NONE;   // separate ident; skinned bump chars pair this VP
        else if (n && strcmp(n, "vp_skinned_lit_fog") == 0)
            s_vpMatchType = (s_fpMatchType == MATCH_DIFF_SIMPLE || s_fpMatchType == MATCH_DIFF_NOLM || s_fpMatchType == MATCH_HEADGEAR) ? s_fpMatchType : MATCH_NONE;
        else if (n && strcmp(n, "vp_static_lit_fog") == 0)
            s_vpMatchType = MATCH_DIFFUSE_MAIN;
        else if (n && strcmp(n, "vp_worldtex_env_fog_t2") == 0)
            s_vpMatchType = (s_fpMatchType == MATCH_WORLD_ENV_REFL || s_fpMatchType == MATCH_WORLD_LM_ENV) ? s_fpMatchType : MATCH_NONE;
        else if (n && strcmp(n, "vp_static_env_fog") == 0)
            s_vpMatchType = (s_fpMatchType == MATCH_WORLD_ENV_REFL || s_fpMatchType == MATCH_WORLD_LM_ENV || s_fpMatchType == MATCH_DOOR
                             || s_fpMatchType == MATCH_WORLD_BUMP_ENV || s_fpMatchType == MATCH_WORLD_BUMP_ENV_GAMMA) ? s_fpMatchType : MATCH_NONE;
        else if (n && strcmp(n, "vp_static_bump_env") == 0)
            s_vpMatchType = (s_fpMatchType == MATCH_WORLD_BUMP_ENV || s_fpMatchType == MATCH_WORLD_BUMP_ENV_GAMMA) ? s_fpMatchType : MATCH_NONE;
        else
            s_vpMatchType = MATCH_NONE;
        s_vpT2 = (n && strcmp(n, "vp_worldtex_env_fog_t2") == 0) ? true : false;
    }
    // Both FP and VP must agree on the same pair.
    if (s_vpMatchType == MATCH_NONE || s_vpMatchType != s_fpMatchType) return false;

    // Scope the override to depth-writing SOLID surfaces (the intended PBR target).
    // The engine draws projected/blob CHARACTER SHADOWS and glow decals with this same
    // pair but with depth-write OFF + blend; the GLSL port diverges on those (degenerate
    // flattened-mesh normals / blend interaction) and renders them solid black, while the
    // ARB path is fine. Falling back to ARB for depth-write-off draws fixes that and can
    // never lose quality (ARB is the faithful baseline). Alpha-tested foliage writes depth
    // (dMask=1), so it keeps the GLSL path.
    GLboolean dmask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &dmask);
    GlslCensus(dmask);   // diag: capture matched-pair draws (incl. dmask=0 fallbacks) once each
    if (!dmask) return false;

    // Skip draws where the ARB fragment program is DISABLED. The engine draws the skybox
    // (and other fixed-function surfaces) with no ARB program enabled, leaving only a STALE
    // program ID bound — so the name pair-match above still matches, but the engine intends
    // fixed-function. glUseProgram overrides fixed-function too, so my GLSL was hijacking
    // those draws (fog + sun on the sky → washed/overbright) where ARB mode leaves them
    // clean. Falling back renders them exactly as the ARB path does. (glIsEnabled is the
    // authoritative per-draw enable, unlike the stale binding id.)
    if (!glIsEnabled(GL_FRAGMENT_PROGRAM_ARB)) return false;

    // --- Select and use the matched program ---
    PerfApplyStart();
    if (s_fpMatchType == MATCH_ENV_REFL) {
        if (!s_triedEnvRefl) { s_triedEnvRefl = true; LoadEnvRefl(); }
        if (!s_progEnvRefl) return false;
        pglUseProgram(s_progEnvRefl);
        UploadStableForProg(s_progEnvRefl);
        UploadPerDrawEnvRefl();
        if ((++s_matApplied % 600) == 1)
            DiagLog("[glsl] material applied (count=%ld) fp=fp_model_env_reflective vp=vp_skinned_env_lit", s_matApplied);
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_BUMP_ENV_SPEC) {
        if (!s_triedBumpEnvSpec) { s_triedBumpEnvSpec = true; LoadBumpEnvSpec(); }
        if (!s_progBumpEnvSpec) return false;
        pglUseProgram(s_progBumpEnvSpec);
        UploadStableForProg(s_progBumpEnvSpec);
        UploadPerDrawEnvRefl();
        if ((++s_matApplied % 600) == 1)
            DiagLog("[glsl] material applied (count=%ld) fp=fp_model_bump_env_spec vp=vp_skinned_env_lit", s_matApplied);
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_DIFF_SIMPLE) {
        if (!s_triedDiffSimple) { s_triedDiffSimple = true; LoadDiffSimple(); }
        if (!s_progDiffSimple) return false;
        pglUseProgram(s_progDiffSimple);
        UploadStableForProg(s_progDiffSimple);
        UploadPerDrawSkinnedLit();
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_DIFF_NOLM) {
        if (!s_triedDiffNolm) { s_triedDiffNolm = true; LoadDiffNolm(); }
        if (!s_progDiffNolm) return false;
        pglUseProgram(s_progDiffNolm);
        UploadStableForProg(s_progDiffNolm);
        UploadPerDrawSkinnedLit();
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_HEADGEAR) {
        if (!s_triedHeadgear) { s_triedHeadgear = true; LoadHeadgear(); }
        if (!s_progHeadgear) return false;
        pglUseProgram(s_progHeadgear);
        UploadStableForProg(s_progHeadgear);
        UploadPerDrawSkinnedLit();
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_DOOR) {
        if (!s_triedDoor) { s_triedDoor = true; LoadDoor(); }
        if (!s_progDoor) return false;
        pglUseProgram(s_progDoor);
        UploadStableForProg(s_progDoor);
        UploadPerDrawWorldEnv();
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_ARMOR_LEGACY) {
        if (!s_triedArmorLegacy) { s_triedArmorLegacy = true; LoadArmorLegacy(); }
        if (!s_progArmorLegacy) return false;
        pglUseProgram(s_progArmorLegacy);
        UploadStableForProg(s_progArmorLegacy);
        UploadPerDrawEnvRefl();
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_WORLD_ENV_REFL) {
        if (s_vpT2) {
            if (!s_triedWorldEnvReflT2) { s_triedWorldEnvReflT2 = true; LoadWorldEnvReflT2(); }
            if (!s_progWorldEnvReflT2) return false;
            pglUseProgram(s_progWorldEnvReflT2);
            UploadStableForProg(s_progWorldEnvReflT2);
            UploadPerDrawWorldEnv();
            if ((++s_matApplied % 600) == 1)
                DiagLog("[glsl] material applied (count=%ld) fp=fp_worldtex_env_reflective vp=vp_worldtex_env_fog_t2", s_matApplied);
            PerfApplyEnd();
            return true;
        }
        if (!s_triedWorldEnvRefl) { s_triedWorldEnvRefl = true; LoadWorldEnvRefl(); }
        if (!s_progWorldEnvRefl) return false;
        pglUseProgram(s_progWorldEnvRefl);
        UploadStableForProg(s_progWorldEnvRefl);
        UploadPerDrawWorldEnv();
        if ((++s_matApplied % 600) == 1)
            DiagLog("[glsl] material applied (count=%ld) fp=fp_worldtex_env_reflective vp=vp_static_env_fog", s_matApplied);
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_WORLD_LM_ENV) {
        if (s_vpT2) {
            if (!s_triedWorldLmEnvT2) { s_triedWorldLmEnvT2 = true; LoadWorldLmEnvT2(); }
            if (!s_progWorldLmEnvT2) return false;
            pglUseProgram(s_progWorldLmEnvT2);
            UploadStableForProg(s_progWorldLmEnvT2);
            UploadPerDrawWorldEnv();
            if ((++s_matApplied % 600) == 1)
                DiagLog("[glsl] material applied (count=%ld) fp=fp_worldtex_lm_env vp=vp_worldtex_env_fog_t2", s_matApplied);
            PerfApplyEnd();
            return true;
        }
        if (!s_triedWorldLmEnv) { s_triedWorldLmEnv = true; LoadWorldLmEnv(); }
        if (!s_progWorldLmEnv) return false;
        pglUseProgram(s_progWorldLmEnv);
        UploadStableForProg(s_progWorldLmEnv);
        UploadPerDrawWorldEnv();
        if ((++s_matApplied % 600) == 1)
            DiagLog("[glsl] material applied (count=%ld) fp=fp_worldtex_lm_env vp=vp_static_env_fog", s_matApplied);
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_WORLD_BUMP_ENV) {
        if (!s_triedWorldBumpEnv) { s_triedWorldBumpEnv = true; LoadWorldBumpEnv(); }
        if (!s_progWorldBumpEnv) return false;
        pglUseProgram(s_progWorldBumpEnv);
        UploadStableForProg(s_progWorldBumpEnv);
        UploadPerDrawWorldEnv();
        if ((++s_matApplied % 600) == 1)
            DiagLog("[glsl] material applied (count=%ld) fp=fp_worldtex_bump_env vp=vp_static_env_fog", s_matApplied);
        PerfApplyEnd();
        return true;
    } else if (s_fpMatchType == MATCH_WORLD_BUMP_ENV_GAMMA) {
        if (!s_triedWorldBumpEnvGamma) { s_triedWorldBumpEnvGamma = true; LoadWorldBumpEnvGamma(); }
        if (!s_progWorldBumpEnvGamma) return false;
        pglUseProgram(s_progWorldBumpEnvGamma);
        UploadStableForProg(s_progWorldBumpEnvGamma);
        UploadPerDrawWorldEnv();
        if ((++s_matApplied % 600) == 1)
            DiagLog("[glsl] material applied (count=%ld) fp=fp_worldtex_bump_env_gamma vp=vp_static_env_fog", s_matApplied);
        PerfApplyEnd();
        return true;
    } else {
        // MATCH_DIFFUSE_MAIN
        if (!s_triedDiffuseMain) { s_triedDiffuseMain = true; LoadDiffuseMain(); }
        if (!s_progDiffuseMain) return false;
        pglUseProgram(s_progDiffuseMain);
        UploadStableForProg(s_progDiffuseMain);
        UploadPerDraw();
        if ((++s_matApplied % 600) == 1)
            DiagLog("[glsl] material applied (count=%ld) fp=fp_worldtex_diffuse_main vp=vp_static_lit_fog", s_matApplied);
        PerfApplyEnd();
        return true;
    }
}

void GlslMaterial_End()
{
    if (s_available && pglUseProgram) pglUseProgram(0);
}
