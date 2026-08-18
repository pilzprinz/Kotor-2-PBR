/*
PBR-specific OpenGL hooks.

Intercepts glBindTexture (trampoline slot 11) and glTexImage2D (slot 308):
- glTexImage2D: associate current GL-bound texture id with most-recently-opened
                Override filename. Also enables auto-mipmap generation for cube faces.
- glBindTexture: when binding a diffuse-slot texture, resolve PBR siblings, bind
                 them to TMU 8/9/10, push env params for the override shaders.
*/

#ifndef PBR_HOOKS_H
#define PBR_HOOKS_H

#include "platform.h"

extern "C" {

void __stdcall my_glBindTexturePBR(GLenum target, GLuint texture);
void __stdcall my_glTexImage2DPBR(GLenum target, GLint level, GLint internalformat,
                                  GLsizei width, GLsizei height, GLint border,
                                  GLenum format, GLenum type, const GLvoid *pixels);
void __stdcall my_glDeleteTexturesPBR(GLsizei n, const GLuint *textures);

// Called from pbr_tune's my_glClear hook at frame start. Resets per-frame
// scene-id capture state (e.g. first-2D-bind detection).
void PbrHooks_OnFrameClear();

}

void PbrHooksInit();

#endif
