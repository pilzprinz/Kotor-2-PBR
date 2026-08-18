/*
Depth-buffer capture.

Snapshots the default-framebuffer depth attachment into a private
GL_DEPTH_COMPONENT24 2D texture bound to TMU 7, so fragment programs can
sample world-space depth at any screen-space UV. Triggered:

  - Once per frame at SwapBuffers (captures the full scene; used by next
    frame's wall/world FPs).
  - Once per frame just before the engine binds an fp_model_* override, so
    character/object FPs sample current-frame depth instead of last frame's
    (kills the visible 1-frame lag for moving characters).

Also IAT-hooks the swap entry points (gdi32!SwapBuffers and
opengl32!wglSwapBuffers) and exposes ARB/EXT framebuffer-object wrappers via
DepthCapture_InterceptProc — these are vestigial: K2 creates a few FBOs at
startup but never uses them for the scene depth pass, so the FBO interceptors
mostly just keep state mirrors warm for future diagnostics.
*/

#ifndef DEPTH_CAPTURE_H
#define DEPTH_CAPTURE_H

#include "platform.h"

// GL_EXT_framebuffer_object / GL_ARB_framebuffer_object enums.
#ifndef GL_FRAMEBUFFER_EXT
#define GL_FRAMEBUFFER_EXT                 0x8D40
#define GL_RENDERBUFFER_EXT                0x8D41
#define GL_READ_FRAMEBUFFER_EXT            0x8CA8
#define GL_DRAW_FRAMEBUFFER_EXT            0x8CA9
#define GL_DEPTH_ATTACHMENT_EXT            0x8D00
#define GL_STENCIL_ATTACHMENT_EXT          0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT        0x821A
#define GL_COLOR_ATTACHMENT0_EXT           0x8CE0
#define GL_DEPTH_COMPONENT16               0x81A5
#define GL_DEPTH_COMPONENT24               0x81A6
#define GL_DEPTH_COMPONENT32               0x81A7
#define GL_DEPTH24_STENCIL8                0x88F0
#endif

// TMU slot where the depth snapshot lives. Aspyr's K2 wrapper rejects
// glActiveTexture(TEXTURE11+); TMU 8/9/10 are used by PBR sibling textures,
// so 7 is the free slot.
#define DEPTH_CAPTURE_TMU 7

// EXT-suffix entry points (Aspyr wrapper exposes the EXT names).
typedef void (WINAPI *PFNGLGENFRAMEBUFFERS)(GLsizei, GLuint*);
typedef void (WINAPI *PFNGLDELETEFRAMEBUFFERS)(GLsizei, const GLuint*);
typedef void (WINAPI *PFNGLBINDFRAMEBUFFER)(GLenum, GLuint);
typedef void (WINAPI *PFNGLFRAMEBUFFERTEXTURE2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (WINAPI *PFNGLFRAMEBUFFERRENDERBUFFER)(GLenum, GLenum, GLenum, GLuint);
typedef void (WINAPI *PFNGLGENRENDERBUFFERS)(GLsizei, GLuint*);
typedef void (WINAPI *PFNGLDELETERENDERBUFFERS)(GLsizei, const GLuint*);
typedef void (WINAPI *PFNGLBINDRENDERBUFFER)(GLenum, GLuint);
typedef void (WINAPI *PFNGLRENDERBUFFERSTORAGE)(GLenum, GLenum, GLsizei, GLsizei);

extern "C" {

// Wrappers returned from the wglGetProcAddress hook.
void WINAPI my_glGenFramebuffersEXT(GLsizei n, GLuint *ids);
void WINAPI my_glDeleteFramebuffersEXT(GLsizei n, const GLuint *ids);
void WINAPI my_glBindFramebufferEXT(GLenum target, GLuint fbo);
void WINAPI my_glFramebufferTexture2DEXT(GLenum target, GLenum attach, GLenum texTarget, GLuint tex, GLint level);
void WINAPI my_glFramebufferRenderbufferEXT(GLenum target, GLenum attach, GLenum rbTarget, GLuint rb);
void WINAPI my_glGenRenderbuffersEXT(GLsizei n, GLuint *ids);
void WINAPI my_glDeleteRenderbuffersEXT(GLsizei n, const GLuint *ids);
void WINAPI my_glBindRenderbufferEXT(GLenum target, GLuint rb);
void WINAPI my_glRenderbufferStorageEXT(GLenum target, GLenum format, GLsizei w, GLsizei h);

}

// Routes the FBO entry-point name to the matching my_* wrapper and lazily
// resolves the system original through orig_wglGetProcAddress. Returns NULL
// when the name is not in our table — caller should fall through to the real
// wglGetProcAddress.
PROC DepthCapture_InterceptProc(const char *name);

// Install IAT hooks for gdi32!SwapBuffers and opengl32!wglSwapBuffers. K2 may
// call either depending on the path Aspyr's wrapper takes (gdi32 in practice).
void DepthCapture_InstallSwapHook();

// Snapshot the current default-framebuffer depth into the TMU 7 texture.
// Cost: one glCopyTexImage2D on first call (lazy alloc) or glCopyTexSubImage2D
// thereafter. Self-disables on first failure to avoid log spam.
void DepthCapture_SnapshotDefaultDepth();

#endif
