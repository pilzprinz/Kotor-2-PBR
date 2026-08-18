/*
Platform stub for ShaderOverride.
Pulls in Windows + OpenGL headers needed by glFunctions.h / glFunctions.cpp.
*/

#ifndef SHADER_OVERRIDE_PLATFORM_H
#define SHADER_OVERRIDE_PLATFORM_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

// ARB extension function pointer typedefs (subset used by ShaderOverride)
// Pulled in from glext.h equivalents.

#ifndef GL_VERTEX_PROGRAM_ARB
#define GL_VERTEX_PROGRAM_ARB   0x8620
#define GL_FRAGMENT_PROGRAM_ARB 0x8804
#endif

typedef void (WINAPI *PFNGLPROGRAMSTRINGARBPROC)(GLenum target, GLenum format, GLsizei len, const void *string);
typedef void (WINAPI *PFNGLBINDPROGRAMARBPROC)(GLenum target, GLuint program);
typedef void (WINAPI *PFNGLPROGRAMENVPARAMETER4DARBPROC)(GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);

#endif
