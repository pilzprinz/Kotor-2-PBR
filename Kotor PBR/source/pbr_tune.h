/*
In-game shader tuning overlay.

Toggle: DEL key. Navigation: Up/Down select param, Left/Right adjust.
Values pushed to program.env[24..40] each frame so shaders can read them.
Persisted to pbr_tune.ini next to opengl32.dll.
*/

#ifndef PBR_TUNE_H
#define PBR_TUNE_H

void PbrTune_Init();
void PbrTune_OnSwap();        // call from swap hook: poll keys, render overlay
void PbrTune_PushEnvParams(); // push tunables to program.env (call when PBR shader bound)
bool PbrTune_IsVisible();

// === Performance stats overlay =============================================
// Lightweight counters incremented from the hot path (draw calls, swap).
// Rendered as a small text overlay in the top-right corner when enabled
// via pbr_config.ini [performance] show_stats=1.
struct PbrStats {
    int   drawCalls;      // glDrawElements + glDrawArrays per frame
    int   casterDraws;    // shadow caster draws per frame
    int   cachedCasters;  // shadow geometry cache size
    int   siblingCache;   // PBR sibling cache size
    float fps;            // computed at swap
};
extern PbrStats g_stats;

// Call from my_glDrawElements / my_glDrawArrays.
void PbrStats_OnDraw();

// Call from OnSwapCommon — computes FPS, renders overlay, resets per-frame counters.
void PbrStats_OnSwap();


// True when the "GLSL material" slider is on. CPU-side read (no glGet) so the GLSL
// material path can gate cheaply on every draw.
bool PbrTune_GlslMaterialEnabled();

// stdio-based fallback diagnostic log next to opengl32.dll. Survives even if
// the file_logger CreateFileA path is broken. printf-style format.
void DiagLog(const char *fmt, ...);

#endif
