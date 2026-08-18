/*
GLSL subsystem — Stage 0: runtime feasibility probe.

This is an Aspyr GL wrapper (it already rejects glActiveTexture(TEXTURE11+)), so
GLSL support is NOT guaranteed. Before any GLSL feature work, GlslProbe_Init()
resolves the GLSL entry points, logs the GL/SL version + limits, and compiles +
links a trivial program. Every GLSL feature must gate on Glsl_Available().
*/

#ifndef GLSL_PROGRAM_H
#define GLSL_PROGRAM_H

// One-shot probe. Call once after the GL context is current (from OnFirstSwap).
// Logs everything to the PBR diag log; sets the availability flag.
void GlslProbe_Init();

// True only after GlslProbe_Init() succeeded: all entry points resolved AND a
// trivial vertex+fragment program compiled and linked. Gate features on this.
bool Glsl_Available();

// GLSL material replacement. Call at the COLOR draw. If GLSL is available, the
// "GLSL material" toggle is on, and the engine's currently-bound ARB FP/VP match a
// ported pair, binds the GLSL program (overriding the ARB programs), uploads the
// captured uniforms, and returns true — the caller issues the draw then calls
// GlslMaterial_End() to restore (glUseProgram 0). Returns false otherwise (caller
// draws normally through the engine's ARB programs).
bool GlslMaterial_Apply();
void GlslMaterial_End();

// Call once per frame from the swap hook: resets the per-frame uniform cache so the
// camera/slider/shadow-K uniforms are re-uploaded once on the next frame's first draw.
void GlslMaterial_OnFrame();

#endif
