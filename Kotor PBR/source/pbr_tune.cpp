/*
In-game shader tuning overlay (DEL toggles).

Text: GDI font rendered into a top-down DIB → uploaded as GL texture atlas,
drawn as textured quads. wglUseFontBitmaps was tried first and silently
no-op'd in the Aspyr OpenGL wrapper, so the atlas route is used.

Overlay drawn from depth_capture's MySwapBuffers callback before the real swap.
GL state saved/restored via glPushAttrib(GL_ALL_ATTRIB_BITS); vertex/fragment
programs explicitly disabled since K2 leaves them bound at swap time.

Tunables → program.env[24..28], pushed once at init and again on change.
Shader-side semantics live in fp_*.txt as PARAM tn/tnB/tnC/tnD/tnE declarations.
*/

#include "pbr_tune.h"
#include "pbr_config.h"
#include "file_logger.h"
#include "glfunctions.h"      // orig_glProgramEnvParameter4d, orig_wglGetProcAddress, p[]
#include "gl_state_capture.h" // GlState_FrameViewMatrix
#include "pbr_hooks.h"        // PbrHooks_OnFrameClear
#include "shadow_map.h"       // ShadowMap_OnDraw
#include "glsl_program.h"     // GlslMaterial_Apply/End (Stage-1 pilot)
#include "platform.h"
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifndef GL_FRAGMENT_PROGRAM_ARB
#define GL_FRAGMENT_PROGRAM_ARB 0x8804
#endif
#ifndef GL_VERTEX_PROGRAM_ARB
#define GL_VERTEX_PROGRAM_ARB   0x8620
#endif

// Reuses orig_glProgramEnvParameter4d (double variant) from glfunctions.h —
// already resolved in opengl32.cpp via orig_wglGetProcAddress. Tune values are
// floats but ARB program env is float internally; the d-variant accepts doubles
// and the engine converts. Avoids a second wglGetProcAddress resolver.

// ============================================================================
// Param table. One row per slider. Stored value is the live value.
// ============================================================================

struct TuneParam {
    const char *label;
    int   envSlot;     // 24, 25, ...
    int   envComp;     // 0=x, 1=y, 2=z, 3=w
    float value;
    float defVal;
    float minVal, maxVal;
    float step;
    const char *iniKey;
    const char *help;
};

// Grouped semantically: surface detail → material → reflections → local light
// (L0) → sun direction → sun intensity → sun color. Env-slot layout unchanged;
// only display order differs. Shadow row was cut (SS-shadow disabled).
static TuneParam g_params[] = {
    // --- Surface detail ---
    { "Detail nrm blend",    24, 1, 0.30f, 0.30f, 0.0f, 1.0f,  0.05f, "detail_blend",   "Detail normal map blend over the base normal. 0 = off, 1 = full detail." },
    { "Detail UV scale",     24, 2, 8.0f,  8.0f,  1.0f, 32.0f, 1.0f,  "detail_uv",      "Detail texture tiling scale. Higher = finer grain, lower = broader." },
    { "Sheen strength",      24, 0, 0.15f, 0.15f, 0.0f, 1.0f,  0.01f, "sheen_strength", "Grazing-angle sheen / rim boost on dark cloth-like surfaces." },

    // --- Material / albedo ---
    { "Alpha-shift K",       24, 3, 0.15f, 0.15f, 0.0f, 0.5f,  0.01f, "alpha_shift_k",  "Mask shift for transparent cutout edges (alpha cards)." },
    { "PBR diffuse dim",     27, 3, 0.0f,  0.0f,  0.0f, 1.0f,  0.01f, "pbr_diff_dim",   "Dims the diffuse term when a PBR metallic material is active. 0 = off." },

    // --- Reflection / envmap ---
    { "Perturb reflect mix", 25, 0, 0.30f, 0.30f, 0.0f, 1.0f,  0.05f, "perturb_mix",    "How much the normal map perturbs EnvMap reflections. 0 = flat, 1 = strong." },
    { "LOD bias scale",      25, 1, 5.0f,  5.0f,  0.0f, 16.0f, 0.5f,  "lod_scale",      "Texture LOD bias multiplier for reflection samples. Higher = blurrier." },
    { "Env reflect boost",   26, 0, 0.5f,  0.5f,  0.0f, 2.0f,  0.05f, "env_boost",      "Brightness boost for cube reflections. 0 = stock, 2 = strongly boosted." },

    // --- Local light (L0) ---
    { "L0 spec strength",    25, 2, 1.0f,  1.0f,  0.0f, 4.0f,  0.1f,  "l0_spec",        "Strength of the local light L0 specular highlight." },
    { "L0 spec exponent",    26, 1, 16.0f, 16.0f, 1.0f, 256.0f,2.0f,  "l0_exp",         "Sharpness of the L0 specular highlight. Higher = smaller, tighter spot." },

    // --- Sun direction ---
    { "Sun dir X",           27, 0, 0.3f,  0.3f,  -1.0f,1.0f,  0.05f, "sun_dir_x",      "Sun direction X. Rotates sun lighting and shadow direction." },
    { "Sun dir Y",           27, 1, 0.85f, 0.85f, -1.0f,1.0f,  0.05f, "sun_dir_y",      "Sun direction Y. Combined with X/Z it steers the sun." },
    { "Sun dir Z",           27, 2, 0.45f, 0.45f, -1.0f,1.0f,  0.05f, "sun_dir_z",      "Sun direction Z (up/down). Low = long shadows, ~1 = noon overhead." },

    // --- Sun intensity ---
    { "Sun diffuse intens",  28, 0, 1.0f,  1.0f,  0.0f, 2.0f,  0.05f, "sun_diff_i",     "Diffuse brightness of the directional sun." },
    { "Sun spec strength",   25, 3, 0.5f,  0.5f,  0.0f, 4.0f,  0.1f,  "sun_spec",       "Strength of the baked sun specular highlight." },
    { "Sun spec exponent",   26, 2, 16.0f, 16.0f, 1.0f, 256.0f,2.0f,  "sun_exp",        "Sharpness of the sun specular highlight. Higher = tighter." },

    // --- Sun color (HSV picker) ---
    // env[28].yzw is RGB in the shader; these rows store HSV (hue°, saturation, value/
    // brightness) and PbrTune_PushEnvParams converts H,S,V → R,G,B before pushing. The
    // value/brightness slider doubles as the sun-color brightness. Defaults = the old
    // RGB (1.0, 0.85, 0.7) expressed in HSV.
    { "Sun hue",             28, 1, 30.0f, 30.0f, 0.0f, 360.0f, 5.0f,  "sun_hue",        "Sun color hue in degrees (HSV). Mirrors the color picker on the right." },
    { "Sun saturation",      28, 2, 0.30f, 0.30f, 0.0f, 1.0f,   0.02f, "sun_sat",        "Sun color saturation (HSV). 0 = grayscale sun." },
    { "Sun brightness",      28, 3, 1.0f,  1.0f,  0.0f, 1.0f,   0.02f, "sun_val",        "Sun color brightness/value (HSV). Directly scales sun color." },

    // --- Shadow ---
    { "Shadow strength",     29, 0, 0.7f,  0.7f,  0.0f, 1.0f,  0.05f, "shadow_str",     "Overall directional shadow strength. 0 = shadows off, 1 = full." },
    { "Shadow darken",       30, 0, 0.6f,  0.6f,  0.0f, 1.0f,  0.05f, "shadow_darken",  "How dark shadowed areas get. 0 = invisible, 1 = near-black." },
    { "Shadow floor",        29, 2, 0.35f, 0.35f, 0.0f, 1.0f,  0.05f, "shadow_floor",   "Minimum light level inside shadows. 1 = shadows vanish, 0 = pure black." },
    { "Shadow bias",         29, 3, 0.0001f,0.0001f,0.0001f,0.05f,0.0001f,"shadow_bias",  "Light-space depth bias. Keep <= 0.001! Larger kills self-shadow." },
    { "Shadow nrm bias",     30, 3, 0.30f, 0.30f, 0.0f, 2.0f,  0.05f, "shadow_nrm_bias","Receiver offset along the surface normal (anti-acne). 0.3 is mild." },
    { "Shadow orbit R",      30, 1, 6.0f,  6.0f,  0.5f, 20.0f, 0.5f,  "shadow_orbit_r", "Camera-to-player orbit distance used to center the shadow box." },
    { "Shadow range",        30, 2, 90.0f, 90.0f, 25.0f,300.0f,5.0f,  "shadow_range",   "Half-extent of the shadow map in world units. Smaller = sharper detail." },
    // envSlot 32 is a sentinel: the env push loop skips it (s>=8). Value is routed
    // to the FBO via ShadowMap_SetResolution, not to a shader uniform.
    { "Shadow res",          32, 0, 4096.0f,4096.0f,1024.0f,8000.0f,256.0f,"shadow_res", "Shadow map resolution. Higher = sharper edges, more GPU cost." },
    { "Shadow cache",        31, 0, 1.0f,  1.0f,  0.0f, 1.0f,  1.0f,  "shadow_cache",   "Cache static world geometry in the shadow map (1 = on)." },
    { "Shadow geometry",     31, 1, 1.0f,  1.0f,  0.0f, 1.0f,  1.0f,  "shadow_geom",    "Static world geometry casts shadows (1 = on)." },
    { "Shadow models",       31, 2, 1.0f,  1.0f,  0.0f, 1.0f,  1.0f,  "shadow_models",  "Models and placeables cast shadows (1 = on)." },
    { "Shadow characters",   31, 3, 1.0f,  1.0f,  0.0f, 1.0f,  1.0f,  "shadow_chars",   "Characters cast shadows (1 = on)." },

    // --- Camera light (sky-behind-camera specular) ---
    { "Camera light strength", 34, 0, 0.0f,  0.0f,  0.0f, 2.0f,  0.05f, "cam_light_str",  "Fake rim light from behind the camera. 0 = off." },
    { "Camera light exponent", 34, 1, 16.0f, 16.0f, 1.0f, 256.0f,2.0f,  "cam_light_exp",  "Sharpness of the camera light highlight. Higher = tighter." },
    { "Camera light range",    34, 2, 15.0f, 15.0f, 1.0f, 100.0f,1.0f,  "cam_light_range","Max distance (world units) the camera light still affects." },

    // --- Debug ---
    { "Shadow viz",          29, 1, 0.0f,  0.0f,  0.0f, 1.0f,  0.05f, "shadow_viz",     "Debug: draw the shadow depth map as grayscale over surfaces." },
    // envSlot 33 sentinel (skipped by env push, s>=8): routed to ShadowMap_SetDiag.
    { "Shadow diag",         33, 0, 0.0f,  0.0f,  0.0f, 1.0f,  1.0f,  "shadow_diag",    "Debug: write per-frame caster diagnostics to the diag log." },
    // GLSL material A/B toggle. env[26].w is pushed to FRAGMENT env by the loop and
    // ignored by the ARB FPs (tnC reads .xyz); glsl_program.cpp reads it to decide
    // whether to override the (vp_static_lit_fog + fp_worldtex_diffuse_main) pair.
    { "GLSL material",       26, 3, 0.0f,  0.0f,  0.0f, 1.0f,  1.0f,  "glsl_material",  "Allows the GLSL shader reimplementation (0 = ARB, 1 = GLSL)." },
};
static const int kNumParams = sizeof(g_params) / sizeof(g_params[0]);

