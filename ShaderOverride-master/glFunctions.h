/*
The MIT License (MIT)

ShaderOverride (c) 2016 HappyFunTimes
  derivative of gShaderReplacer (c) 2015 psycholns

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "platform.h"

#ifndef LIBRARY_DEF
#define LIBRARY_DEF extern
#endif

LIBRARY_DEF bool bFogOn;
LIBRARY_DEF GLdouble fogStart, fogEnd, fogDensity;
LIBRARY_DEF GLuint fogMode;
LIBRARY_DEF double fogBehavior[4];		// {DENSITY/LN(2), DENSITY/SQRT(LN(2)), -1/(END-START), END/(END-START)}
LIBRARY_DEF void InitShaderLookup();

typedef void (WINAPI *PFNGLFOGFPROC)(GLenum pname,  GLfloat param);
LIBRARY_DEF PFNGLFOGFPROC orig_glFogf;

typedef void (WINAPI *PFNGLFOGIPROC)(GLenum pname,  GLint param);
LIBRARY_DEF PFNGLFOGIPROC orig_glFogi;

typedef void (WINAPI *PFNGLFOGFVPROC)(GLenum pname,  GLfloat *param);
LIBRARY_DEF PFNGLFOGFVPROC orig_glFogfv;

typedef void (WINAPI *PFNGLFOGIVPROC)(GLenum pname,  GLint *param);
LIBRARY_DEF PFNGLFOGIVPROC orig_glFogiv;

typedef void (WINAPI *PFNGLGETINTEGERV)(GLenum pname,  GLint *param);
LIBRARY_DEF PFNGLGETINTEGERV orig_glGetIntegerv;

typedef PROC (WINAPI *PFNWGLGETPROCADDRESS)( const char * );
LIBRARY_DEF PFNWGLGETPROCADDRESS orig_wglGetProcAddress;

LIBRARY_DEF PFNGLPROGRAMENVPARAMETER4DARBPROC orig_glProgramEnvParameter4d;

LIBRARY_DEF PFNGLPROGRAMSTRINGARBPROC orig_glProgramString;
void WINAPI my_glProgramString( GLenum target, GLenum format, GLsizei len, const void *string );

LIBRARY_DEF PFNGLBINDPROGRAMARBPROC orig_glBindProgram;
void WINAPI my_glBindProgram(  GLenum target, GLuint program );

void fogRecalculate();