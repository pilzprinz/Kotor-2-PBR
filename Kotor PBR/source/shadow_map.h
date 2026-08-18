/*
Shadow map subsystem — directional sun shadow.

Renders the world from the sun's POV into a depth FBO (caster pass), then the
main fragment programs sample that depth tex to gate the sun's contribution.
See docs/SHADOW_MAP.md for the full design.
*/

#ifndef SHADOW_MAP_H
#define SHADOW_MAP_H

// Init after a GL context exists (first SwapBuffers). Creates FBO + depth tex,
// compiles caster VPs. Logs probe result to pbr_tune_diag.log.
void ShadowMap_Init();

// Request a new shadow-map resolution (px, clamped 1024..8000, rounded to 64).
// The FBO/depth-tex recreate is deferred to the next BeginCasterPass so it runs
// on the GL thread with a current context. No-op if unchanged.
void ShadowMap_SetResolution(int px);

// Toggle per-frame shadow diagnostics ("Shadow diag" slider). When on, logs box
// anchor stability + a tracked dynamic caster's light-space texel position every
// frame so jitter sources can be measured instead of guessed.
void ShadowMap_SetDiag(int on);

// True if FBO + depth tex were created and status == COMPLETE.
bool ShadowMap_IsAvailable();

// Caster-pass plumbing.
//   BeginCaster fires from MyClear when GL_DEPTH_BUFFER_BIT set (frame start):
//     clears shadow FBO, rebuilds the light-space matrices.
//   EndCaster fires from the SwapBuffers hook: binds shadow tex to its TMU.
void ShadowMap_BeginCasterPass();
void ShadowMap_EndCasterPass();

// Per-draw notification from my_glDrawElements/Arrays/glBegin trampolines.
// Counts caster-pass draws + logs the per-(fp,vp) histogram on log frames.
void ShadowMap_OnDraw();

// Wrap each engine draw call. Pattern:
//
//   ShadowMap_OnDraw();
//   if (ShadowMap_BeforeDraw()) {
//       real_draw(...);          // renders to shadow FBO
//       ShadowMap_AfterDraw();   // restores main FB + viewport + colormask + VP
//   }
//   real_draw(...);              // engine's intended render to main FB
//
// Returns false when the draw should not cast (not in caster pass, sub-window
// viewport, non-world FP, or no caster VP) — caller skips the shadow draw.
bool ShadowMap_BeforeDraw();
void ShadowMap_AfterDraw();

// Geometry cache. SetRealDrawFns must be called once (from PbrTune init) with the
// engine's real glDrawElements/glDrawArrays so the cache can replay geometry
// without re-entering our own draw hooks.
void ShadowMap_SetRealDrawFns(void *drawElements, void *drawArrays);

// Called from the draw trampolines. When the geometry cache is on and the draw
// is a cacheable STATIC caster, records it for the once-per-frame cache render
// and returns true (caller then skips the live double-draw). Returns false for
// skinned/non-cacheable draws or when the cache is off → caller runs the live
// BeforeDraw/AfterDraw path.
// Plain types (no GL headers needed here): GLenum=unsigned, GLsizei=int.
bool ShadowMap_TryCaptureCaster(unsigned mode, int count, unsigned type,
                                const void *indices, int isArrays, int first);

#endif