// ============================================================================
// State
// ============================================================================

// Font atlas dimensions: 16 cols × 6 rows = 96 cells covers ASCII 32..127.
static const int kCellW     = 8;
static const int kCellH     = 16;
static const int kAtlasCols = 16;
static const int kAtlasRows = 6;
static const int kAtlasW    = kAtlasCols * kCellW;
static const int kAtlasH    = kAtlasRows * kCellH;

static bool   g_visible       = false;
static int    g_selectedRow   = 0;
static GLuint g_fontTex       = 0;
static bool   g_fontReady     = false;
// Tune env-slot push: ARB env params are state-bound to the FP target, not
// per-program. Push once at init and again only when a value changes.
static bool   g_dirty           = false; // unsaved INI changes
static bool   g_tunePushNeeded  = true;

// Camera forward capture: snapped at FIRST glDrawElements/Arrays per frame.
// Reset on glClear so the next frame re-captures from the freshly set view.
static GLfloat g_frameViewMatrix[16];
static bool    g_frameViewCaptured = false;

static int   g_sunDirParamIdx[3] = { -1, -1, -1 }; // resolved at init
static int   g_dragRow           = -1;             // -1 = idle, else row being dragged
static int   g_cursorShown       = 0;              // ShowCursor() balance counter

// Edge-detection state for all polled keys/buttons.
static struct {
    bool del, up, down, left, right, home, end, capture, lb;
} g_prev;

// Overlay layout. Sized to fit longest label "Perturb reflect mix" + value + bar.
static const int kLayoutRowH   = 22;
static const int kLayoutPanelW = 580;
static const int kLayoutX0     = 20;
static const int kLayoutY0     = 20;
static const int kLayoutLabelX = kLayoutX0 + 8;
static const int kLayoutValueX = kLayoutLabelX + 200;
static const int kLayoutBarX   = kLayoutValueX + 60 + 4;
static const int kLayoutBarW   = kLayoutPanelW - (kLayoutBarX - kLayoutX0) - 12;

// Visual HSV color picker (sun color), to the right of the slider panel.
static const int kPickX    = kLayoutX0 + kLayoutPanelW + 24;  // SV square left
static const int kPickY    = kLayoutY0 + 30;                  // SV square top
static const int kPickSV   = 176;                             // SV square size (px)
static const int kPickHueW = 26;                              // hue strip width
static const int kPickGap  = 12;
static const int kPickHueX = kPickX + kPickSV + kPickGap;     // hue strip left

// Sun-color HSV param pointers (resolved once). The visual picker drives these;
// PbrTune_PushEnvParams converts H,S,V → RGB before pushing env[28].yzw.
static struct TuneParam *g_sunHue = 0, *g_sunSat = 0, *g_sunVal = 0;

static void CaptureSunFromView();
static bool IsIdentity(const float *m, float tol = 0.01f);

static inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// HSV → RGB. h in [0,360), s,v in [0,inf) (v may exceed 1 for an over-bright sun color).
// Standard sextant conversion. Sun-color sliders store HSV; the env push converts here.
static void HsvToRgb(double h, double s, double v, double &r, double &g, double &b)
{
    h = fmod(h, 360.0); if (h < 0.0) h += 360.0;
    if (s <= 0.0) { r = g = b = v; return; }
    double hh = h / 60.0;
    int    i  = (int)hh;
    double f  = hh - i;
    double p  = v * (1.0 - s);
    double q  = v * (1.0 - s * f);
    double t  = v * (1.0 - s * (1.0 - f));
    switch (i) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;   // case 5
    }
}

// Mark a tune value as changed: triggers env-slot push and INI auto-save on close.
static inline void MarkValueChanged() { g_dirty = true; g_tunePushNeeded = true; }

