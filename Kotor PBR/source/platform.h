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

// GL multitexture constants (ARB_multitexture / GL 1.3+)
#ifndef GL_TEXTURE0
#define GL_TEXTURE0  0x84C0
#define GL_TEXTURE1  0x84C1
#define GL_TEXTURE2  0x84C2
#define GL_TEXTURE3  0x84C3
#define GL_TEXTURE4  0x84C4
#define GL_TEXTURE5  0x84C5
#define GL_TEXTURE6  0x84C6
#define GL_TEXTURE7  0x84C7
#define GL_TEXTURE8  0x84C8
#define GL_TEXTURE9  0x84C9
#define GL_TEXTURE10 0x84CA
#define GL_TEXTURE11 0x84CB
#define GL_ACTIVE_TEXTURE 0x84E0
#endif

#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif

// Cube map constants (ARB_texture_cube_map / GL 1.3+)
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP            0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X 0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y 0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y 0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z 0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#define GL_TEXTURE_BINDING_CUBE_MAP    0x8514
#endif

// Mipmap auto-generation parameter (GL 1.4)
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif

#endif
