/*
GL fixed-function state capture.

IAT-hooks the GL entry points the engine uses to drive its fixed-function
state, so the proxy DLL mirrors the same data and can:

  - Trigger the depth snapshot at the right moments inside a frame
    (glDepthMask FALSE → opaque pass done; glBindProgram of an fp_model_*
    override → about to draw characters).
  - Track the live modelview / projection matrices for future view-space
    operations (proper SSAO, light-direction screen raymarch).
  - Track GL fixed-function light state (positions, colours, attenuation).

Hooks are installed once at DllMain time via IatHook; IAT redirection means
the engine's call lands in our wrapper before the DLL trampoline. No naked-
trampoline conversion — that crashed the game previously.
*/

#ifndef GL_STATE_CAPTURE_H
#define GL_STATE_CAPTURE_H

#include "platform.h"

// Install IAT hooks on swkotor2.exe and initialise mirrored state.
// Idempotent. Call after InitGL (originals must be populated) and once
// the loader has resolved imports — DLL_PROCESS_ATTACH is safe.
void GlStateCapture_Init();

// === Matrix state ====================================================
// Top-of-stack of the MODELVIEW mirror (column-major, OpenGL convention).
// Consumed only by the F2 sun-capture diagnostic. (No projection accessor:
// the projection stack is mirrored solely to keep projection-mode matrix
// writes from polluting this modelview mirror — nothing reads it.)
const float *GlState_Modelview();

// View matrix captured at first glPushMatrix in MODELVIEW mode each frame.
// Reset every frame via GlState_ResetFrameView (called from glClear hook).
// Returns NULL if no push has happened yet in the current frame.
extern "C" {
const float *GlState_FrameViewMatrix();
void         GlState_ResetFrameView();
}

// === Per-frame snapshot flags ========================================
// Returns 1 if either snapshot trigger fired this frame.
int  GlState_DidSnapshotThisFrame();

// Triggers the mid-frame snapshot intended to run right before the engine
// binds an fp_model_* override — by that point worldtex has already
// drawn, so character FPs sample current-frame depth instead of last-
// frame's. Idempotent within a frame; reset by glClear(GL_DEPTH_BUFFER_BIT).
void GlState_TriggerCharSnapshotIfNeeded();

// === Light state =====================================================
// GL_LIGHT0..GL_LIGHT7. position is in eye-space at the time glLightfv
// was called (OpenGL transforms position by current modelview).
struct GlLightState {
    int    enabled;
    float  position[4];     // (x,y,z,w) — w=0 directional, w=1 positional
    float  diffuse[4];
    float  ambient[4];
    float  specular[4];
    float  attenuation[3];  // (constant, linear, quadratic)
};
const GlLightState *GlState_Light(int idx);  // idx in [0..7], NULL out of range

#endif