// Pixel Y of the bar/highlight for a row index, shared by render and hit-test.
static inline int RowYPos(int row) { return kLayoutY0 + kLayoutRowH * (row + 1) + 6; }

// ============================================================================
// INI persist
// ============================================================================

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
    snprintf(out, outSize, "%s", path);
}

static void GetIniPath(char *out, size_t outSize)
{
    char dir[MAX_PATH];
    GetDllDirPath(dir, sizeof(dir));
    snprintf(out, outSize, "%spbr_tune.ini", dir);
}

// Belt-and-suspenders diagnostic log. Writes via stdio fopen to
// <dll-dir>/pbr_tune_diag.log — survives even if the main file_logger
// is broken (CWD shenanigans, IAT hook recursion, etc).
void DiagLog(const char *fmt, ...)
{
    char dir[MAX_PATH];
    GetDllDirPath(dir, sizeof(dir));
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%spbr_tune_diag.log", dir);
    FILE *f = fopen(path, "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

// Module whose values are currently loaded into g_params (the per-location key).
// Empty = no area yet (main menu / startup) → everything uses the global section.
static char s_tuneModule[64] = "";

// Light + shadow APPEARANCE params are bound per-location (sun direction/color/
// intensity/spec, shadow strength/darken/floor/bias/nrm/orbit/range). Material
// params (detail, sheen, PBR, reflect) and debug toggles stay GLOBAL. Keyed by
// iniKey so the table rows don't each need a flag.
static bool IsPerLoc(const char *key)
{
    static const char *k[] = {
        "sun_dir_x", "sun_dir_y", "sun_dir_z", "sun_diff_i",
        "sun_hue", "sun_sat", "sun_val", "sun_spec", "sun_exp",
        "l0_spec", "l0_exp",
        "shadow_str", "shadow_darken", "shadow_floor", "shadow_bias",
        "shadow_nrm_bias", "shadow_orbit_r", "shadow_range",
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        if (strcmp(k[i], key) == 0) return true;
    return false;
}

// Load params for `module`: per-location params from the [module] section (falling
// back to [pbr_tune] when the location has no saved value yet); everything else from
// [pbr_tune]. Empty module → all from [pbr_tune].
static void LoadIniForModule(const char *module)
{
    char ini[MAX_PATH];
    GetIniPath(ini, sizeof(ini));
    char buf[64];
    bool perLocSection = (module && module[0]);
    for (int i = 0; i < kNumParams; i++) {
        TuneParam &p = g_params[i];
        buf[0] = 0;
        if (perLocSection && IsPerLoc(p.iniKey)) {
            GetPrivateProfileStringA(module, p.iniKey, "", buf, sizeof(buf), ini);
            if (!buf[0]) GetPrivateProfileStringA("pbr_tune", p.iniKey, "", buf, sizeof(buf), ini);
        } else {
            GetPrivateProfileStringA("pbr_tune", p.iniKey, "", buf, sizeof(buf), ini);
        }
        if (buf[0]) {
            float v = (float)atof(buf);
            if (v >= p.minVal && v <= p.maxVal) p.value = v;
        }
    }
    strncpy(s_tuneModule, module ? module : "", sizeof(s_tuneModule) - 1);
    s_tuneModule[sizeof(s_tuneModule) - 1] = 0;
}

static void LoadIni() { LoadIniForModule(""); }   // startup: global section only

static void SaveIni()
{
    char ini[MAX_PATH];
    GetIniPath(ini, sizeof(ini));
    bool perLocSection = (s_tuneModule[0] != 0);
    char buf[64];
    for (int i = 0; i < kNumParams; i++) {
        TuneParam &p = g_params[i];
        snprintf(buf, sizeof(buf), "%.4f", p.value);
        const char *section = (perLocSection && IsPerLoc(p.iniKey)) ? s_tuneModule : "pbr_tune";
        WritePrivateProfileStringA(section, p.iniKey, buf, ini);
    }
    g_dirty = false;
    char line[160];
    snprintf(line, sizeof(line), "[tune] saved (module=%s)\n", s_tuneModule[0] ? s_tuneModule : "global");
    PbrLogLine(line);
}

// Poll the active module each frame; on change, persist edits to the OUTGOING
// location's section then load the incoming location's params.
static void PbrTune_PollLocation()
{
    static long s_lastVer = -1;
    long v = Location_Version();
    if (v == s_lastVer) return;
    bool first = (s_lastVer == -1);
    s_lastVer = v;
    if (!first && g_dirty) SaveIni();          // save edits under the module we're leaving
    const char *m = Location_Module();
    LoadIniForModule(m);
    g_tunePushNeeded = true;                    // re-route shadow-range / res next frame
    g_dirty = false;
    DiagLog("[loc] entered '%s' — applied per-location tune", (m && m[0]) ? m : "(none)");
}

// ============================================================================
// Font setup — lazy on first render
// ============================================================================

// Build glyph atlas via GDI: render ASCII 32..127 into a memory DIB,
// upload as GL texture. Bypasses wglUseFontBitmaps which Aspyr's wrapper
// appears to no-op silently.
static HGLRC s_fontCtx = NULL;
static void EnsureFont()
{
    // Context/area reload destroys the atlas texture but leaves g_fontReady set,
    // so the overlay drew a dead texture id = garbled glyphs after travel. Detect
    // via context-handle change (recreation; id may be reused so glIsTexture can
    // be fooled) OR an invalid texture id, then force a rebuild.
    HGLRC curCtx = wglGetCurrentContext();
    if (g_fontReady && (curCtx != s_fontCtx || !glIsTexture(g_fontTex))) {
        g_fontReady = false;
        g_fontTex   = 0;
    }
    if (g_fontReady) return;

    HDC mem = CreateCompatibleDC(NULL);
    if (!mem) { PbrLogLine("[tune] font: CreateCompatibleDC failed\n"); return; }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kAtlasW;
    bi.bmiHeader.biHeight = -kAtlasH;  // top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *pBits = NULL;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!bmp || !pBits) { DeleteDC(mem); PbrLogLine("[tune] font: DIBSection failed\n"); return; }

    memset(pBits, 0, (size_t)kAtlasW * kAtlasH * 4);

    HFONT hf = CreateFontA(
        kCellH, kCellW, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
        "Lucida Console");
    if (!hf) {
        hf = CreateFontA(kCellH, kCellW, 0, 0, FW_NORMAL, 0, 0, 0,
                         ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         NONANTIALIASED_QUALITY, FIXED_PITCH, "Courier New");
    }
    if (!hf) { DeleteObject(bmp); DeleteDC(mem); return; }

    HBITMAP oldB = (HBITMAP)SelectObject(mem, bmp);
    HFONT   oldF = (HFONT)SelectObject(mem, hf);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));

    for (int c = 32; c < 128; c++) {
        int idx = c - 32;
        int col = idx % kAtlasCols;
        int row = idx / kAtlasCols;
        char ch = (char)c;
        TextOutA(mem, col * kCellW, row * kCellH, &ch, 1);
    }

    // Convert BGRX → RGBA (alpha = source brightness from R/G/B).
    size_t n = (size_t)kAtlasW * kAtlasH;
    unsigned char *rgba = (unsigned char *)malloc(n * 4);
    if (rgba) {
        const unsigned char *src = (const unsigned char *)pBits;
        for (size_t i = 0; i < n; i++) {
            unsigned char a = src[i * 4 + 0]; // BGRX: B == G == R for white text
            rgba[i * 4 + 0] = 255;
            rgba[i * 4 + 1] = 255;
            rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = a;
        }
        glGenTextures(1, &g_fontTex);
        glBindTexture(GL_TEXTURE_2D, g_fontTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kAtlasW, kAtlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
        g_fontReady = (g_fontTex != 0);
        s_fontCtx   = curCtx;   // bind atlas to this context
    }

    SelectObject(mem, oldF);
    SelectObject(mem, oldB);
    DeleteObject(hf);
    DeleteObject(bmp);
    DeleteDC(mem);

    char line[160];
    snprintf(line, sizeof(line), "[tune] font atlas tex=%u %dx%d ready=%d\n",
             g_fontTex, kAtlasW, kAtlasH, (int)g_fontReady);
    PbrLogLine(line);
}

static void DrawText2D(int x, int y, const char *txt)
{
    if (!g_fontReady || !txt) return;

    const float uStep = (float)kCellW / (float)kAtlasW;
    const float vStep = (float)kCellH / (float)kAtlasH;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_fontTex);
    glBegin(GL_QUADS);
    int penX = x;
    for (const unsigned char *p = (const unsigned char *)txt; *p; p++) {
        unsigned char c = *p;
        if (c < 32 || c > 127) { penX += kCellW; continue; }
        int idx = c - 32;
        int col = idx % kAtlasCols;
        int row = idx / kAtlasCols;
        float u0 = col * uStep;
        float u1 = u0 + uStep;
        float v0 = row * vStep;
        float v1 = v0 + vStep;
        int x0 = penX;
        int x1 = penX + kCellW;
        int y0 = y - kCellH + 2;   // y is text baseline; quad spans up by kCellH
        int y1 = y + 2;
        glTexCoord2f(u0, v0); glVertex2i(x0, y0);
        glTexCoord2f(u1, v0); glVertex2i(x1, y0);
        glTexCoord2f(u1, v1); glVertex2i(x1, y1);
        glTexCoord2f(u0, v1); glVertex2i(x0, y1);
        penX += kCellW;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// ============================================================================
// Overlay render — call BEFORE real wglSwapBuffers
// ============================================================================

static void DrawRect(int x, int y, int w, int h, float r, float g, float b, float a)
{
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2i(x,     y);
    glVertex2i(x + w, y);
    glVertex2i(x + w, y + h);
    glVertex2i(x,     y + h);
    glEnd();
}

static HWND GetGameHwnd();   // defined below (used by ComputeHoverRow)

// ============================================================================
// Tooltip — bottom overlay band describing the hovered / keyboard-selected slider.
// ============================================================================

static const int kTooltipH = 62;          // title line + up to 2 help lines + pad

// Split txt into up to maxLines lines of maxChars chars each (word wrap).
// Fills lines[][80]; returns the number of lines produced.
static int WordWrap(const char *txt, int maxChars, char lines[][80], int maxLines)
{
    if (!txt) return 0;
    char word[80];
    int n = 0;
    lines[0][0] = 0;
    int curLen = 0;
    const char *p = txt;
    while (*p && n < maxLines) {
        int wi = 0;
        while (*p && *p != ' ' && *p != '\t' && wi < 78) word[wi++] = *p++;
        word[wi] = 0;
        while (*p == ' ' || *p == '\t') p++;
        int add = wi + (curLen > 0 ? 1 : 0);
        if (curLen > 0 && add > maxChars) {
            n++;
            if (n >= maxLines) return n;
            lines[n][0] = 0;
            curLen = 0;
        }
        if (curLen > 0) strcat(lines[n], " ");
        strcat(lines[n], word);
        curLen = (int)strlen(lines[n]);
    }
    return n + 1;
}

// Hover row: -1 when the cursor isn't over a slider row. Hover wins over the
// keyboard selection so the tooltip follows the mouse; navigating with Up/Down
// still works when the cursor is elsewhere.
static int ComputeHoverRow()
{
    POINT pt;
    if (!GetCursorPos(&pt)) return -1;
    HWND hw = GetGameHwnd();
    if (hw) ScreenToClient(hw, &pt);
    for (int i = 0; i < kNumParams; i++) {
        int rowY  = RowYPos(i);
        int rowTop = rowY - 2;
        int rowBot = rowY + kLayoutRowH;
        if (pt.y >= rowTop && pt.y < rowBot &&
            pt.x >= kLayoutX0 && pt.x < kLayoutX0 + kLayoutPanelW)
            return i;
    }
    return -1;
}

static void DrawTooltip(int x0, int panelBottom, int viewH, int tipRow)
{
    if (tipRow < 0 || tipRow >= kNumParams) return;
    const TuneParam &p = g_params[tipRow];
    if (!g_fontReady) return;

    const int tipX = x0;
    const int tipW = kLayoutPanelW;
    int tipY = panelBottom + 8;
    if (tipY + kTooltipH > viewH) tipY = viewH - kTooltipH - 4;

    DrawRect(tipX, tipY, tipW, kTooltipH, 0.02f, 0.02f, 0.06f, 0.92f);
    DrawRect(tipX, tipY, tipW, 2, 1.0f, 0.85f, 0.2f, 1.0f);   // yellow top edge

    // Title: label (yellow) + range/step/INI hint (grey) on one line.
    char title[80], rng[96];
    snprintf(title, sizeof(title), "%s", p.label);
    snprintf(rng, sizeof(rng), "  min %.3g max %.3g step %.3g  [%s]",
             p.minVal, p.maxVal, p.step, p.iniKey);
    const int titleW = (int)strlen(title) * kCellW;
    glColor3f(1.0f, 0.85f, 0.2f);
    DrawText2D(tipX + 8, tipY + 18, title);
    glColor3f(0.55f, 0.55f, 0.65f);
    DrawText2D(tipX + 8 + titleW, tipY + 18, rng);

    // Body: wrapped help (up to 2 lines); "..." suffix if it overflows.
    const char *src = p.help ? p.help : "(no description)";
    char lines[2][80];
    int nl = WordWrap(src, 70, lines, 2);
    int len = (int)strlen(src);
    bool truncated = false;
    if (nl == 2 && len > 140) truncated = true;   // crude overflow hint
    glColor3f(0.85f, 0.85f, 0.90f);
    for (int i = 0; i < nl; i++) {
        char out[96];
        if (truncated && i == nl - 1) snprintf(out, sizeof(out), "%s...", lines[i]);
        else                          snprintf(out, sizeof(out), "%s", lines[i]);
        DrawText2D(tipX + 8, tipY + 38 + i * kCellH, out);
    }
}

// Resolve the sun-color HSV param pointers once (by iniKey, rename-safe).
static void ResolveSunColorParams()
{
    if (g_sunHue) return;
    for (int i = 0; i < kNumParams; i++) {
        if      (strcmp(g_params[i].iniKey, "sun_hue") == 0) g_sunHue = &g_params[i];
        else if (strcmp(g_params[i].iniKey, "sun_sat") == 0) g_sunSat = &g_params[i];
        else if (strcmp(g_params[i].iniKey, "sun_val") == 0) g_sunVal = &g_params[i];
    }
}

static void RenderOverlay()
{
    if (!g_visible) return;
    EnsureFont(); // diagnostic only; bars don't need it

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    int W = vp[2], H = vp[3];

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDisable(GL_VERTEX_PROGRAM_ARB);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    const int rowH   = kLayoutRowH;
    const int panelW = kLayoutPanelW;
    const int panelH = rowH * (kNumParams + 2) + 20;
    const int x0 = kLayoutX0, y0 = kLayoutY0;

    // Panel background
    DrawRect(x0, y0, panelW, panelH, 0.0f, 0.0f, 0.0f, 0.78f);

    // Title bar
    DrawRect(x0, y0, panelW, rowH, 0.15f, 0.15f, 0.30f, 0.85f);
    // Title tick-marker: yellow square at top-left indicates menu is alive
    DrawRect(x0 + 6, y0 + 4, rowH - 8, rowH - 8, 1.0f, 0.85f, 0.2f, 1.0f);

    for (int i = 0; i < kNumParams; i++) {
        int y = RowYPos(i);
        TuneParam &p = g_params[i];
        bool sel = (i == g_selectedRow);
        bool modified = (p.value != p.defVal);

        // Row highlight
        if (sel) DrawRect(x0 + 2, y - 2, panelW - 4, rowH, 0.35f, 0.30f, 0.10f, 0.7f);

        // Value bar: fraction from min to max
        float frac = Clamp01((p.value - p.minVal) / (p.maxVal - p.minVal));
        int barX = kLayoutBarX;
        int barW = kLayoutBarW;
        // Track
        DrawRect(barX, y + 2, barW, rowH - 8, 0.15f, 0.15f, 0.15f, 1.0f);
        // Fill
        int fillW = (int)(barW * frac);
        if (fillW > 0)
            DrawRect(barX, y + 2, fillW, rowH - 8,
                     sel ? 1.0f : 0.4f,
                     sel ? 0.85f : 0.7f,
                     sel ? 0.2f : 0.9f, 1.0f);
        // Default-value marker (thin vertical line)
        float defFrac = (p.defVal - p.minVal) / (p.maxVal - p.minVal);
        int defX = barX + (int)(barW * defFrac);
        DrawRect(defX, y, 1, rowH - 4, 1.0f, 1.0f, 1.0f, 0.6f);

        // Label (left) + value (between label and bar)
        if (g_fontReady) {
            // Modified indicator
            if (modified) DrawRect(x0 + 2, y + 4, 4, rowH - 12, 1.0f, 0.6f, 0.2f, 1.0f);

            char lbl[64], val[32];
            snprintf(lbl, sizeof(lbl), "%s", p.label);
            // Step-aware precision: sub-0.001 steps (e.g. shadow_bias 0.0001) need
            // 4 decimals or they render as "0.000" and look unsettable.
            snprintf(val, sizeof(val), p.step < 0.001f ? "%.4f" : "%.3f", p.value);
            glColor3f(sel ? 1.0f : 0.9f, sel ? 1.0f : 0.9f, sel ? 0.6f : 0.9f);
            DrawText2D(kLayoutLabelX, y + rowH - 7, lbl);
            DrawText2D(kLayoutValueX, y + rowH - 7, val);
        }
    }

    // Footer: dirty indicator + active location (per-location tune key)
    int yFoot = y0 + rowH * (kNumParams + 1) + 6;
    DrawRect(x0 + 8, yFoot + 4, 12, 12,
             g_dirty ? 1.0f : 0.3f,
             g_dirty ? 0.4f : 1.0f,
             g_dirty ? 0.2f : 0.4f, 1.0f);
    {
        char loc[96];
        snprintf(loc, sizeof(loc), "loc: %s", s_tuneModule[0] ? s_tuneModule : "(global)");
        DrawText2D(x0 + 26, yFoot + 14, loc);
    }

    // ---- Tooltip band: description of the hovered / selected slider ----
    {
        int hoverRow = ComputeHoverRow();
        int tipRow   = (hoverRow >= 0) ? hoverRow : g_selectedRow;
        DrawTooltip(x0, y0 + panelH, H, tipRow);
    }

    // ---- Visual HSV sun-color picker: SV square + hue strip + live swatch ----
    ResolveSunColorParams();
    if (g_sunHue && g_sunSat && g_sunVal) {
        float hue = g_sunHue->value;            // 0..360
        float sat = Clamp01(g_sunSat->value);   // 0..1
        float val = Clamp01(g_sunVal->value);   // 0..1
        double pr, pg, pb, cr, cg, cb;
        HsvToRgb(hue, 1.0, 1.0, pr, pg, pb);    // pure hue → SV square top-right corner
        HsvToRgb(hue, sat, val, cr, cg, cb);    // current color → swatch + marker fill

        int sx = kPickX, sy = kPickY, ss = kPickSV;
        int hx = kPickHueX, hw = kPickHueW;

        // Backing panel
        DrawRect(sx - 10, sy - 26, ss + kPickGap + hw + 20, ss + 64,
                 0.0f, 0.0f, 0.0f, 0.80f);
        if (g_fontReady) {
            glColor3f(1.0f, 1.0f, 0.7f);
            DrawText2D(sx, sy - 9, "Sun color (drag)");
        }

        // SV square: one bilinear quad. TL=white, TR=pure hue, BL/BR=black.
        // color(s,v) = v * lerp(white, hue, s) — exactly the SV plane.
        glBegin(GL_QUADS);
        glColor3f(1.0f, 1.0f, 1.0f);                 glVertex2i(sx,      sy);
        glColor3f((float)pr, (float)pg, (float)pb);  glVertex2i(sx + ss, sy);
        glColor3f(0.0f, 0.0f, 0.0f);                 glVertex2i(sx + ss, sy + ss);
        glColor3f(0.0f, 0.0f, 0.0f);                 glVertex2i(sx,      sy + ss);
        glEnd();

        // SV marker at (sat, 1-val): black ring, white box, color center.
        int mpx = sx + (int)(sat * ss);
        int mpy = sy + (int)((1.0f - val) * ss);
        DrawRect(mpx - 5, mpy - 5, 10, 10, 0.0f, 0.0f, 0.0f, 1.0f);
        DrawRect(mpx - 4, mpy - 4, 8,  8,  1.0f, 1.0f, 1.0f, 1.0f);
        DrawRect(mpx - 2, mpy - 2, 4,  4,  (float)cr, (float)cg, (float)cb, 1.0f);

        // Hue strip: 6 vertical gradient segments (red→yellow→…→red).
        glBegin(GL_QUADS);
        for (int k = 0; k < 6; k++) {
            double r0, g0, b0, r1, g1, b1;
            HsvToRgb(k * 60.0,       1.0, 1.0, r0, g0, b0);
            HsvToRgb((k + 1) * 60.0, 1.0, 1.0, r1, g1, b1);
            int yT = sy + (ss * k) / 6;
            int yB = sy + (ss * (k + 1)) / 6;
            glColor3f((float)r0, (float)g0, (float)b0); glVertex2i(hx,      yT);
            glColor3f((float)r0, (float)g0, (float)b0); glVertex2i(hx + hw, yT);
            glColor3f((float)r1, (float)g1, (float)b1); glVertex2i(hx + hw, yB);
            glColor3f((float)r1, (float)g1, (float)b1); glVertex2i(hx,      yB);
        }
        glEnd();

        // Hue marker (horizontal bar at the selected hue)
        int hyp = sy + (int)((hue / 360.0f) * ss);
        DrawRect(hx - 3, hyp - 2, hw + 6, 4, 0.0f, 0.0f, 0.0f, 1.0f);
        DrawRect(hx - 3, hyp - 1, hw + 6, 2, 1.0f, 1.0f, 1.0f, 1.0f);

        // Live swatch under the square
        int swy = sy + ss + 8;
        DrawRect(sx - 1, swy - 1, ss + 2, 24, 1.0f, 1.0f, 1.0f, 0.5f);    // border
        DrawRect(sx, swy, ss, 22, (float)cr, (float)cg, (float)cb, 1.0f); // color
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}

// ============================================================================
// Key polling — edge detection so single press = single step
// ============================================================================

static bool KeyEdge(int vk, bool &prev)
{
    bool now = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool edge = now && !prev;
    prev = now;
    return edge;
}

// Game window: cache lazily for ScreenToClient.
static HWND GetGameHwnd()
{
    static HWND cached = NULL;
    if (cached && IsWindow(cached)) return cached;
    HDC hdc = wglGetCurrentDC();
    if (hdc) cached = WindowFromDC(hdc);
    if (!cached) cached = GetActiveWindow();
    return cached;
}

static void ShowCursorBalanced(bool show)
{
    if (show) {
        int c = ShowCursor(TRUE);
        while (c < 0) { g_cursorShown++; c = ShowCursor(TRUE); }
    } else {
        while (g_cursorShown > 0) { ShowCursor(FALSE); g_cursorShown--; }
    }
}

static void SetParamFromFrac(TuneParam &p, float frac)
{
    frac = Clamp01(frac);
    float raw = p.minVal + frac * (p.maxVal - p.minVal);
    // Snap to step
    if (p.step > 0) {
        raw = p.minVal + p.step * (float)((int)((raw - p.minVal) / p.step + 0.5f));
    }
    if (raw < p.minVal) raw = p.minVal;
    if (raw > p.maxVal) raw = p.maxVal;
    if (raw != p.value) { p.value = raw; MarkValueChanged(); }
}

static void HandleMouse()
{
    bool lbDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool lbEdge = lbDown && !g_prev.lb;

    POINT pt;
    if (!GetCursorPos(&pt)) { g_prev.lb = lbDown; return; }
    HWND hw = GetGameHwnd();
    if (hw) ScreenToClient(hw, &pt);
    int mx = pt.x, my = pt.y;

    // Release drag on button-up
    if (!lbDown) { g_dragRow = -1; g_prev.lb = false; return; }

    ResolveSunColorParams();
    int psx = kPickX, psy = kPickY, pss = kPickSV;
    int phx = kPickHueX, phw = kPickHueW;

    // Acquire drag target on press (idle only). Picker regions take priority over rows.
    // g_dragRow sentinels: -2 = SV square, -3 = hue strip.
    if (lbEdge && g_dragRow == -1) {
        if (g_sunSat && g_sunVal && mx >= psx && mx < psx + pss && my >= psy && my < psy + pss) {
            g_dragRow = -2;
        } else if (g_sunHue && mx >= phx && mx < phx + phw && my >= psy && my < psy + pss) {
            g_dragRow = -3;
        } else {
            for (int i = 0; i < kNumParams; i++) {
                int rowY = RowYPos(i);
                int rowTop = rowY - 2;
                int rowBot = rowY + kLayoutRowH;
                if (my >= rowTop && my < rowBot &&
                    mx >= kLayoutX0 && mx < kLayoutX0 + kLayoutPanelW)
                {
                    g_dragRow = i;
                    g_selectedRow = i;
                    break;
                }
            }
        }
    }

    // Drag. SetParamFromFrac clamps the fraction, so off-region cursor saturates cleanly.
    if (g_dragRow == -2 && g_sunSat && g_sunVal) {
        SetParamFromFrac(*g_sunSat, (mx - psx) / (float)pss);         // saturation = x
        SetParamFromFrac(*g_sunVal, 1.0f - (my - psy) / (float)pss);  // value = y (top=1)
    } else if (g_dragRow == -3 && g_sunHue) {
        SetParamFromFrac(*g_sunHue, (my - psy) / (float)pss);         // hue = y (0..360)
    } else if (g_dragRow >= 0 && g_dragRow < kNumParams) {
        if (mx >= kLayoutBarX - 4 && mx <= kLayoutBarX + kLayoutBarW + 4) {
            float frac = (mx - kLayoutBarX) / (float)kLayoutBarW;
            SetParamFromFrac(g_params[g_dragRow], frac);
        }
    }

    g_prev.lb = lbDown;
}

static void AdjustSelected(int dir)
{
    if (g_selectedRow < 0 || g_selectedRow >= kNumParams) return;
    TuneParam &p = g_params[g_selectedRow];
    float mult = 1.0f;
    bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
    bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (shift) mult = 0.1f;
    if (ctrl)  mult = 10.0f;
    if (shift && ctrl) mult = 0.01f;
    float v = p.value + dir * p.step * mult;
    if (v < p.minVal) v = p.minVal;
    if (v > p.maxVal) v = p.maxVal;
    if (v != p.value) {
        p.value = v;
        MarkValueChanged();
    }
}

static void PollKeys()
{
    // DEL toggles. Auto-save when closing if dirty. Cursor balanced on toggle.
    if (KeyEdge(VK_DELETE, g_prev.del)) {
        g_visible = !g_visible;
        ShowCursorBalanced(g_visible);
        if (!g_visible && g_dirty) SaveIni();
    }

    if (!g_visible) return;
    HandleMouse();

    if (KeyEdge(VK_UP, g_prev.up)) {
        g_selectedRow = (g_selectedRow + kNumParams - 1) % kNumParams;
    }
    if (KeyEdge(VK_DOWN, g_prev.down)) {
        g_selectedRow = (g_selectedRow + 1) % kNumParams;
    }
    if (KeyEdge(VK_LEFT, g_prev.left))  AdjustSelected(-1);
    if (KeyEdge(VK_RIGHT, g_prev.right)) AdjustSelected(+1);
    if (KeyEdge(VK_HOME, g_prev.home)) {
        if (g_selectedRow >= 0 && g_selectedRow < kNumParams) {
            g_params[g_selectedRow].value = g_params[g_selectedRow].defVal;
            MarkValueChanged();
        }
    }
    // End = bake current values as new defaults (persists to INI).
    if (KeyEdge(VK_END, g_prev.end)) {
        for (int i = 0; i < kNumParams; i++) g_params[i].defVal = g_params[i].value;
        SaveIni();
        PbrLogLine("[tune] defaults updated to current values\n");
    }
    // Backspace = capture sun direction from current camera forward.
    if (KeyEdge(VK_BACK, g_prev.capture)) {
        PbrLogLine("[tune] Backspace pressed - capturing sun from view\n");
        DiagLog("Backspace pressed - capturing sun from view");
        CaptureSunFromView();
    }
}

// ============================================================================
// Public API
// ============================================================================

void PbrTune_Init()
{
    LoadIni();
    // Sun direction lives in env[27].xyz — locate the rows by (slot, comp)
    // so a label rename doesn't break the F2/Backspace sun capture.
    for (int i = 0; i < kNumParams; i++) {
        if (g_params[i].envSlot != 27) continue;
        int c = g_params[i].envComp;
        if (c >= 0 && c <= 2) g_sunDirParamIdx[c] = i;
    }
    PbrLogLine("[tune] init\n");
    DiagLog("=== pbr_tune diag log started ===");
    char dir[MAX_PATH]; GetDllDirPath(dir, sizeof(dir));
    DiagLog("dll-dir = %s", dir);
}

// Snapshot the current OpenGL modelview as candidate "view matrix" right
// before the first draw of the frame. K2 should have set its view matrix
// after glClear and before any glDrawElements/Arrays — at that moment
// modelview = view (no per-object world transform applied yet on the very
// first draw call, OR first-draw is the skybox with identity world).
static void TryCaptureModelviewAtFirstDraw()
{
    if (g_frameViewCaptured) return;
    GLfloat m[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    // Skip HUD/menu 2D draws — their modelview has identity rotation. Wait
    // for a draw with a real view matrix (rotation block non-identity).
    if (IsIdentity(m)) return;
    memcpy(g_frameViewMatrix, m, sizeof(m));
    g_frameViewCaptured = true;
}

// 3x3 rotation block-equal-to-identity check. Translation in r3 is OK to
// ignore — we treat any pure-translation matrix as "not a view matrix" because
// K2's HUD/menu passes use identity-rotation + small translate.
static bool IsIdentity(const float *m, float tol)
{
    if (!m) return true;
    return  fabsf(m[0]  - 1) < tol && fabsf(m[5]  - 1) < tol &&
            fabsf(m[10] - 1) < tol &&
            fabsf(m[1]) < tol && fabsf(m[2]) < tol && fabsf(m[4]) < tol &&
            fabsf(m[6]) < tol && fabsf(m[8]) < tol && fabsf(m[9]) < tol;
}

static void LogMatrix(const char *tag, const float *m)
{
    if (!m) {
        char b[64]; snprintf(b, sizeof(b), "[tune] %s: NULL\n", tag);
        PbrLogLine(b);
        DiagLog("%s: NULL", tag);
        return;
    }
    char buf[320];
    snprintf(buf, sizeof(buf),
        "[tune] %s: r0=(%.3f,%.3f,%.3f,%.3f) r1=(%.3f,%.3f,%.3f,%.3f) "
        "r2=(%.3f,%.3f,%.3f,%.3f) r3=(%.3f,%.3f,%.3f,%.3f)%s\n",
        tag,
        m[0], m[4], m[8], m[12],
        m[1], m[5], m[9], m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15],
        IsIdentity(m) ? "  [IDENTITY]" : "");
    PbrLogLine(buf);
    DiagLog("%s: r0=(%.3f,%.3f,%.3f,%.3f) r1=(%.3f,%.3f,%.3f,%.3f) "
            "r2=(%.3f,%.3f,%.3f,%.3f) r3=(%.3f,%.3f,%.3f,%.3f)%s",
        tag,
        m[0], m[4], m[8], m[12],
        m[1], m[5], m[9], m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15],
        IsIdentity(m) ? "  [IDENTITY]" : "");
}

// Try every available source for a view matrix and pick the first non-identity.
static void CaptureSunFromView()
{
    GLfloat now[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, now);
    const float *mvTop   = GlState_Modelview();        // gl_state_capture top-of-stack
    const float *mvFirst = GlState_FrameViewMatrix();  // saved at first glPushMatrix
    const float *mvDraw  = g_frameViewCaptured ? g_frameViewMatrix : NULL;

    LogMatrix("F2 glGetFloatv NOW",       now);
    LogMatrix("F2 gl_state mv top",       mvTop);
    LogMatrix("F2 gl_state first push",   mvFirst);
    LogMatrix("F2 first-draw snapshot",   mvDraw);

    const float *m   = NULL;
    const char  *src = "none";
    if (mvFirst && !IsIdentity(mvFirst)) { m = mvFirst; src = "gl_state_first_push"; }
    else if (mvTop   && !IsIdentity(mvTop))   { m = mvTop;   src = "gl_state_top"; }
    else if (mvDraw  && !IsIdentity(mvDraw))  { m = mvDraw;  src = "first_draw_snapshot"; }
    else if (!IsIdentity(now))                { m = now;     src = "glGetFloatv_now"; }

    if (!m) {
        PbrLogLine("[tune] F2: all candidates IDENTITY — can't derive sun direction\n");
        return;
    }
    float fx = -m[2];
    float fy = -m[6];
    float fz = -m[10];
    float L = sqrtf(fx*fx + fy*fy + fz*fz);
    if (L > 1e-6f) { fx /= L; fy /= L; fz /= L; }
    if (g_sunDirParamIdx[0] >= 0) g_params[g_sunDirParamIdx[0]].value = fx;
    if (g_sunDirParamIdx[1] >= 0) g_params[g_sunDirParamIdx[1]].value = fy;
    if (g_sunDirParamIdx[2] >= 0) g_params[g_sunDirParamIdx[2]].value = fz;
    MarkValueChanged();
    char buf[128];
    snprintf(buf, sizeof(buf),
        "[tune] sun captured (src=%s) forward=(%.3f, %.3f, %.3f)\n",
        src, fx, fy, fz);
    PbrLogLine(buf);
}

bool PbrTune_GlslMaterialEnabled()
{
    static int idx = -2;   // -2 = unresolved, -1 = not found
    if (idx == -2) {
        idx = -1;
        for (int i = 0; i < kNumParams; i++)
            if (strcmp(g_params[i].iniKey, "glsl_material") == 0) { idx = i; break; }
    }
    return idx >= 0 && g_params[idx].value >= 0.5f;
}

void PbrTune_OnSwap()
{
    PbrTune_PollLocation();
    PollKeys();
    RenderOverlay();
}

void PbrTune_PushEnvParams()
{
    // Push every call. Engine may write env[24..31] for its own purposes between
    // our pushes; the per-bind cost is 8 env writes (~negligible).
    if (!orig_glProgramEnvParameter4d) return;

    double slot[12][4] = { {0} };
    for (int i = 0; i < kNumParams; i++) {
        int s = g_params[i].envSlot - 24;
        int c = g_params[i].envComp;
        if (s < 0 || (s >= 8 && s < 10)) continue;   // skip sentinels 32,33; keep cam-light 34,35
        if (s >= 0 && s < 12) slot[s][c] = (double)g_params[i].value;
    }
    // Sun color: env[28].yzw (slot index 4) is RGB in the shader, but the sliders store
    // HSV (hue°, saturation, value/brightness). Convert before pushing. env[28].x (slot
    // 4,comp 0) = sun diffuse intensity, left untouched.
    {
        double r, g, b;
        HsvToRgb(slot[4][1], slot[4][2], slot[4][3], r, g, b);
        slot[4][1] = r; slot[4][2] = g; slot[4][3] = b;
    }
    for (int s = 0; s < 12; s++) {
        if (s == 8 || s == 9) continue;   // skip env[32..33] (shadow-map sentinels)
        orig_glProgramEnvParameter4d(GL_FRAGMENT_PROGRAM_ARB, 24 + s,
            slot[s][0], slot[s][1], slot[s][2], slot[s][3]);
    }

    // Everything below is a global slider setting (shadow resolution, diag toggle,
    // one-shot log) that does NOT change between draws. This function runs on every
    // diffuse bind (hundreds/frame) so the engine can't permanently stomp env[24..31]
    // — but that routing must not repeat per bind. Gate it on g_tunePushNeeded (set
    // once per frame by glClear and on any slider edit) so it applies once/frame.
    // (Per-bind ShadowMap_SetDiag also reset the diag mesh-lock every call.)
    if (!g_tunePushNeeded) return;
    g_tunePushNeeded = false;

    // Shadow resolution: a C++/FBO concern, not a shader uniform. Route the
    // slider value to the shadow subsystem (idempotent; recreate is deferred to
    // the next caster pass). Cache the row index on first call.
    static int s_resIdx = -2;
    if (s_resIdx == -2) {
        s_resIdx = -1;
        for (int i = 0; i < kNumParams; i++)
            if (g_params[i].envSlot == 32) { s_resIdx = i; break; }
    }
    if (s_resIdx >= 0) ShadowMap_SetResolution((int)g_params[s_resIdx].value);

    // Shadow diag toggle (env 33 sentinel) — routed C++-side, not a GL uniform.
    static int s_diagIdx = -2;
    if (s_diagIdx == -2) {
        s_diagIdx = -1;
        for (int i = 0; i < kNumParams; i++)
            if (g_params[i].envSlot == 33) { s_diagIdx = i; break; }
    }
    if (s_diagIdx >= 0) ShadowMap_SetDiag((int)(g_params[s_diagIdx].value + 0.5f));

    // One-shot diagnostic on F2 capture or first push after init — write the
    // slot values to log so user can confirm env[27] reflects slider state.
    static int s_logCount = 0;
    if (s_logCount < 3) {
        s_logCount++;
        char buf[256];
        snprintf(buf, sizeof(buf),
            "[tune] push #%d env[24]=(%.3f,%.3f,%.3f,%.3f) env[25]=(%.3f,%.3f,%.3f,%.3f) "
            "env[26]=(%.3f,%.3f,%.3f,%.3f) env[27]=(%.3f,%.3f,%.3f,%.3f)\n",
            s_logCount,
            slot[0][0], slot[0][1], slot[0][2], slot[0][3],
            slot[1][0], slot[1][1], slot[1][2], slot[1][3],
            slot[2][0], slot[2][1], slot[2][2], slot[2][3],
            slot[3][0], slot[3][1], slot[3][2], slot[3][3]);
        PbrLogLine(buf);
    }
}

bool PbrTune_IsVisible() { return g_visible; }

// =========================================================================
// Frame hooks (glClear / glDrawElements / glDrawArrays) — exported through
// opengl32.def so the engine's calls are routed here. View-matrix capture
// fires on the FIRST draw call of each frame, right after glClear.
// =========================================================================

// Trampoline indices into p[] (must match opengl32.def @ordinals - 1).
static const int TRAMP_GLBEGIN        = 10;
static const int TRAMP_GLCLEAR        = 16;
static const int TRAMP_GLDRAWARRAYS   = 72;
static const int TRAMP_GLDRAWELEMENTS = 74;

extern "C" void __stdcall my_glClear(GLbitfield mask)
{
    if (mask & GL_COLOR_BUFFER_BIT) {
        g_frameViewCaptured = false;
        // Re-push tune env slots every frame so engine code that touches
        // program.env[24..27] for its own purposes can't permanently stomp us.
        g_tunePushNeeded = true;
        GlState_ResetFrameView();
        PbrHooks_OnFrameClear();
    }
    typedef void (WINAPI *PFN)(GLbitfield);
    PFN real = (PFN)p[TRAMP_GLCLEAR];
    if (real) real(mask);
}

// One-time wiring of the real draw fns into the shadow geometry cache, done
// lazily on the first draw (p[] is guaranteed populated by then).
static bool s_realDrawSet = false;
static inline void EnsureRealDrawFns()
{
    if (s_realDrawSet) return;
    ShadowMap_SetRealDrawFns((void*)p[TRAMP_GLDRAWELEMENTS], (void*)p[TRAMP_GLDRAWARRAYS]);
    s_realDrawSet = true;
}

extern "C" void __stdcall my_glDrawElements(GLenum mode, GLsizei count,
                                            GLenum type, const GLvoid *indices)
{
    TryCaptureModelviewAtFirstDraw();
    ShadowMap_OnDraw();
    PbrStats_OnDraw();
    typedef void (WINAPI *PFN)(GLenum, GLsizei, GLenum, const GLvoid *);
    PFN real = (PFN)p[TRAMP_GLDRAWELEMENTS];
    if (!real) return;
    EnsureRealDrawFns();
    // Cache path captures static casters (returns true → skip live double-draw).
    if (!ShadowMap_TryCaptureCaster(mode, count, type, indices, 0, 0)) {
        if (ShadowMap_BeforeDraw()) {
            real(mode, count, type, indices);
            ShadowMap_AfterDraw();
        }
    }
    // Color draw — optionally through the GLSL pilot (overrides ARB for its target pair).
    if (GlslMaterial_Apply()) {
        real(mode, count, type, indices);
        GlslMaterial_End();
    } else {
        real(mode, count, type, indices);
    }
}

extern "C" void __stdcall my_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    TryCaptureModelviewAtFirstDraw();
    ShadowMap_OnDraw();
    PbrStats_OnDraw();
    typedef void (WINAPI *PFN)(GLenum, GLint, GLsizei);
    PFN real = (PFN)p[TRAMP_GLDRAWARRAYS];
    if (!real) return;
    EnsureRealDrawFns();
    if (!ShadowMap_TryCaptureCaster(mode, count, 0, 0, 1, first)) {
        if (ShadowMap_BeforeDraw()) {
            real(mode, first, count);
            ShadowMap_AfterDraw();
        }
    }
    // Color draw — optionally through the GLSL pilot (overrides ARB for its target pair).
    if (GlslMaterial_Apply()) {
        real(mode, first, count);
        GlslMaterial_End();
    } else {
        real(mode, first, count);
    }
}

extern "C" void __stdcall my_glBegin(GLenum mode)
{
    // Immediate-mode K2 paths use glBegin → capture matrix at first non-HUD begin.
    TryCaptureModelviewAtFirstDraw();
    ShadowMap_OnDraw();   // count immediate-mode draws too — shadow blob may use this path
    PbrStats_OnDraw();
    typedef void (WINAPI *PFN)(GLenum);
    PFN real = (PFN)p[TRAMP_GLBEGIN];
    if (real) real(mode);
}

// ============================================================================
// Performance stats overlay
// ============================================================================

PbrStats g_stats = { 0, 0, 0, 0, 0.0f };

static int    s_statsFrameCount = 0;
static bool   s_statsVisible = false;
static bool   s_statsPrevKey = false;

void PbrStats_OnDraw()
{
    g_stats.drawCalls++;
}

void PbrStats_OnSwap()
{
    // Toggle via configured hotkey (default VK_END = 0x23).
    // But VK_END is already used by the tune overlay for "bake defaults".
    // So use a different key: VK_F11 for stats toggle.
    bool keyNow = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (keyNow && !s_statsPrevKey) {
        s_statsVisible = !s_statsVisible;
    }
    s_statsPrevKey = keyNow;

    // Compute FPS
    s_statsFrameCount++;
    static double s_lastFpsTime = 0.0;
    double now = (double)GetTickCount() / 1000.0;
    if (s_lastFpsTime == 0.0) s_lastFpsTime = now;
    double elapsed = now - s_lastFpsTime;
    if (elapsed >= 0.5) {
        g_stats.fps = (float)((double)s_statsFrameCount / elapsed);
        s_statsFrameCount = 0;
        s_lastFpsTime = now;
    }

    // Get cache sizes from subsystems
    g_stats.cachedCasters = ShadowMap_IsAvailable() ? 1 : 0; // placeholder; real count internal to shadow_map

    if (!s_statsVisible) {
        g_stats.drawCalls = 0;
        g_stats.casterDraws = 0;
        return;
    }

    // Render stats overlay (top-right corner)
    EnsureFont();
    if (!g_fontReady) { g_stats.drawCalls = 0; return; }

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    int W = vp[2], H = vp[3];

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);
    glDisable(GL_VERTEX_PROGRAM_ARB);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    char line[128];
    snprintf(line, sizeof(line), "FPS: %.1f  Draws: %d", g_stats.fps, g_stats.drawCalls);
    int textW = (int)strlen(line) * kCellW;
    int x0 = W - textW - 20;
    int y0 = 10;

    DrawRect(x0 - 8, y0 - 4, textW + 16, kCellH + 8, 0.0f, 0.0f, 0.0f, 0.75f);
    glColor3f(0.7f, 1.0f, 0.7f);
    DrawText2D(x0, y0 + kCellH - 2, line);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();

    // Reset per-frame counters
    g_stats.drawCalls = 0;
    g_stats.casterDraws = 0;
}
