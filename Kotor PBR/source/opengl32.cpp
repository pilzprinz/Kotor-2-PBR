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

#include "glfunctions.h"
#include "file_logger.h"
#include "pbr_hooks.h"
#include "depth_capture.h"
#include "gl_state_capture.h"
#include "pbr_tune.h"
#include "pbr_config.h"
#include "pbr_state.h"
#include <windows.h>
#pragma pack(1)

FARPROC p[368] = { 0 };
void InitGL();

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		// Load configuration first — all subsystem inits check g_config flags.
		PbrConfig_Load();

		// NOTE: VEH (Vectored Exception Handler) was removed because it can
		// interfere with the Aspyr wrapper's internal exception handling.
		// The wrapper uses SEH for flow control (copy protection, etc.),
		// and a process-wide VEH that logs every exception via
		// OutputDebugStringA can disrupt that flow, causing the game to
		// exit silently after the splash screens.

		fogStart = 0;
		fogEnd = 1;
		fogDensity = 1;
		fogMode = GL_EXP;
		bFogOn = false;
		InitGL();
		InitShaderLookup();
		if (g_config.enableFileLogger)   InitFileLogger();
		if (g_config.enablePBR)          PbrHooksInit();
		if (g_config.enableTuneOverlay)  PbrTune_Init();
		DepthCapture_InstallSwapHook();
		GlStateCapture_Init();
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		// Only shut down the file logger. PbrShutdown() was removed because
		// clearing std::map at DLL_PROCESS_DETACH time can crash if the CRT
		// heap is already partially torn down. The OS reclaims all memory
		// on process exit anyway.
		ShutdownFileLogger();
	}

	return TRUE;
}

void InitGL()
{
	// Build path to the system opengl32.dll safely. The original code used
	// manual pointer arithmetic to append the filename — fragile if
	// GetSystemDirectory fills the buffer near its limit.
	char DefaultGLLibName[MAX_PATH];
	DWORD len = GetSystemDirectoryA(DefaultGLLibName, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) return;
	snprintf(DefaultGLLibName + len, MAX_PATH - len, "\\opengl32.dll");

	HINSTANCE hL = LoadLibraryA(DefaultGLLibName);
	if (!hL) return;

	p[0] = GetProcAddress(hL, "GlmfBeginGlsBlock");
	p[1] = GetProcAddress(hL, "GlmfCloseMetaFile");
	p[2] = GetProcAddress(hL, "GlmfEndGlsBlock");
	p[3] = GetProcAddress(hL, "GlmfEndPlayback");
	p[4] = GetProcAddress(hL, "GlmfInitPlayback");
	p[5] = GetProcAddress(hL, "GlmfPlayGlsRecord");
	p[6] = GetProcAddress(hL, "glAccum");
	p[7] = GetProcAddress(hL, "glAlphaFunc");
	p[8] = GetProcAddress(hL, "glAreTexturesResident");
	p[9] = GetProcAddress(hL, "glArrayElement");
	p[10] = GetProcAddress(hL, "glBegin");
	p[11] = GetProcAddress(hL, "glBindTexture");
	p[12] = GetProcAddress(hL, "glBitmap");
	p[13] = GetProcAddress(hL, "glBlendFunc");
	p[14] = GetProcAddress(hL, "glCallList");
	p[15] = GetProcAddress(hL, "glCallLists");
	p[16] = GetProcAddress(hL, "glClear");
	p[17] = GetProcAddress(hL, "glClearAccum");
	p[18] = GetProcAddress(hL, "glClearColor");
	p[19] = GetProcAddress(hL, "glClearDepth");
	p[20] = GetProcAddress(hL, "glClearIndex");
	p[21] = GetProcAddress(hL, "glClearStencil");
	p[22] = GetProcAddress(hL, "glClipPlane");
	p[23] = GetProcAddress(hL, "glColor3b");
	p[24] = GetProcAddress(hL, "glColor3bv");
	p[25] = GetProcAddress(hL, "glColor3d");
	p[26] = GetProcAddress(hL, "glColor3dv");
	p[27] = GetProcAddress(hL, "glColor3f");
	p[28] = GetProcAddress(hL, "glColor3fv");
	p[29] = GetProcAddress(hL, "glColor3i");
	p[30] = GetProcAddress(hL, "glColor3iv");
	p[31] = GetProcAddress(hL, "glColor3s");
	p[32] = GetProcAddress(hL, "glColor3sv");
	p[33] = GetProcAddress(hL, "glColor3ub");
	p[34] = GetProcAddress(hL, "glColor3ubv");
	p[35] = GetProcAddress(hL, "glColor3ui");
	p[36] = GetProcAddress(hL, "glColor3uiv");
	p[37] = GetProcAddress(hL, "glColor3us");
	p[38] = GetProcAddress(hL, "glColor3usv");
	p[39] = GetProcAddress(hL, "glColor4b");
	p[40] = GetProcAddress(hL, "glColor4bv");
	p[41] = GetProcAddress(hL, "glColor4d");
	p[42] = GetProcAddress(hL, "glColor4dv");
	p[43] = GetProcAddress(hL, "glColor4f");
	p[44] = GetProcAddress(hL, "glColor4fv");
	p[45] = GetProcAddress(hL, "glColor4i");
	p[46] = GetProcAddress(hL, "glColor4iv");
	p[47] = GetProcAddress(hL, "glColor4s");
	p[48] = GetProcAddress(hL, "glColor4sv");
	p[49] = GetProcAddress(hL, "glColor4ub");
	p[50] = GetProcAddress(hL, "glColor4ubv");
	p[51] = GetProcAddress(hL, "glColor4ui");
	p[52] = GetProcAddress(hL, "glColor4uiv");
	p[53] = GetProcAddress(hL, "glColor4us");
	p[54] = GetProcAddress(hL, "glColor4usv");
	p[55] = GetProcAddress(hL, "glColorMask");
	p[56] = GetProcAddress(hL, "glColorMaterial");
	p[57] = GetProcAddress(hL, "glColorPointer");
	p[58] = GetProcAddress(hL, "glCopyPixels");
	p[59] = GetProcAddress(hL, "glCopyTexImage1D");
	p[60] = GetProcAddress(hL, "glCopyTexImage2D");
	p[61] = GetProcAddress(hL, "glCopyTexSubImage1D");
	p[62] = GetProcAddress(hL, "glCopyTexSubImage2D");
	p[63] = GetProcAddress(hL, "glCullFace");
	p[64] = GetProcAddress(hL, "glDebugEntry");
	p[65] = GetProcAddress(hL, "glDeleteLists");
	p[66] = GetProcAddress(hL, "glDeleteTextures");
	p[67] = GetProcAddress(hL, "glDepthFunc");
	p[68] = GetProcAddress(hL, "glDepthMask");
	p[69] = GetProcAddress(hL, "glDepthRange");
	p[70] = GetProcAddress(hL, "glDisable");
	p[71] = GetProcAddress(hL, "glDisableClientState");
	p[72] = GetProcAddress(hL, "glDrawArrays");
	p[73] = GetProcAddress(hL, "glDrawBuffer");
	p[74] = GetProcAddress(hL, "glDrawElements");
	p[75] = GetProcAddress(hL, "glDrawPixels");
	p[76] = GetProcAddress(hL, "glEdgeFlag");
	p[77] = GetProcAddress(hL, "glEdgeFlagPointer");
	p[78] = GetProcAddress(hL, "glEdgeFlagv");
	p[79] = GetProcAddress(hL, "glEnable");
	p[80] = GetProcAddress(hL, "glEnableClientState");
	p[81] = GetProcAddress(hL, "glEnd");
	p[82] = GetProcAddress(hL, "glEndList");
	p[83] = GetProcAddress(hL, "glEvalCoord1d");
	p[84] = GetProcAddress(hL, "glEvalCoord1dv");
	p[85] = GetProcAddress(hL, "glEvalCoord1f");
	p[86] = GetProcAddress(hL, "glEvalCoord1fv");
	p[87] = GetProcAddress(hL, "glEvalCoord2d");
	p[88] = GetProcAddress(hL, "glEvalCoord2dv");
	p[89] = GetProcAddress(hL, "glEvalCoord2f");
	p[90] = GetProcAddress(hL, "glEvalCoord2fv");
	p[91] = GetProcAddress(hL, "glEvalMesh1");
	p[92] = GetProcAddress(hL, "glEvalMesh2");
	p[93] = GetProcAddress(hL, "glEvalPoint1");
	p[94] = GetProcAddress(hL, "glEvalPoint2");
	p[95] = GetProcAddress(hL, "glFeedbackBuffer");
	p[96] = GetProcAddress(hL, "glFinish");
	p[97] = GetProcAddress(hL, "glFlush");
	p[98] = GetProcAddress(hL, "glFogf");
	orig_glFogf = (PFNGLFOGFPROC)p[98];
	p[99] = GetProcAddress(hL, "glFogfv");
	orig_glFogfv = (PFNGLFOGFVPROC)p[99];
	p[100] = GetProcAddress(hL, "glFogi");
	orig_glFogi = (PFNGLFOGIPROC)p[100];
	p[101] = GetProcAddress(hL, "glFogiv");
	orig_glFogiv = (PFNGLFOGIVPROC)p[101];
	p[102] = GetProcAddress(hL, "glFrontFace");
	p[103] = GetProcAddress(hL, "glFrustum");
	p[104] = GetProcAddress(hL, "glGenLists");
	p[105] = GetProcAddress(hL, "glGenTextures");
	p[106] = GetProcAddress(hL, "glGetBooleanv");
	p[107] = GetProcAddress(hL, "glGetClipPlane");
	p[108] = GetProcAddress(hL, "glGetDoublev");
	p[109] = GetProcAddress(hL, "glGetError");
	p[110] = GetProcAddress(hL, "glGetFloatv");
	p[111] = GetProcAddress(hL, "glGetIntegerv");
	orig_glGetIntegerv = (PFNGLGETINTEGERV)p[111];
	p[112] = GetProcAddress(hL, "glGetLightfv");
	p[113] = GetProcAddress(hL, "glGetLightiv");
	p[114] = GetProcAddress(hL, "glGetMapdv");
	p[115] = GetProcAddress(hL, "glGetMapfv");
	p[116] = GetProcAddress(hL, "glGetMapiv");
	p[117] = GetProcAddress(hL, "glGetMaterialfv");
	p[118] = GetProcAddress(hL, "glGetMaterialiv");
	p[119] = GetProcAddress(hL, "glGetPixelMapfv");
	p[120] = GetProcAddress(hL, "glGetPixelMapuiv");
	p[121] = GetProcAddress(hL, "glGetPixelMapusv");
	p[122] = GetProcAddress(hL, "glGetPointerv");
	p[123] = GetProcAddress(hL, "glGetPolygonStipple");
	p[124] = GetProcAddress(hL, "glGetString");
	p[125] = GetProcAddress(hL, "glGetTexEnvfv");
	p[126] = GetProcAddress(hL, "glGetTexEnviv");
	p[127] = GetProcAddress(hL, "glGetTexGendv");
	p[128] = GetProcAddress(hL, "glGetTexGenfv");
	p[129] = GetProcAddress(hL, "glGetTexGeniv");
	p[130] = GetProcAddress(hL, "glGetTexImage");
	p[131] = GetProcAddress(hL, "glGetTexLevelParameterfv");
	p[132] = GetProcAddress(hL, "glGetTexLevelParameteriv");
	p[133] = GetProcAddress(hL, "glGetTexParameterfv");
	p[134] = GetProcAddress(hL, "glGetTexParameteriv");
	p[135] = GetProcAddress(hL, "glHint");
	p[136] = GetProcAddress(hL, "glIndexMask");
	p[137] = GetProcAddress(hL, "glIndexPointer");
	p[138] = GetProcAddress(hL, "glIndexd");
	p[139] = GetProcAddress(hL, "glIndexdv");
	p[140] = GetProcAddress(hL, "glIndexf");
	p[141] = GetProcAddress(hL, "glIndexfv");
	p[142] = GetProcAddress(hL, "glIndexi");
	p[143] = GetProcAddress(hL, "glIndexiv");
	p[144] = GetProcAddress(hL, "glIndexs");
	p[145] = GetProcAddress(hL, "glIndexsv");
	p[146] = GetProcAddress(hL, "glIndexub");
	p[147] = GetProcAddress(hL, "glIndexubv");
	p[148] = GetProcAddress(hL, "glInitNames");
	p[149] = GetProcAddress(hL, "glInterleavedArrays");
	p[150] = GetProcAddress(hL, "glIsEnabled");
	p[151] = GetProcAddress(hL, "glIsList");
	p[152] = GetProcAddress(hL, "glIsTexture");
	p[153] = GetProcAddress(hL, "glLightModelf");
	p[154] = GetProcAddress(hL, "glLightModelfv");
	p[155] = GetProcAddress(hL, "glLightModeli");
	p[156] = GetProcAddress(hL, "glLightModeliv");
	p[157] = GetProcAddress(hL, "glLightf");
	p[158] = GetProcAddress(hL, "glLightfv");
	p[159] = GetProcAddress(hL, "glLighti");
	p[160] = GetProcAddress(hL, "glLightiv");
	p[161] = GetProcAddress(hL, "glLineStipple");
	p[162] = GetProcAddress(hL, "glLineWidth");
	p[163] = GetProcAddress(hL, "glListBase");
	p[164] = GetProcAddress(hL, "glLoadIdentity");
	p[165] = GetProcAddress(hL, "glLoadMatrixd");
	p[166] = GetProcAddress(hL, "glLoadMatrixf");
	p[167] = GetProcAddress(hL, "glLoadName");
	p[168] = GetProcAddress(hL, "glLogicOp");
	p[169] = GetProcAddress(hL, "glMap1d");
	p[170] = GetProcAddress(hL, "glMap1f");
	p[171] = GetProcAddress(hL, "glMap2d");
	p[172] = GetProcAddress(hL, "glMap2f");
	p[173] = GetProcAddress(hL, "glMapGrid1d");
	p[174] = GetProcAddress(hL, "glMapGrid1f");
	p[175] = GetProcAddress(hL, "glMapGrid2d");
	p[176] = GetProcAddress(hL, "glMapGrid2f");
	p[177] = GetProcAddress(hL, "glMaterialf");
	p[178] = GetProcAddress(hL, "glMaterialfv");
	p[179] = GetProcAddress(hL, "glMateriali");
	p[180] = GetProcAddress(hL, "glMaterialiv");
	p[181] = GetProcAddress(hL, "glMatrixMode");
	p[182] = GetProcAddress(hL, "glMultMatrixd");
	p[183] = GetProcAddress(hL, "glMultMatrixf");
	p[184] = GetProcAddress(hL, "glNewList");
	p[185] = GetProcAddress(hL, "glNormal3b");
	p[186] = GetProcAddress(hL, "glNormal3bv");
	p[187] = GetProcAddress(hL, "glNormal3d");
	p[188] = GetProcAddress(hL, "glNormal3dv");
	p[189] = GetProcAddress(hL, "glNormal3f");
	p[190] = GetProcAddress(hL, "glNormal3fv");
	p[191] = GetProcAddress(hL, "glNormal3i");
	p[192] = GetProcAddress(hL, "glNormal3iv");
	p[193] = GetProcAddress(hL, "glNormal3s");
	p[194] = GetProcAddress(hL, "glNormal3sv");
	p[195] = GetProcAddress(hL, "glNormalPointer");
	p[196] = GetProcAddress(hL, "glOrtho");
	p[197] = GetProcAddress(hL, "glPassThrough");
	p[198] = GetProcAddress(hL, "glPixelMapfv");
	p[199] = GetProcAddress(hL, "glPixelMapuiv");
	p[200] = GetProcAddress(hL, "glPixelMapusv");
	p[201] = GetProcAddress(hL, "glPixelStoref");
	p[202] = GetProcAddress(hL, "glPixelStorei");
	p[203] = GetProcAddress(hL, "glPixelTransferf");
	p[204] = GetProcAddress(hL, "glPixelTransferi");
	p[205] = GetProcAddress(hL, "glPixelZoom");
	p[206] = GetProcAddress(hL, "glPointSize");
	p[207] = GetProcAddress(hL, "glPolygonMode");
	p[208] = GetProcAddress(hL, "glPolygonOffset");
	p[209] = GetProcAddress(hL, "glPolygonStipple");
	p[210] = GetProcAddress(hL, "glPopAttrib");
	p[211] = GetProcAddress(hL, "glPopClientAttrib");
	p[212] = GetProcAddress(hL, "glPopMatrix");
	p[213] = GetProcAddress(hL, "glPopName");
	p[214] = GetProcAddress(hL, "glPrioritizeTextures");
	p[215] = GetProcAddress(hL, "glPushAttrib");
	p[216] = GetProcAddress(hL, "glPushClientAttrib");
	p[217] = GetProcAddress(hL, "glPushMatrix");
	p[218] = GetProcAddress(hL, "glPushName");
	p[219] = GetProcAddress(hL, "glRasterPos2d");
	p[220] = GetProcAddress(hL, "glRasterPos2dv");
	p[221] = GetProcAddress(hL, "glRasterPos2f");
	p[222] = GetProcAddress(hL, "glRasterPos2fv");
	p[223] = GetProcAddress(hL, "glRasterPos2i");
	p[224] = GetProcAddress(hL, "glRasterPos2iv");
	p[225] = GetProcAddress(hL, "glRasterPos2s");
	p[226] = GetProcAddress(hL, "glRasterPos2sv");
	p[227] = GetProcAddress(hL, "glRasterPos3d");
	p[228] = GetProcAddress(hL, "glRasterPos3dv");
	p[229] = GetProcAddress(hL, "glRasterPos3f");
	p[230] = GetProcAddress(hL, "glRasterPos3fv");
	p[231] = GetProcAddress(hL, "glRasterPos3i");
	p[232] = GetProcAddress(hL, "glRasterPos3iv");
	p[233] = GetProcAddress(hL, "glRasterPos3s");
	p[234] = GetProcAddress(hL, "glRasterPos3sv");
	p[235] = GetProcAddress(hL, "glRasterPos4d");
	p[236] = GetProcAddress(hL, "glRasterPos4dv");
	p[237] = GetProcAddress(hL, "glRasterPos4f");
	p[238] = GetProcAddress(hL, "glRasterPos4fv");
	p[239] = GetProcAddress(hL, "glRasterPos4i");
	p[240] = GetProcAddress(hL, "glRasterPos4iv");
	p[241] = GetProcAddress(hL, "glRasterPos4s");
	p[242] = GetProcAddress(hL, "glRasterPos4sv");
	p[243] = GetProcAddress(hL, "glReadBuffer");
	p[244] = GetProcAddress(hL, "glReadPixels");
	p[245] = GetProcAddress(hL, "glRectd");
	p[246] = GetProcAddress(hL, "glRectdv");
	p[247] = GetProcAddress(hL, "glRectf");
	p[248] = GetProcAddress(hL, "glRectfv");
	p[249] = GetProcAddress(hL, "glRecti");
	p[250] = GetProcAddress(hL, "glRectiv");
	p[251] = GetProcAddress(hL, "glRects");
	p[252] = GetProcAddress(hL, "glRectsv");
	p[253] = GetProcAddress(hL, "glRenderMode");
	p[254] = GetProcAddress(hL, "glRotated");
	p[255] = GetProcAddress(hL, "glRotatef");
	p[256] = GetProcAddress(hL, "glScaled");
	p[257] = GetProcAddress(hL, "glScalef");
	p[258] = GetProcAddress(hL, "glScissor");
	p[259] = GetProcAddress(hL, "glSelectBuffer");
	p[260] = GetProcAddress(hL, "glShadeModel");
	p[261] = GetProcAddress(hL, "glStencilFunc");
	p[262] = GetProcAddress(hL, "glStencilMask");
	p[263] = GetProcAddress(hL, "glStencilOp");
	p[264] = GetProcAddress(hL, "glTexCoord1d");
	p[265] = GetProcAddress(hL, "glTexCoord1dv");
	p[266] = GetProcAddress(hL, "glTexCoord1f");
	p[267] = GetProcAddress(hL, "glTexCoord1fv");
	p[268] = GetProcAddress(hL, "glTexCoord1i");
	p[269] = GetProcAddress(hL, "glTexCoord1iv");
	p[270] = GetProcAddress(hL, "glTexCoord1s");
	p[271] = GetProcAddress(hL, "glTexCoord1sv");
	p[272] = GetProcAddress(hL, "glTexCoord2d");
	p[273] = GetProcAddress(hL, "glTexCoord2dv");
	p[274] = GetProcAddress(hL, "glTexCoord2f");
	p[275] = GetProcAddress(hL, "glTexCoord2fv");
	p[276] = GetProcAddress(hL, "glTexCoord2i");
	p[277] = GetProcAddress(hL, "glTexCoord2iv");
	p[278] = GetProcAddress(hL, "glTexCoord2s");
	p[279] = GetProcAddress(hL, "glTexCoord2sv");
	p[280] = GetProcAddress(hL, "glTexCoord3d");
	p[281] = GetProcAddress(hL, "glTexCoord3dv");
	p[282] = GetProcAddress(hL, "glTexCoord3f");
	p[283] = GetProcAddress(hL, "glTexCoord3fv");
	p[284] = GetProcAddress(hL, "glTexCoord3i");
	p[285] = GetProcAddress(hL, "glTexCoord3iv");
	p[286] = GetProcAddress(hL, "glTexCoord3s");
	p[287] = GetProcAddress(hL, "glTexCoord3sv");
	p[288] = GetProcAddress(hL, "glTexCoord4d");
	p[289] = GetProcAddress(hL, "glTexCoord4dv");
	p[290] = GetProcAddress(hL, "glTexCoord4f");
	p[291] = GetProcAddress(hL, "glTexCoord4fv");
	p[292] = GetProcAddress(hL, "glTexCoord4i");
	p[293] = GetProcAddress(hL, "glTexCoord4iv");
	p[294] = GetProcAddress(hL, "glTexCoord4s");
	p[295] = GetProcAddress(hL, "glTexCoord4sv");
	p[296] = GetProcAddress(hL, "glTexCoordPointer");
	p[297] = GetProcAddress(hL, "glTexEnvf");
	p[298] = GetProcAddress(hL, "glTexEnvfv");
	p[299] = GetProcAddress(hL, "glTexEnvi");
	p[300] = GetProcAddress(hL, "glTexEnviv");
	p[301] = GetProcAddress(hL, "glTexGend");
	p[302] = GetProcAddress(hL, "glTexGendv");
	p[303] = GetProcAddress(hL, "glTexGenf");
	p[304] = GetProcAddress(hL, "glTexGenfv");
	p[305] = GetProcAddress(hL, "glTexGeni");
	p[306] = GetProcAddress(hL, "glTexGeniv");
	p[307] = GetProcAddress(hL, "glTexImage1D");
	p[308] = GetProcAddress(hL, "glTexImage2D");
	p[309] = GetProcAddress(hL, "glTexParameterf");
	p[310] = GetProcAddress(hL, "glTexParameterfv");
	p[311] = GetProcAddress(hL, "glTexParameteri");
	p[312] = GetProcAddress(hL, "glTexParameteriv");
	p[313] = GetProcAddress(hL, "glTexSubImage1D");
	p[314] = GetProcAddress(hL, "glTexSubImage2D");
	p[315] = GetProcAddress(hL, "glTranslated");
	p[316] = GetProcAddress(hL, "glTranslatef");
	p[317] = GetProcAddress(hL, "glVertex2d");
	p[318] = GetProcAddress(hL, "glVertex2dv");
	p[319] = GetProcAddress(hL, "glVertex2f");
	p[320] = GetProcAddress(hL, "glVertex2fv");
	p[321] = GetProcAddress(hL, "glVertex2i");
	p[322] = GetProcAddress(hL, "glVertex2iv");
	p[323] = GetProcAddress(hL, "glVertex2s");
	p[324] = GetProcAddress(hL, "glVertex2sv");
	p[325] = GetProcAddress(hL, "glVertex3d");
	p[326] = GetProcAddress(hL, "glVertex3dv");
	p[327] = GetProcAddress(hL, "glVertex3f");
	p[328] = GetProcAddress(hL, "glVertex3fv");
	p[329] = GetProcAddress(hL, "glVertex3i");
	p[330] = GetProcAddress(hL, "glVertex3iv");
	p[331] = GetProcAddress(hL, "glVertex3s");
	p[332] = GetProcAddress(hL, "glVertex3sv");
	p[333] = GetProcAddress(hL, "glVertex4d");
	p[334] = GetProcAddress(hL, "glVertex4dv");
	p[335] = GetProcAddress(hL, "glVertex4f");
	p[336] = GetProcAddress(hL, "glVertex4fv");
	p[337] = GetProcAddress(hL, "glVertex4i");
	p[338] = GetProcAddress(hL, "glVertex4iv");
	p[339] = GetProcAddress(hL, "glVertex4s");
	p[340] = GetProcAddress(hL, "glVertex4sv");
	p[341] = GetProcAddress(hL, "glVertexPointer");
	p[342] = GetProcAddress(hL, "glViewport");
	p[343] = GetProcAddress(hL, "wglChoosePixelFormat");
	p[344] = GetProcAddress(hL, "wglCopyContext");
	p[345] = GetProcAddress(hL, "wglCreateContext");
	p[346] = GetProcAddress(hL, "wglCreateLayerContext");
	p[347] = GetProcAddress(hL, "wglDeleteContext");
	p[348] = GetProcAddress(hL, "wglDescribeLayerPlane");
	p[349] = GetProcAddress(hL, "wglDescribePixelFormat");
	p[350] = GetProcAddress(hL, "wglGetCurrentContext");
	p[351] = GetProcAddress(hL, "wglGetCurrentDC");
	p[352] = GetProcAddress(hL, "wglGetDefaultProcAddress");
	p[353] = GetProcAddress(hL, "wglGetLayerPaletteEntries");
	p[354] = GetProcAddress(hL, "wglGetPixelFormat");
	p[355] = GetProcAddress(hL, "wglGetProcAddress");
	orig_wglGetProcAddress = (PFNWGLGETPROCADDRESS)p[355];
	p[356] = GetProcAddress(hL, "wglMakeCurrent");
	p[357] = GetProcAddress(hL, "wglRealizeLayerPalette");
	p[358] = GetProcAddress(hL, "wglSetLayerPaletteEntries");
	p[359] = GetProcAddress(hL, "wglSetPixelFormat");
	p[360] = GetProcAddress(hL, "wglShareLists");
	p[361] = GetProcAddress(hL, "wglSwapBuffers");
	p[362] = GetProcAddress(hL, "wglSwapLayerBuffers");
	p[363] = GetProcAddress(hL, "wglSwapMultipleBuffers");
	p[364] = GetProcAddress(hL, "wglUseFontBitmapsA");
	p[365] = GetProcAddress(hL, "wglUseFontBitmapsW");
	p[366] = GetProcAddress(hL, "wglUseFontOutlinesA");
	p[367] = GetProcAddress(hL, "wglUseFontOutlinesW");
}


// GlmfBeginGlsBlock
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__0__() { __asm__("jmp *(_p+0)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__0__() { __asm__("jmp *(_p+0)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__0__()
{
	__asm
	{
		jmp p[0 * 4];
	}
}
#endif
#endif

// GlmfCloseMetaFile
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__1__() { __asm__("jmp *(_p+4)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__1__() { __asm__("jmp *(_p+4)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__1__()
{
	__asm
	{
		jmp p[1 * 4];
	}
}
#endif
#endif

// GlmfEndGlsBlock
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__2__() { __asm__("jmp *(_p+8)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__2__() { __asm__("jmp *(_p+8)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__2__()
{
	__asm
	{
		jmp p[2 * 4];
	}
}
#endif
#endif

// GlmfEndPlayback
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__3__() { __asm__("jmp *(_p+12)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__3__() { __asm__("jmp *(_p+12)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__3__()
{
	__asm
	{
		jmp p[3 * 4];
	}
}
#endif
#endif

// GlmfInitPlayback
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__4__() { __asm__("jmp *(_p+16)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__4__() { __asm__("jmp *(_p+16)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__4__()
{
	__asm
	{
		jmp p[4 * 4];
	}
}
#endif
#endif

// GlmfPlayGlsRecord
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__5__() { __asm__("jmp *(_p+20)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__5__() { __asm__("jmp *(_p+20)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__5__()
{
	__asm
	{
		jmp p[5 * 4];
	}
}
#endif
#endif

// glAccum
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__6__() { __asm__("jmp *(_p+24)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__6__() { __asm__("jmp *(_p+24)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__6__()
{
	__asm
	{
		jmp p[6 * 4];
	}
}
#endif
#endif

// glAlphaFunc
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__7__() { __asm__("jmp *(_p+28)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__7__() { __asm__("jmp *(_p+28)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__7__()
{
	__asm
	{
		jmp p[7 * 4];
	}
}
#endif
#endif

// glAreTexturesResident
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__8__() { __asm__("jmp *(_p+32)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__8__() { __asm__("jmp *(_p+32)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__8__()
{
	__asm
	{
		jmp p[8 * 4];
	}
}
#endif
#endif

// glArrayElement
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__9__() { __asm__("jmp *(_p+36)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__9__() { __asm__("jmp *(_p+36)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__9__()
{
	__asm
	{
		jmp p[9 * 4];
	}
}
#endif
#endif

// glBegin
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__10__() { __asm__("jmp *(_p+40)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__10__() { __asm__("jmp *(_p+40)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__10__()
{
	__asm
	{
		jmp p[10 * 4];
	}
}
#endif
#endif

// glBindTexture
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__11__() { __asm__("jmp *(_p+44)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__11__() { __asm__("jmp *(_p+44)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__11__()
{
	__asm
	{
		jmp p[11 * 4];
	}
}
#endif
#endif

// glBitmap
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__12__() { __asm__("jmp *(_p+48)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__12__() { __asm__("jmp *(_p+48)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__12__()
{
	__asm
	{
		jmp p[12 * 4];
	}
}
#endif
#endif

// glBlendFunc
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__13__() { __asm__("jmp *(_p+52)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__13__() { __asm__("jmp *(_p+52)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__13__()
{
	__asm
	{
		jmp p[13 * 4];
	}
}
#endif
#endif

// glCallList
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__14__() { __asm__("jmp *(_p+56)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__14__() { __asm__("jmp *(_p+56)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__14__()
{
	__asm
	{
		jmp p[14 * 4];
	}
}
#endif
#endif

// glCallLists
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__15__() { __asm__("jmp *(_p+60)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__15__() { __asm__("jmp *(_p+60)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__15__()
{
	__asm
	{
		jmp p[15 * 4];
	}
}
#endif
#endif

// glClear
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__16__() { __asm__("jmp *(_p+64)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__16__() { __asm__("jmp *(_p+64)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__16__()
{
	__asm
	{
		jmp p[16 * 4];
	}
}
#endif
#endif

// glClearAccum
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__17__() { __asm__("jmp *(_p+68)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__17__() { __asm__("jmp *(_p+68)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__17__()
{
	__asm
	{
		jmp p[17 * 4];
	}
}
#endif
#endif

// glClearColor
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__18__() { __asm__("jmp *(_p+72)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__18__() { __asm__("jmp *(_p+72)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__18__()
{
	__asm
	{
		jmp p[18 * 4];
	}
}
#endif
#endif

// glClearDepth
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__19__() { __asm__("jmp *(_p+76)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__19__() { __asm__("jmp *(_p+76)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__19__()
{
	__asm
	{
		jmp p[19 * 4];
	}
}
#endif
#endif

// glClearIndex
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__20__() { __asm__("jmp *(_p+80)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__20__() { __asm__("jmp *(_p+80)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__20__()
{
	__asm
	{
		jmp p[20 * 4];
	}
}
#endif
#endif

// glClearStencil
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__21__() { __asm__("jmp *(_p+84)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__21__() { __asm__("jmp *(_p+84)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__21__()
{
	__asm
	{
		jmp p[21 * 4];
	}
}
#endif
#endif

// glClipPlane
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__22__() { __asm__("jmp *(_p+88)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__22__() { __asm__("jmp *(_p+88)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__22__()
{
	__asm
	{
		jmp p[22 * 4];
	}
}
#endif
#endif

// glColor3b
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__23__() { __asm__("jmp *(_p+92)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__23__() { __asm__("jmp *(_p+92)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__23__()
{
	__asm
	{
		jmp p[23 * 4];
	}
}
#endif
#endif

// glColor3bv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__24__() { __asm__("jmp *(_p+96)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__24__() { __asm__("jmp *(_p+96)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__24__()
{
	__asm
	{
		jmp p[24 * 4];
	}
}
#endif
#endif

// glColor3d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__25__() { __asm__("jmp *(_p+100)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__25__() { __asm__("jmp *(_p+100)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__25__()
{
	__asm
	{
		jmp p[25 * 4];
	}
}
#endif
#endif

// glColor3dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__26__() { __asm__("jmp *(_p+104)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__26__() { __asm__("jmp *(_p+104)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__26__()
{
	__asm
	{
		jmp p[26 * 4];
	}
}
#endif
#endif

// glColor3f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__27__() { __asm__("jmp *(_p+108)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__27__() { __asm__("jmp *(_p+108)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__27__()
{
	__asm
	{
		jmp p[27 * 4];
	}
}
#endif
#endif

// glColor3fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__28__() { __asm__("jmp *(_p+112)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__28__() { __asm__("jmp *(_p+112)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__28__()
{
	__asm
	{
		jmp p[28 * 4];
	}
}
#endif
#endif

// glColor3i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__29__() { __asm__("jmp *(_p+116)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__29__() { __asm__("jmp *(_p+116)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__29__()
{
	__asm
	{
		jmp p[29 * 4];
	}
}
#endif
#endif

// glColor3iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__30__() { __asm__("jmp *(_p+120)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__30__() { __asm__("jmp *(_p+120)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__30__()
{
	__asm
	{
		jmp p[30 * 4];
	}
}
#endif
#endif

// glColor3s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__31__() { __asm__("jmp *(_p+124)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__31__() { __asm__("jmp *(_p+124)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__31__()
{
	__asm
	{
		jmp p[31 * 4];
	}
}
#endif
#endif

// glColor3sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__32__() { __asm__("jmp *(_p+128)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__32__() { __asm__("jmp *(_p+128)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__32__()
{
	__asm
	{
		jmp p[32 * 4];
	}
}
#endif
#endif

// glColor3ub
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__33__() { __asm__("jmp *(_p+132)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__33__() { __asm__("jmp *(_p+132)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__33__()
{
	__asm
	{
		jmp p[33 * 4];
	}
}
#endif
#endif

// glColor3ubv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__34__() { __asm__("jmp *(_p+136)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__34__() { __asm__("jmp *(_p+136)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__34__()
{
	__asm
	{
		jmp p[34 * 4];
	}
}
#endif
#endif

// glColor3ui
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__35__() { __asm__("jmp *(_p+140)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__35__() { __asm__("jmp *(_p+140)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__35__()
{
	__asm
	{
		jmp p[35 * 4];
	}
}
#endif
#endif

// glColor3uiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__36__() { __asm__("jmp *(_p+144)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__36__() { __asm__("jmp *(_p+144)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__36__()
{
	__asm
	{
		jmp p[36 * 4];
	}
}
#endif
#endif

// glColor3us
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__37__() { __asm__("jmp *(_p+148)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__37__() { __asm__("jmp *(_p+148)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__37__()
{
	__asm
	{
		jmp p[37 * 4];
	}
}
#endif
#endif

// glColor3usv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__38__() { __asm__("jmp *(_p+152)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__38__() { __asm__("jmp *(_p+152)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__38__()
{
	__asm
	{
		jmp p[38 * 4];
	}
}
#endif
#endif

// glColor4b
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__39__() { __asm__("jmp *(_p+156)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__39__() { __asm__("jmp *(_p+156)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__39__()
{
	__asm
	{
		jmp p[39 * 4];
	}
}
#endif
#endif

// glColor4bv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__40__() { __asm__("jmp *(_p+160)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__40__() { __asm__("jmp *(_p+160)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__40__()
{
	__asm
	{
		jmp p[40 * 4];
	}
}
#endif
#endif

// glColor4d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__41__() { __asm__("jmp *(_p+164)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__41__() { __asm__("jmp *(_p+164)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__41__()
{
	__asm
	{
		jmp p[41 * 4];
	}
}
#endif
#endif

// glColor4dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__42__() { __asm__("jmp *(_p+168)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__42__() { __asm__("jmp *(_p+168)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__42__()
{
	__asm
	{
		jmp p[42 * 4];
	}
}
#endif
#endif

// glColor4f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__43__() { __asm__("jmp *(_p+172)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__43__() { __asm__("jmp *(_p+172)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__43__()
{
	__asm
	{
		jmp p[43 * 4];
	}
}
#endif
#endif

// glColor4fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__44__() { __asm__("jmp *(_p+176)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__44__() { __asm__("jmp *(_p+176)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__44__()
{
	__asm
	{
		jmp p[44 * 4];
	}
}
#endif
#endif

// glColor4i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__45__() { __asm__("jmp *(_p+180)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__45__() { __asm__("jmp *(_p+180)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__45__()
{
	__asm
	{
		jmp p[45 * 4];
	}
}
#endif
#endif

// glColor4iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__46__() { __asm__("jmp *(_p+184)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__46__() { __asm__("jmp *(_p+184)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__46__()
{
	__asm
	{
		jmp p[46 * 4];
	}
}
#endif
#endif

// glColor4s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__47__() { __asm__("jmp *(_p+188)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__47__() { __asm__("jmp *(_p+188)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__47__()
{
	__asm
	{
		jmp p[47 * 4];
	}
}
#endif
#endif

// glColor4sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__48__() { __asm__("jmp *(_p+192)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__48__() { __asm__("jmp *(_p+192)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__48__()
{
	__asm
	{
		jmp p[48 * 4];
	}
}
#endif
#endif

// glColor4ub
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__49__() { __asm__("jmp *(_p+196)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__49__() { __asm__("jmp *(_p+196)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__49__()
{
	__asm
	{
		jmp p[49 * 4];
	}
}
#endif
#endif

// glColor4ubv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__50__() { __asm__("jmp *(_p+200)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__50__() { __asm__("jmp *(_p+200)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__50__()
{
	__asm
	{
		jmp p[50 * 4];
	}
}
#endif
#endif

// glColor4ui
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__51__() { __asm__("jmp *(_p+204)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__51__() { __asm__("jmp *(_p+204)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__51__()
{
	__asm
	{
		jmp p[51 * 4];
	}
}
#endif
#endif

// glColor4uiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__52__() { __asm__("jmp *(_p+208)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__52__() { __asm__("jmp *(_p+208)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__52__()
{
	__asm
	{
		jmp p[52 * 4];
	}
}
#endif
#endif

// glColor4us
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__53__() { __asm__("jmp *(_p+212)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__53__() { __asm__("jmp *(_p+212)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__53__()
{
	__asm
	{
		jmp p[53 * 4];
	}
}
#endif
#endif

// glColor4usv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__54__() { __asm__("jmp *(_p+216)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__54__() { __asm__("jmp *(_p+216)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__54__()
{
	__asm
	{
		jmp p[54 * 4];
	}
}
#endif
#endif

// glColorMask
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__55__() { __asm__("jmp *(_p+220)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__55__() { __asm__("jmp *(_p+220)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__55__()
{
	__asm
	{
		jmp p[55 * 4];
	}
}
#endif
#endif

// glColorMaterial
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__56__() { __asm__("jmp *(_p+224)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__56__() { __asm__("jmp *(_p+224)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__56__()
{
	__asm
	{
		jmp p[56 * 4];
	}
}
#endif
#endif

// glColorPointer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__57__() { __asm__("jmp *(_p+228)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__57__() { __asm__("jmp *(_p+228)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__57__()
{
	__asm
	{
		jmp p[57 * 4];
	}
}
#endif
#endif

// glCopyPixels
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__58__() { __asm__("jmp *(_p+232)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__58__() { __asm__("jmp *(_p+232)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__58__()
{
	__asm
	{
		jmp p[58 * 4];
	}
}
#endif
#endif

// glCopyTexImage1D
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__59__() { __asm__("jmp *(_p+236)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__59__() { __asm__("jmp *(_p+236)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__59__()
{
	__asm
	{
		jmp p[59 * 4];
	}
}
#endif
#endif

// glCopyTexImage2D
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__60__() { __asm__("jmp *(_p+240)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__60__() { __asm__("jmp *(_p+240)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__60__()
{
	__asm
	{
		jmp p[60 * 4];
	}
}
#endif
#endif

// glCopyTexSubImage1D
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__61__() { __asm__("jmp *(_p+244)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__61__() { __asm__("jmp *(_p+244)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__61__()
{
	__asm
	{
		jmp p[61 * 4];
	}
}
#endif
#endif

// glCopyTexSubImage2D
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__62__() { __asm__("jmp *(_p+248)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__62__() { __asm__("jmp *(_p+248)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__62__()
{
	__asm
	{
		jmp p[62 * 4];
	}
}
#endif
#endif

// glCullFace
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__63__() { __asm__("jmp *(_p+252)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__63__() { __asm__("jmp *(_p+252)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__63__()
{
	__asm
	{
		jmp p[63 * 4];
	}
}
#endif
#endif

// glDebugEntry
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__64__() { __asm__("jmp *(_p+256)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__64__() { __asm__("jmp *(_p+256)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__64__()
{
	__asm
	{
		jmp p[64 * 4];
	}
}
#endif
#endif

// glDeleteLists
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__65__() { __asm__("jmp *(_p+260)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__65__() { __asm__("jmp *(_p+260)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__65__()
{
	__asm
	{
		jmp p[65 * 4];
	}
}
#endif
#endif

// glDeleteTextures
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__66__() { __asm__("jmp *(_p+264)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__66__() { __asm__("jmp *(_p+264)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__66__()
{
	__asm
	{
		jmp p[66 * 4];
	}
}
#endif
#endif

// glDepthFunc
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__67__() { __asm__("jmp *(_p+268)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__67__() { __asm__("jmp *(_p+268)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__67__()
{
	__asm
	{
		jmp p[67 * 4];
	}
}
#endif
#endif

// glDepthMask
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__68__() { __asm__("jmp *(_p+272)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__68__() { __asm__("jmp *(_p+272)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__68__()
{
	__asm
	{
		jmp p[68 * 4];
	}
}
#endif
#endif

// glDepthRange
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__69__() { __asm__("jmp *(_p+276)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__69__() { __asm__("jmp *(_p+276)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__69__()
{
	__asm
	{
		jmp p[69 * 4];
	}
}
#endif
#endif

// glDisable
/*#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__70__() { __asm__("jmp *(_p+280)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__70__() { __asm__("jmp *(_p+280)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__70__()
{
	__asm
	{
		jmp p[70 * 4];
	}
}
#endif
#endif*/


typedef void (__stdcall *PFNGLDISABLE)( GLenum );
extern "C" void __stdcall __E__70__( GLenum cap )
{
	if( cap == GL_FOG )
		bFogOn = false;
	
	((PFNGLDISABLE)p[70])( cap );
}

// glDisableClientState
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__71__() { __asm__("jmp *(_p+284)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__71__() { __asm__("jmp *(_p+284)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__71__()
{
	__asm
	{
		jmp p[71 * 4];
	}
}
#endif
#endif

// glDrawArrays
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__72__() { __asm__("jmp *(_p+288)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__72__()
{
	__asm
	{
		jmp p[72 * 4];
	}
}
#endif

// glDrawBuffer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__73__() { __asm__("jmp *(_p+292)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__73__() { __asm__("jmp *(_p+292)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__73__()
{
	__asm
	{
		jmp p[73 * 4];
	}
}
#endif
#endif

// glDrawElements
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__74__() { __asm__("jmp *(_p+296)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__74__()
{
	__asm
	{
		jmp p[74 * 4];
	}
}
#endif

// glDrawPixels
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__75__() { __asm__("jmp *(_p+300)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__75__() { __asm__("jmp *(_p+300)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__75__()
{
	__asm
	{
		jmp p[75 * 4];
	}
}
#endif
#endif

// glEdgeFlag
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__76__() { __asm__("jmp *(_p+304)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__76__() { __asm__("jmp *(_p+304)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__76__()
{
	__asm
	{
		jmp p[76 * 4];
	}
}
#endif
#endif

// glEdgeFlagPointer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__77__() { __asm__("jmp *(_p+308)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__77__() { __asm__("jmp *(_p+308)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__77__()
{
	__asm
	{
		jmp p[77 * 4];
	}
}
#endif
#endif

// glEdgeFlagv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__78__() { __asm__("jmp *(_p+312)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__78__() { __asm__("jmp *(_p+312)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__78__()
{
	__asm
	{
		jmp p[78 * 4];
	}
}
#endif
#endif

// glEnable
/*#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__79__() { __asm__("jmp *(_p+316)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__79__() { __asm__("jmp *(_p+316)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__79__()
{
	__asm
	{
		jmp p[79 * 4];
	}
}
#endif
#endif*/

typedef void (__stdcall *PFNGLENABLE)( GLenum );
extern "C" void __stdcall __stdcall __E__79__( GLenum cap )
{
	if( cap == GL_FOG )
		bFogOn = true;
	
	((PFNGLENABLE)p[79])( cap );
}

// glEnableClientState
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__80__() { __asm__("jmp *(_p+320)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__80__() { __asm__("jmp *(_p+320)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__80__()
{
	__asm
	{
		jmp p[80 * 4];
	}
}
#endif
#endif

// glEnd
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__81__() { __asm__("jmp *(_p+324)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__81__() { __asm__("jmp *(_p+324)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__81__()
{
	__asm
	{
		jmp p[81 * 4];
	}
}
#endif
#endif

// glEndList
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__82__() { __asm__("jmp *(_p+328)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__82__() { __asm__("jmp *(_p+328)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__82__()
{
	__asm
	{
		jmp p[82 * 4];
	}
}
#endif
#endif

// glEvalCoord1d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__83__() { __asm__("jmp *(_p+332)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__83__() { __asm__("jmp *(_p+332)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__83__()
{
	__asm
	{
		jmp p[83 * 4];
	}
}
#endif
#endif

// glEvalCoord1dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__84__() { __asm__("jmp *(_p+336)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__84__() { __asm__("jmp *(_p+336)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__84__()
{
	__asm
	{
		jmp p[84 * 4];
	}
}
#endif
#endif

// glEvalCoord1f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__85__() { __asm__("jmp *(_p+340)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__85__() { __asm__("jmp *(_p+340)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__85__()
{
	__asm
	{
		jmp p[85 * 4];
	}
}
#endif
#endif

// glEvalCoord1fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__86__() { __asm__("jmp *(_p+344)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__86__() { __asm__("jmp *(_p+344)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__86__()
{
	__asm
	{
		jmp p[86 * 4];
	}
}
#endif
#endif

// glEvalCoord2d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__87__() { __asm__("jmp *(_p+348)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__87__() { __asm__("jmp *(_p+348)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__87__()
{
	__asm
	{
		jmp p[87 * 4];
	}
}
#endif
#endif

// glEvalCoord2dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__88__() { __asm__("jmp *(_p+352)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__88__() { __asm__("jmp *(_p+352)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__88__()
{
	__asm
	{
		jmp p[88 * 4];
	}
}
#endif
#endif

// glEvalCoord2f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__89__() { __asm__("jmp *(_p+356)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__89__() { __asm__("jmp *(_p+356)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__89__()
{
	__asm
	{
		jmp p[89 * 4];
	}
}
#endif
#endif

// glEvalCoord2fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__90__() { __asm__("jmp *(_p+360)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__90__() { __asm__("jmp *(_p+360)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__90__()
{
	__asm
	{
		jmp p[90 * 4];
	}
}
#endif
#endif

// glEvalMesh1
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__91__() { __asm__("jmp *(_p+364)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__91__() { __asm__("jmp *(_p+364)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__91__()
{
	__asm
	{
		jmp p[91 * 4];
	}
}
#endif
#endif

// glEvalMesh2
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__92__() { __asm__("jmp *(_p+368)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__92__() { __asm__("jmp *(_p+368)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__92__()
{
	__asm
	{
		jmp p[92 * 4];
	}
}
#endif
#endif

// glEvalPoint1
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__93__() { __asm__("jmp *(_p+372)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__93__() { __asm__("jmp *(_p+372)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__93__()
{
	__asm
	{
		jmp p[93 * 4];
	}
}
#endif
#endif

// glEvalPoint2
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__94__() { __asm__("jmp *(_p+376)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__94__() { __asm__("jmp *(_p+376)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__94__()
{
	__asm
	{
		jmp p[94 * 4];
	}
}
#endif
#endif

// glFeedbackBuffer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__95__() { __asm__("jmp *(_p+380)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__95__() { __asm__("jmp *(_p+380)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__95__()
{
	__asm
	{
		jmp p[95 * 4];
	}
}
#endif
#endif

// glFinish
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__96__() { __asm__("jmp *(_p+384)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__96__() { __asm__("jmp *(_p+384)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__96__()
{
	__asm
	{
		jmp p[96 * 4];
	}
}
#endif
#endif

// glFlush
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__97__() { __asm__("jmp *(_p+388)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__97__() { __asm__("jmp *(_p+388)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__97__()
{
	__asm
	{
		jmp p[97 * 4];
	}
}
#endif
#endif

// glFogf
/*#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__98__() { __asm__("jmp *(_p+392)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__98__() { __asm__("jmp *(_p+392)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__98__()
{
	__asm
	{
		jmp p[98 * 4];
	}
}
#endif
#endif*/

extern "C" void __stdcall __E__98__( GLenum pname,  GLfloat param )
{
	switch( pname )
	{
	case GL_FOG_MODE:
		fogMode = param;
		fogRecalculate();
		break;
	case GL_FOG_DENSITY:
		fogDensity = param;
		fogRecalculate();
		break;
	case GL_FOG_START:
		fogStart = param;
		fogRecalculate();
		break;
	case GL_FOG_END:
		fogEnd = param;
		fogRecalculate();
		break;
	};

	orig_glFogf( pname, param );
}

// glFogfv
/*#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__99__() { __asm__("jmp *(_p+396)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__99__() { __asm__("jmp *(_p+396)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__99__()
{
	__asm
	{
		jmp p[99 * 4];
	}
}
#endif
#endif*/

extern "C" void __stdcall __E__99__( GLenum pname,  GLfloat *param )
{
	switch( pname )
	{
	case GL_FOG_MODE:
		fogMode = *param;
		fogRecalculate();
		break;
	case GL_FOG_DENSITY:
		fogDensity = *param;
		fogRecalculate();
		break;
	case GL_FOG_START:
		fogStart = *param;
		fogRecalculate();
		break;
	case GL_FOG_END:
		fogEnd = *param;
		fogRecalculate();
		break;
	};

	orig_glFogfv( pname, param );
}

// glFogi
/*#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__100__() { __asm__("jmp *(_p+400)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__100__() { __asm__("jmp *(_p+400)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__100__()
{
	__asm
	{
		jmp p[100 * 4];
	}
}
#endif
#endif*/

extern "C" void __stdcall __E__100__( GLenum pname,  GLint param )
{
	switch( pname )
	{
	case GL_FOG_MODE:
		fogMode = param;
		fogRecalculate();
		break;
	case GL_FOG_DENSITY:
		fogDensity = param;
		fogRecalculate();
		break;
	case GL_FOG_START:
		fogStart = param;
		fogRecalculate();
		break;
	case GL_FOG_END:
		fogEnd = param;
		fogRecalculate();
		break;
	};

	orig_glFogi( pname, param );
}

// glFogiv
/*#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__101__() { __asm__("jmp *(_p+404)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__101__() { __asm__("jmp *(_p+404)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__101__()
{
	__asm
	{
		jmp p[101 * 4];
	}
}
#endif
#endif*/

extern "C" void __stdcall __E__101__( GLenum pname,  GLint *param )
{
	switch( pname )
	{
	case GL_FOG_MODE:
		fogMode = *param;
		fogRecalculate();
		break;
	case GL_FOG_DENSITY:
		fogDensity = *param;
		fogRecalculate();
		break;
	case GL_FOG_START:
		fogStart = *param;
		fogRecalculate();
		break;
	case GL_FOG_END:
		fogEnd = *param;
		fogRecalculate();
		break;
	};

	orig_glFogiv( pname, param );
}

// glFrontFace
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__102__() { __asm__("jmp *(_p+408)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__102__() { __asm__("jmp *(_p+408)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__102__()
{
	__asm
	{
		jmp p[102 * 4];
	}
}
#endif
#endif

// glFrustum
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__103__() { __asm__("jmp *(_p+412)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__103__() { __asm__("jmp *(_p+412)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__103__()
{
	__asm
	{
		jmp p[103 * 4];
	}
}
#endif
#endif

// glGenLists
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__104__() { __asm__("jmp *(_p+416)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__104__() { __asm__("jmp *(_p+416)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__104__()
{
	__asm
	{
		jmp p[104 * 4];
	}
}
#endif
#endif

// glGenTextures
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__105__() { __asm__("jmp *(_p+420)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__105__() { __asm__("jmp *(_p+420)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__105__()
{
	__asm
	{
		jmp p[105 * 4];
	}
}
#endif
#endif

// glGetBooleanv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__106__() { __asm__("jmp *(_p+424)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__106__() { __asm__("jmp *(_p+424)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__106__()
{
	__asm
	{
		jmp p[106 * 4];
	}
}
#endif
#endif

// glGetClipPlane
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__107__() { __asm__("jmp *(_p+428)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__107__() { __asm__("jmp *(_p+428)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__107__()
{
	__asm
	{
		jmp p[107 * 4];
	}
}
#endif
#endif

// glGetDoublev
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__108__() { __asm__("jmp *(_p+432)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__108__() { __asm__("jmp *(_p+432)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__108__()
{
	__asm
	{
		jmp p[108 * 4];
	}
}
#endif
#endif

// glGetError
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__109__() { __asm__("jmp *(_p+436)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__109__() { __asm__("jmp *(_p+436)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__109__()
{
	__asm
	{
		jmp p[109 * 4];
	}
}
#endif
#endif

// glGetFloatv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__110__() { __asm__("jmp *(_p+440)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__110__() { __asm__("jmp *(_p+440)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__110__()
{
	__asm
	{
		jmp p[110 * 4];
	}
}
#endif
#endif

// glGetIntegerv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__111__() { __asm__("jmp *(_p+444)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__111__() { __asm__("jmp *(_p+444)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__111__()
{
	__asm
	{
		jmp p[111 * 4];
	}
}
#endif
#endif

// glGetLightfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__112__() { __asm__("jmp *(_p+448)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__112__() { __asm__("jmp *(_p+448)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__112__()
{
	__asm
	{
		jmp p[112 * 4];
	}
}
#endif
#endif

// glGetLightiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__113__() { __asm__("jmp *(_p+452)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__113__() { __asm__("jmp *(_p+452)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__113__()
{
	__asm
	{
		jmp p[113 * 4];
	}
}
#endif
#endif

// glGetMapdv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__114__() { __asm__("jmp *(_p+456)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__114__() { __asm__("jmp *(_p+456)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__114__()
{
	__asm
	{
		jmp p[114 * 4];
	}
}
#endif
#endif

// glGetMapfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__115__() { __asm__("jmp *(_p+460)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__115__() { __asm__("jmp *(_p+460)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__115__()
{
	__asm
	{
		jmp p[115 * 4];
	}
}
#endif
#endif

// glGetMapiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__116__() { __asm__("jmp *(_p+464)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__116__() { __asm__("jmp *(_p+464)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__116__()
{
	__asm
	{
		jmp p[116 * 4];
	}
}
#endif
#endif

// glGetMaterialfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__117__() { __asm__("jmp *(_p+468)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__117__() { __asm__("jmp *(_p+468)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__117__()
{
	__asm
	{
		jmp p[117 * 4];
	}
}
#endif
#endif

// glGetMaterialiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__118__() { __asm__("jmp *(_p+472)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__118__() { __asm__("jmp *(_p+472)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__118__()
{
	__asm
	{
		jmp p[118 * 4];
	}
}
#endif
#endif

// glGetPixelMapfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__119__() { __asm__("jmp *(_p+476)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__119__() { __asm__("jmp *(_p+476)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__119__()
{
	__asm
	{
		jmp p[119 * 4];
	}
}
#endif
#endif

// glGetPixelMapuiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__120__() { __asm__("jmp *(_p+480)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__120__() { __asm__("jmp *(_p+480)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__120__()
{
	__asm
	{
		jmp p[120 * 4];
	}
}
#endif
#endif

// glGetPixelMapusv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__121__() { __asm__("jmp *(_p+484)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__121__() { __asm__("jmp *(_p+484)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__121__()
{
	__asm
	{
		jmp p[121 * 4];
	}
}
#endif
#endif

// glGetPointerv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__122__() { __asm__("jmp *(_p+488)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__122__() { __asm__("jmp *(_p+488)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__122__()
{
	__asm
	{
		jmp p[122 * 4];
	}
}
#endif
#endif

// glGetPolygonStipple
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__123__() { __asm__("jmp *(_p+492)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__123__() { __asm__("jmp *(_p+492)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__123__()
{
	__asm
	{
		jmp p[123 * 4];
	}
}
#endif
#endif

// glGetString
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__124__() { __asm__("jmp *(_p+496)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__124__() { __asm__("jmp *(_p+496)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__124__()
{
	__asm
	{
		jmp p[124 * 4];
	}
}
#endif
#endif

// glGetTexEnvfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__125__() { __asm__("jmp *(_p+500)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__125__() { __asm__("jmp *(_p+500)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__125__()
{
	__asm
	{
		jmp p[125 * 4];
	}
}
#endif
#endif

// glGetTexEnviv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__126__() { __asm__("jmp *(_p+504)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__126__() { __asm__("jmp *(_p+504)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__126__()
{
	__asm
	{
		jmp p[126 * 4];
	}
}
#endif
#endif

// glGetTexGendv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__127__() { __asm__("jmp *(_p+508)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__127__() { __asm__("jmp *(_p+508)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__127__()
{
	__asm
	{
		jmp p[127 * 4];
	}
}
#endif
#endif

// glGetTexGenfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__128__() { __asm__("jmp *(_p+512)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__128__() { __asm__("jmp *(_p+512)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__128__()
{
	__asm
	{
		jmp p[128 * 4];
	}
}
#endif
#endif

// glGetTexGeniv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__129__() { __asm__("jmp *(_p+516)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__129__() { __asm__("jmp *(_p+516)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__129__()
{
	__asm
	{
		jmp p[129 * 4];
	}
}
#endif
#endif

// glGetTexImage
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__130__() { __asm__("jmp *(_p+520)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__130__() { __asm__("jmp *(_p+520)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__130__()
{
	__asm
	{
		jmp p[130 * 4];
	}
}
#endif
#endif

// glGetTexLevelParameterfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__131__() { __asm__("jmp *(_p+524)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__131__() { __asm__("jmp *(_p+524)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__131__()
{
	__asm
	{
		jmp p[131 * 4];
	}
}
#endif
#endif

// glGetTexLevelParameteriv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__132__() { __asm__("jmp *(_p+528)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__132__() { __asm__("jmp *(_p+528)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__132__()
{
	__asm
	{
		jmp p[132 * 4];
	}
}
#endif
#endif

// glGetTexParameterfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__133__() { __asm__("jmp *(_p+532)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__133__() { __asm__("jmp *(_p+532)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__133__()
{
	__asm
	{
		jmp p[133 * 4];
	}
}
#endif
#endif

// glGetTexParameteriv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__134__() { __asm__("jmp *(_p+536)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__134__() { __asm__("jmp *(_p+536)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__134__()
{
	__asm
	{
		jmp p[134 * 4];
	}
}
#endif
#endif

// glHint
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__135__() { __asm__("jmp *(_p+540)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__135__() { __asm__("jmp *(_p+540)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__135__()
{
	__asm
	{
		jmp p[135 * 4];
	}
}
#endif
#endif

// glIndexMask
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__136__() { __asm__("jmp *(_p+544)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__136__() { __asm__("jmp *(_p+544)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__136__()
{
	__asm
	{
		jmp p[136 * 4];
	}
}
#endif
#endif

// glIndexPointer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__137__() { __asm__("jmp *(_p+548)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__137__() { __asm__("jmp *(_p+548)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__137__()
{
	__asm
	{
		jmp p[137 * 4];
	}
}
#endif
#endif

// glIndexd
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__138__() { __asm__("jmp *(_p+552)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__138__() { __asm__("jmp *(_p+552)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__138__()
{
	__asm
	{
		jmp p[138 * 4];
	}
}
#endif
#endif

// glIndexdv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__139__() { __asm__("jmp *(_p+556)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__139__() { __asm__("jmp *(_p+556)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__139__()
{
	__asm
	{
		jmp p[139 * 4];
	}
}
#endif
#endif

// glIndexf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__140__() { __asm__("jmp *(_p+560)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__140__() { __asm__("jmp *(_p+560)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__140__()
{
	__asm
	{
		jmp p[140 * 4];
	}
}
#endif
#endif

// glIndexfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__141__() { __asm__("jmp *(_p+564)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__141__() { __asm__("jmp *(_p+564)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__141__()
{
	__asm
	{
		jmp p[141 * 4];
	}
}
#endif
#endif

// glIndexi
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__142__() { __asm__("jmp *(_p+568)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__142__() { __asm__("jmp *(_p+568)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__142__()
{
	__asm
	{
		jmp p[142 * 4];
	}
}
#endif
#endif

// glIndexiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__143__() { __asm__("jmp *(_p+572)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__143__() { __asm__("jmp *(_p+572)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__143__()
{
	__asm
	{
		jmp p[143 * 4];
	}
}
#endif
#endif

// glIndexs
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__144__() { __asm__("jmp *(_p+576)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__144__() { __asm__("jmp *(_p+576)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__144__()
{
	__asm
	{
		jmp p[144 * 4];
	}
}
#endif
#endif

// glIndexsv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__145__() { __asm__("jmp *(_p+580)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__145__() { __asm__("jmp *(_p+580)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__145__()
{
	__asm
	{
		jmp p[145 * 4];
	}
}
#endif
#endif

// glIndexub
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__146__() { __asm__("jmp *(_p+584)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__146__() { __asm__("jmp *(_p+584)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__146__()
{
	__asm
	{
		jmp p[146 * 4];
	}
}
#endif
#endif

// glIndexubv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__147__() { __asm__("jmp *(_p+588)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__147__() { __asm__("jmp *(_p+588)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__147__()
{
	__asm
	{
		jmp p[147 * 4];
	}
}
#endif
#endif

// glInitNames
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__148__() { __asm__("jmp *(_p+592)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__148__() { __asm__("jmp *(_p+592)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__148__()
{
	__asm
	{
		jmp p[148 * 4];
	}
}
#endif
#endif

// glInterleavedArrays
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__149__() { __asm__("jmp *(_p+596)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__149__() { __asm__("jmp *(_p+596)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__149__()
{
	__asm
	{
		jmp p[149 * 4];
	}
}
#endif
#endif

// glIsEnabled
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__150__() { __asm__("jmp *(_p+600)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__150__() { __asm__("jmp *(_p+600)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__150__()
{
	__asm
	{
		jmp p[150 * 4];
	}
}
#endif
#endif

// glIsList
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__151__() { __asm__("jmp *(_p+604)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__151__() { __asm__("jmp *(_p+604)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__151__()
{
	__asm
	{
		jmp p[151 * 4];
	}
}
#endif
#endif

// glIsTexture
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__152__() { __asm__("jmp *(_p+608)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__152__() { __asm__("jmp *(_p+608)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__152__()
{
	__asm
	{
		jmp p[152 * 4];
	}
}
#endif
#endif

// glLightModelf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__153__() { __asm__("jmp *(_p+612)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__153__() { __asm__("jmp *(_p+612)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__153__()
{
	__asm
	{
		jmp p[153 * 4];
	}
}
#endif
#endif

// glLightModelfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__154__() { __asm__("jmp *(_p+616)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__154__() { __asm__("jmp *(_p+616)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__154__()
{
	__asm
	{
		jmp p[154 * 4];
	}
}
#endif
#endif

// glLightModeli
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__155__() { __asm__("jmp *(_p+620)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__155__() { __asm__("jmp *(_p+620)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__155__()
{
	__asm
	{
		jmp p[155 * 4];
	}
}
#endif
#endif

// glLightModeliv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__156__() { __asm__("jmp *(_p+624)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__156__() { __asm__("jmp *(_p+624)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__156__()
{
	__asm
	{
		jmp p[156 * 4];
	}
}
#endif
#endif

// glLightf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__157__() { __asm__("jmp *(_p+628)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__157__() { __asm__("jmp *(_p+628)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__157__()
{
	__asm
	{
		jmp p[157 * 4];
	}
}
#endif
#endif

// glLightfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__158__() { __asm__("jmp *(_p+632)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__158__() { __asm__("jmp *(_p+632)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__158__()
{
	__asm
	{
		jmp p[158 * 4];
	}
}
#endif
#endif

// glLighti
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__159__() { __asm__("jmp *(_p+636)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__159__() { __asm__("jmp *(_p+636)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__159__()
{
	__asm
	{
		jmp p[159 * 4];
	}
}
#endif
#endif

// glLightiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__160__() { __asm__("jmp *(_p+640)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__160__() { __asm__("jmp *(_p+640)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__160__()
{
	__asm
	{
		jmp p[160 * 4];
	}
}
#endif
#endif

// glLineStipple
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__161__() { __asm__("jmp *(_p+644)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__161__() { __asm__("jmp *(_p+644)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__161__()
{
	__asm
	{
		jmp p[161 * 4];
	}
}
#endif
#endif

// glLineWidth
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__162__() { __asm__("jmp *(_p+648)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__162__() { __asm__("jmp *(_p+648)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__162__()
{
	__asm
	{
		jmp p[162 * 4];
	}
}
#endif
#endif

// glListBase
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__163__() { __asm__("jmp *(_p+652)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__163__() { __asm__("jmp *(_p+652)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__163__()
{
	__asm
	{
		jmp p[163 * 4];
	}
}
#endif
#endif

// glLoadIdentity
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__164__() { __asm__("jmp *(_p+656)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__164__() { __asm__("jmp *(_p+656)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__164__()
{
	__asm
	{
		jmp p[164 * 4];
	}
}
#endif
#endif

// glLoadMatrixd
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__165__() { __asm__("jmp *(_p+660)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__165__() { __asm__("jmp *(_p+660)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__165__()
{
	__asm
	{
		jmp p[165 * 4];
	}
}
#endif
#endif

// glLoadMatrixf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__166__() { __asm__("jmp *(_p+664)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__166__() { __asm__("jmp *(_p+664)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__166__()
{
	__asm
	{
		jmp p[166 * 4];
	}
}
#endif
#endif

// glLoadName
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__167__() { __asm__("jmp *(_p+668)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__167__() { __asm__("jmp *(_p+668)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__167__()
{
	__asm
	{
		jmp p[167 * 4];
	}
}
#endif
#endif

// glLogicOp
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__168__() { __asm__("jmp *(_p+672)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__168__() { __asm__("jmp *(_p+672)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__168__()
{
	__asm
	{
		jmp p[168 * 4];
	}
}
#endif
#endif

// glMap1d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__169__() { __asm__("jmp *(_p+676)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__169__() { __asm__("jmp *(_p+676)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__169__()
{
	__asm
	{
		jmp p[169 * 4];
	}
}
#endif
#endif

// glMap1f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__170__() { __asm__("jmp *(_p+680)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__170__() { __asm__("jmp *(_p+680)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__170__()
{
	__asm
	{
		jmp p[170 * 4];
	}
}
#endif
#endif

// glMap2d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__171__() { __asm__("jmp *(_p+684)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__171__() { __asm__("jmp *(_p+684)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__171__()
{
	__asm
	{
		jmp p[171 * 4];
	}
}
#endif
#endif

// glMap2f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__172__() { __asm__("jmp *(_p+688)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__172__() { __asm__("jmp *(_p+688)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__172__()
{
	__asm
	{
		jmp p[172 * 4];
	}
}
#endif
#endif

// glMapGrid1d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__173__() { __asm__("jmp *(_p+692)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__173__() { __asm__("jmp *(_p+692)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__173__()
{
	__asm
	{
		jmp p[173 * 4];
	}
}
#endif
#endif

// glMapGrid1f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__174__() { __asm__("jmp *(_p+696)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__174__() { __asm__("jmp *(_p+696)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__174__()
{
	__asm
	{
		jmp p[174 * 4];
	}
}
#endif
#endif

// glMapGrid2d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__175__() { __asm__("jmp *(_p+700)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__175__() { __asm__("jmp *(_p+700)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__175__()
{
	__asm
	{
		jmp p[175 * 4];
	}
}
#endif
#endif

// glMapGrid2f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__176__() { __asm__("jmp *(_p+704)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__176__() { __asm__("jmp *(_p+704)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__176__()
{
	__asm
	{
		jmp p[176 * 4];
	}
}
#endif
#endif

// glMaterialf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__177__() { __asm__("jmp *(_p+708)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__177__() { __asm__("jmp *(_p+708)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__177__()
{
	__asm
	{
		jmp p[177 * 4];
	}
}
#endif
#endif

// glMaterialfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__178__() { __asm__("jmp *(_p+712)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__178__() { __asm__("jmp *(_p+712)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__178__()
{
	__asm
	{
		jmp p[178 * 4];
	}
}
#endif
#endif

// glMateriali
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__179__() { __asm__("jmp *(_p+716)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__179__() { __asm__("jmp *(_p+716)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__179__()
{
	__asm
	{
		jmp p[179 * 4];
	}
}
#endif
#endif

// glMaterialiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__180__() { __asm__("jmp *(_p+720)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__180__() { __asm__("jmp *(_p+720)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__180__()
{
	__asm
	{
		jmp p[180 * 4];
	}
}
#endif
#endif

// glMatrixMode
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__181__() { __asm__("jmp *(_p+724)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__181__() { __asm__("jmp *(_p+724)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__181__()
{
	__asm
	{
		jmp p[181 * 4];
	}
}
#endif
#endif

// glMultMatrixd
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__182__() { __asm__("jmp *(_p+728)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__182__() { __asm__("jmp *(_p+728)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__182__()
{
	__asm
	{
		jmp p[182 * 4];
	}
}
#endif
#endif

// glMultMatrixf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__183__() { __asm__("jmp *(_p+732)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__183__() { __asm__("jmp *(_p+732)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__183__()
{
	__asm
	{
		jmp p[183 * 4];
	}
}
#endif
#endif

// glNewList
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__184__() { __asm__("jmp *(_p+736)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__184__() { __asm__("jmp *(_p+736)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__184__()
{
	__asm
	{
		jmp p[184 * 4];
	}
}
#endif
#endif

// glNormal3b
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__185__() { __asm__("jmp *(_p+740)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__185__() { __asm__("jmp *(_p+740)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__185__()
{
	__asm
	{
		jmp p[185 * 4];
	}
}
#endif
#endif

// glNormal3bv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__186__() { __asm__("jmp *(_p+744)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__186__() { __asm__("jmp *(_p+744)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__186__()
{
	__asm
	{
		jmp p[186 * 4];
	}
}
#endif
#endif

// glNormal3d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__187__() { __asm__("jmp *(_p+748)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__187__() { __asm__("jmp *(_p+748)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__187__()
{
	__asm
	{
		jmp p[187 * 4];
	}
}
#endif
#endif

// glNormal3dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__188__() { __asm__("jmp *(_p+752)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__188__() { __asm__("jmp *(_p+752)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__188__()
{
	__asm
	{
		jmp p[188 * 4];
	}
}
#endif
#endif

// glNormal3f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__189__() { __asm__("jmp *(_p+756)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__189__() { __asm__("jmp *(_p+756)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__189__()
{
	__asm
	{
		jmp p[189 * 4];
	}
}
#endif
#endif

// glNormal3fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__190__() { __asm__("jmp *(_p+760)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__190__() { __asm__("jmp *(_p+760)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__190__()
{
	__asm
	{
		jmp p[190 * 4];
	}
}
#endif
#endif

// glNormal3i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__191__() { __asm__("jmp *(_p+764)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__191__() { __asm__("jmp *(_p+764)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__191__()
{
	__asm
	{
		jmp p[191 * 4];
	}
}
#endif
#endif

// glNormal3iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__192__() { __asm__("jmp *(_p+768)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__192__() { __asm__("jmp *(_p+768)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__192__()
{
	__asm
	{
		jmp p[192 * 4];
	}
}
#endif
#endif

// glNormal3s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__193__() { __asm__("jmp *(_p+772)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__193__() { __asm__("jmp *(_p+772)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__193__()
{
	__asm
	{
		jmp p[193 * 4];
	}
}
#endif
#endif

// glNormal3sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__194__() { __asm__("jmp *(_p+776)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__194__() { __asm__("jmp *(_p+776)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__194__()
{
	__asm
	{
		jmp p[194 * 4];
	}
}
#endif
#endif

// glNormalPointer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__195__() { __asm__("jmp *(_p+780)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__195__() { __asm__("jmp *(_p+780)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__195__()
{
	__asm
	{
		jmp p[195 * 4];
	}
}
#endif
#endif

// glOrtho
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__196__() { __asm__("jmp *(_p+784)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__196__() { __asm__("jmp *(_p+784)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__196__()
{
	__asm
	{
		jmp p[196 * 4];
	}
}
#endif
#endif

// glPassThrough
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__197__() { __asm__("jmp *(_p+788)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__197__() { __asm__("jmp *(_p+788)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__197__()
{
	__asm
	{
		jmp p[197 * 4];
	}
}
#endif
#endif

// glPixelMapfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__198__() { __asm__("jmp *(_p+792)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__198__() { __asm__("jmp *(_p+792)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__198__()
{
	__asm
	{
		jmp p[198 * 4];
	}
}
#endif
#endif

// glPixelMapuiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__199__() { __asm__("jmp *(_p+796)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__199__() { __asm__("jmp *(_p+796)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__199__()
{
	__asm
	{
		jmp p[199 * 4];
	}
}
#endif
#endif

// glPixelMapusv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__200__() { __asm__("jmp *(_p+800)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__200__() { __asm__("jmp *(_p+800)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__200__()
{
	__asm
	{
		jmp p[200 * 4];
	}
}
#endif
#endif

// glPixelStoref
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__201__() { __asm__("jmp *(_p+804)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__201__() { __asm__("jmp *(_p+804)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__201__()
{
	__asm
	{
		jmp p[201 * 4];
	}
}
#endif
#endif

// glPixelStorei
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__202__() { __asm__("jmp *(_p+808)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__202__() { __asm__("jmp *(_p+808)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__202__()
{
	__asm
	{
		jmp p[202 * 4];
	}
}
#endif
#endif

// glPixelTransferf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__203__() { __asm__("jmp *(_p+812)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__203__() { __asm__("jmp *(_p+812)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__203__()
{
	__asm
	{
		jmp p[203 * 4];
	}
}
#endif
#endif

// glPixelTransferi
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__204__() { __asm__("jmp *(_p+816)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__204__() { __asm__("jmp *(_p+816)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__204__()
{
	__asm
	{
		jmp p[204 * 4];
	}
}
#endif
#endif

// glPixelZoom
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__205__() { __asm__("jmp *(_p+820)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__205__() { __asm__("jmp *(_p+820)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__205__()
{
	__asm
	{
		jmp p[205 * 4];
	}
}
#endif
#endif

// glPointSize
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__206__() { __asm__("jmp *(_p+824)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__206__() { __asm__("jmp *(_p+824)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__206__()
{
	__asm
	{
		jmp p[206 * 4];
	}
}
#endif
#endif

// glPolygonMode
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__207__() { __asm__("jmp *(_p+828)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__207__() { __asm__("jmp *(_p+828)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__207__()
{
	__asm
	{
		jmp p[207 * 4];
	}
}
#endif
#endif

// glPolygonOffset
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__208__() { __asm__("jmp *(_p+832)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__208__() { __asm__("jmp *(_p+832)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__208__()
{
	__asm
	{
		jmp p[208 * 4];
	}
}
#endif
#endif

// glPolygonStipple
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__209__() { __asm__("jmp *(_p+836)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__209__() { __asm__("jmp *(_p+836)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__209__()
{
	__asm
	{
		jmp p[209 * 4];
	}
}
#endif
#endif

// glPopAttrib
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__210__() { __asm__("jmp *(_p+840)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__210__() { __asm__("jmp *(_p+840)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__210__()
{
	__asm
	{
		jmp p[210 * 4];
	}
}
#endif
#endif

// glPopClientAttrib
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__211__() { __asm__("jmp *(_p+844)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__211__() { __asm__("jmp *(_p+844)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__211__()
{
	__asm
	{
		jmp p[211 * 4];
	}
}
#endif
#endif

// glPopMatrix
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__212__() { __asm__("jmp *(_p+848)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__212__() { __asm__("jmp *(_p+848)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__212__()
{
	__asm
	{
		jmp p[212 * 4];
	}
}
#endif
#endif

// glPopName
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__213__() { __asm__("jmp *(_p+852)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__213__() { __asm__("jmp *(_p+852)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__213__()
{
	__asm
	{
		jmp p[213 * 4];
	}
}
#endif
#endif

// glPrioritizeTextures
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__214__() { __asm__("jmp *(_p+856)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__214__() { __asm__("jmp *(_p+856)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__214__()
{
	__asm
	{
		jmp p[214 * 4];
	}
}
#endif
#endif

// glPushAttrib
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__215__() { __asm__("jmp *(_p+860)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__215__() { __asm__("jmp *(_p+860)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__215__()
{
	__asm
	{
		jmp p[215 * 4];
	}
}
#endif
#endif

// glPushClientAttrib
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__216__() { __asm__("jmp *(_p+864)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__216__() { __asm__("jmp *(_p+864)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__216__()
{
	__asm
	{
		jmp p[216 * 4];
	}
}
#endif
#endif

// glPushMatrix
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__217__() { __asm__("jmp *(_p+868)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__217__() { __asm__("jmp *(_p+868)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__217__()
{
	__asm
	{
		jmp p[217 * 4];
	}
}
#endif
#endif

// glPushName
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__218__() { __asm__("jmp *(_p+872)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__218__() { __asm__("jmp *(_p+872)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__218__()
{
	__asm
	{
		jmp p[218 * 4];
	}
}
#endif
#endif

// glRasterPos2d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__219__() { __asm__("jmp *(_p+876)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__219__() { __asm__("jmp *(_p+876)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__219__()
{
	__asm
	{
		jmp p[219 * 4];
	}
}
#endif
#endif

// glRasterPos2dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__220__() { __asm__("jmp *(_p+880)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__220__() { __asm__("jmp *(_p+880)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__220__()
{
	__asm
	{
		jmp p[220 * 4];
	}
}
#endif
#endif

// glRasterPos2f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__221__() { __asm__("jmp *(_p+884)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__221__() { __asm__("jmp *(_p+884)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__221__()
{
	__asm
	{
		jmp p[221 * 4];
	}
}
#endif
#endif

// glRasterPos2fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__222__() { __asm__("jmp *(_p+888)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__222__() { __asm__("jmp *(_p+888)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__222__()
{
	__asm
	{
		jmp p[222 * 4];
	}
}
#endif
#endif

// glRasterPos2i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__223__() { __asm__("jmp *(_p+892)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__223__() { __asm__("jmp *(_p+892)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__223__()
{
	__asm
	{
		jmp p[223 * 4];
	}
}
#endif
#endif

// glRasterPos2iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__224__() { __asm__("jmp *(_p+896)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__224__() { __asm__("jmp *(_p+896)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__224__()
{
	__asm
	{
		jmp p[224 * 4];
	}
}
#endif
#endif

// glRasterPos2s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__225__() { __asm__("jmp *(_p+900)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__225__() { __asm__("jmp *(_p+900)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__225__()
{
	__asm
	{
		jmp p[225 * 4];
	}
}
#endif
#endif

// glRasterPos2sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__226__() { __asm__("jmp *(_p+904)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__226__() { __asm__("jmp *(_p+904)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__226__()
{
	__asm
	{
		jmp p[226 * 4];
	}
}
#endif
#endif

// glRasterPos3d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__227__() { __asm__("jmp *(_p+908)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__227__() { __asm__("jmp *(_p+908)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__227__()
{
	__asm
	{
		jmp p[227 * 4];
	}
}
#endif
#endif

// glRasterPos3dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__228__() { __asm__("jmp *(_p+912)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__228__() { __asm__("jmp *(_p+912)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__228__()
{
	__asm
	{
		jmp p[228 * 4];
	}
}
#endif
#endif

// glRasterPos3f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__229__() { __asm__("jmp *(_p+916)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__229__() { __asm__("jmp *(_p+916)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__229__()
{
	__asm
	{
		jmp p[229 * 4];
	}
}
#endif
#endif

// glRasterPos3fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__230__() { __asm__("jmp *(_p+920)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__230__() { __asm__("jmp *(_p+920)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__230__()
{
	__asm
	{
		jmp p[230 * 4];
	}
}
#endif
#endif

// glRasterPos3i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__231__() { __asm__("jmp *(_p+924)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__231__() { __asm__("jmp *(_p+924)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__231__()
{
	__asm
	{
		jmp p[231 * 4];
	}
}
#endif
#endif

// glRasterPos3iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__232__() { __asm__("jmp *(_p+928)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__232__() { __asm__("jmp *(_p+928)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__232__()
{
	__asm
	{
		jmp p[232 * 4];
	}
}
#endif
#endif

// glRasterPos3s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__233__() { __asm__("jmp *(_p+932)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__233__() { __asm__("jmp *(_p+932)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__233__()
{
	__asm
	{
		jmp p[233 * 4];
	}
}
#endif
#endif

// glRasterPos3sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__234__() { __asm__("jmp *(_p+936)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__234__() { __asm__("jmp *(_p+936)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__234__()
{
	__asm
	{
		jmp p[234 * 4];
	}
}
#endif
#endif

// glRasterPos4d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__235__() { __asm__("jmp *(_p+940)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__235__() { __asm__("jmp *(_p+940)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__235__()
{
	__asm
	{
		jmp p[235 * 4];
	}
}
#endif
#endif

// glRasterPos4dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__236__() { __asm__("jmp *(_p+944)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__236__() { __asm__("jmp *(_p+944)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__236__()
{
	__asm
	{
		jmp p[236 * 4];
	}
}
#endif
#endif

// glRasterPos4f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__237__() { __asm__("jmp *(_p+948)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__237__() { __asm__("jmp *(_p+948)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__237__()
{
	__asm
	{
		jmp p[237 * 4];
	}
}
#endif
#endif

// glRasterPos4fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__238__() { __asm__("jmp *(_p+952)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__238__() { __asm__("jmp *(_p+952)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__238__()
{
	__asm
	{
		jmp p[238 * 4];
	}
}
#endif
#endif

// glRasterPos4i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__239__() { __asm__("jmp *(_p+956)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__239__() { __asm__("jmp *(_p+956)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__239__()
{
	__asm
	{
		jmp p[239 * 4];
	}
}
#endif
#endif

// glRasterPos4iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__240__() { __asm__("jmp *(_p+960)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__240__() { __asm__("jmp *(_p+960)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__240__()
{
	__asm
	{
		jmp p[240 * 4];
	}
}
#endif
#endif

// glRasterPos4s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__241__() { __asm__("jmp *(_p+964)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__241__() { __asm__("jmp *(_p+964)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__241__()
{
	__asm
	{
		jmp p[241 * 4];
	}
}
#endif
#endif

// glRasterPos4sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__242__() { __asm__("jmp *(_p+968)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__242__() { __asm__("jmp *(_p+968)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__242__()
{
	__asm
	{
		jmp p[242 * 4];
	}
}
#endif
#endif

// glReadBuffer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__243__() { __asm__("jmp *(_p+972)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__243__() { __asm__("jmp *(_p+972)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__243__()
{
	__asm
	{
		jmp p[243 * 4];
	}
}
#endif
#endif

// glReadPixels
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__244__() { __asm__("jmp *(_p+976)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__244__() { __asm__("jmp *(_p+976)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__244__()
{
	__asm
	{
		jmp p[244 * 4];
	}
}
#endif
#endif

// glRectd
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__245__() { __asm__("jmp *(_p+980)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__245__() { __asm__("jmp *(_p+980)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__245__()
{
	__asm
	{
		jmp p[245 * 4];
	}
}
#endif
#endif

// glRectdv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__246__() { __asm__("jmp *(_p+984)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__246__() { __asm__("jmp *(_p+984)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__246__()
{
	__asm
	{
		jmp p[246 * 4];
	}
}
#endif
#endif

// glRectf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__247__() { __asm__("jmp *(_p+988)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__247__() { __asm__("jmp *(_p+988)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__247__()
{
	__asm
	{
		jmp p[247 * 4];
	}
}
#endif
#endif

// glRectfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__248__() { __asm__("jmp *(_p+992)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__248__() { __asm__("jmp *(_p+992)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__248__()
{
	__asm
	{
		jmp p[248 * 4];
	}
}
#endif
#endif

// glRecti
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__249__() { __asm__("jmp *(_p+996)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__249__() { __asm__("jmp *(_p+996)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__249__()
{
	__asm
	{
		jmp p[249 * 4];
	}
}
#endif
#endif

// glRectiv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__250__() { __asm__("jmp *(_p+1000)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__250__() { __asm__("jmp *(_p+1000)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__250__()
{
	__asm
	{
		jmp p[250 * 4];
	}
}
#endif
#endif

// glRects
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__251__() { __asm__("jmp *(_p+1004)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__251__() { __asm__("jmp *(_p+1004)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__251__()
{
	__asm
	{
		jmp p[251 * 4];
	}
}
#endif
#endif

// glRectsv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__252__() { __asm__("jmp *(_p+1008)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__252__() { __asm__("jmp *(_p+1008)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__252__()
{
	__asm
	{
		jmp p[252 * 4];
	}
}
#endif
#endif

// glRenderMode
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__253__() { __asm__("jmp *(_p+1012)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__253__() { __asm__("jmp *(_p+1012)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__253__()
{
	__asm
	{
		jmp p[253 * 4];
	}
}
#endif
#endif

// glRotated
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__254__() { __asm__("jmp *(_p+1016)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__254__() { __asm__("jmp *(_p+1016)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__254__()
{
	__asm
	{
		jmp p[254 * 4];
	}
}
#endif
#endif

// glRotatef
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__255__() { __asm__("jmp *(_p+1020)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__255__() { __asm__("jmp *(_p+1020)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__255__()
{
	__asm
	{
		jmp p[255 * 4];
	}
}
#endif
#endif

// glScaled
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__256__() { __asm__("jmp *(_p+1024)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__256__() { __asm__("jmp *(_p+1024)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__256__()
{
	__asm
	{
		jmp p[256 * 4];
	}
}
#endif
#endif

// glScalef
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__257__() { __asm__("jmp *(_p+1028)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__257__() { __asm__("jmp *(_p+1028)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__257__()
{
	__asm
	{
		jmp p[257 * 4];
	}
}
#endif
#endif

// glScissor
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__258__() { __asm__("jmp *(_p+1032)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__258__() { __asm__("jmp *(_p+1032)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__258__()
{
	__asm
	{
		jmp p[258 * 4];
	}
}
#endif
#endif

// glSelectBuffer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__259__() { __asm__("jmp *(_p+1036)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__259__() { __asm__("jmp *(_p+1036)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__259__()
{
	__asm
	{
		jmp p[259 * 4];
	}
}
#endif
#endif

// glShadeModel
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__260__() { __asm__("jmp *(_p+1040)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__260__() { __asm__("jmp *(_p+1040)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__260__()
{
	__asm
	{
		jmp p[260 * 4];
	}
}
#endif
#endif

// glStencilFunc
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__261__() { __asm__("jmp *(_p+1044)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__261__() { __asm__("jmp *(_p+1044)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__261__()
{
	__asm
	{
		jmp p[261 * 4];
	}
}
#endif
#endif

// glStencilMask
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__262__() { __asm__("jmp *(_p+1048)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__262__() { __asm__("jmp *(_p+1048)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__262__()
{
	__asm
	{
		jmp p[262 * 4];
	}
}
#endif
#endif

// glStencilOp
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__263__() { __asm__("jmp *(_p+1052)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__263__() { __asm__("jmp *(_p+1052)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__263__()
{
	__asm
	{
		jmp p[263 * 4];
	}
}
#endif
#endif

// glTexCoord1d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__264__() { __asm__("jmp *(_p+1056)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__264__() { __asm__("jmp *(_p+1056)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__264__()
{
	__asm
	{
		jmp p[264 * 4];
	}
}
#endif
#endif

// glTexCoord1dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__265__() { __asm__("jmp *(_p+1060)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__265__() { __asm__("jmp *(_p+1060)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__265__()
{
	__asm
	{
		jmp p[265 * 4];
	}
}
#endif
#endif

// glTexCoord1f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__266__() { __asm__("jmp *(_p+1064)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__266__() { __asm__("jmp *(_p+1064)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__266__()
{
	__asm
	{
		jmp p[266 * 4];
	}
}
#endif
#endif

// glTexCoord1fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__267__() { __asm__("jmp *(_p+1068)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__267__() { __asm__("jmp *(_p+1068)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__267__()
{
	__asm
	{
		jmp p[267 * 4];
	}
}
#endif
#endif

// glTexCoord1i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__268__() { __asm__("jmp *(_p+1072)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__268__() { __asm__("jmp *(_p+1072)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__268__()
{
	__asm
	{
		jmp p[268 * 4];
	}
}
#endif
#endif

// glTexCoord1iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__269__() { __asm__("jmp *(_p+1076)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__269__() { __asm__("jmp *(_p+1076)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__269__()
{
	__asm
	{
		jmp p[269 * 4];
	}
}
#endif
#endif

// glTexCoord1s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__270__() { __asm__("jmp *(_p+1080)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__270__() { __asm__("jmp *(_p+1080)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__270__()
{
	__asm
	{
		jmp p[270 * 4];
	}
}
#endif
#endif

// glTexCoord1sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__271__() { __asm__("jmp *(_p+1084)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__271__() { __asm__("jmp *(_p+1084)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__271__()
{
	__asm
	{
		jmp p[271 * 4];
	}
}
#endif
#endif

// glTexCoord2d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__272__() { __asm__("jmp *(_p+1088)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__272__() { __asm__("jmp *(_p+1088)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__272__()
{
	__asm
	{
		jmp p[272 * 4];
	}
}
#endif
#endif

// glTexCoord2dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__273__() { __asm__("jmp *(_p+1092)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__273__() { __asm__("jmp *(_p+1092)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__273__()
{
	__asm
	{
		jmp p[273 * 4];
	}
}
#endif
#endif

// glTexCoord2f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__274__() { __asm__("jmp *(_p+1096)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__274__() { __asm__("jmp *(_p+1096)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__274__()
{
	__asm
	{
		jmp p[274 * 4];
	}
}
#endif
#endif

// glTexCoord2fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__275__() { __asm__("jmp *(_p+1100)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__275__() { __asm__("jmp *(_p+1100)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__275__()
{
	__asm
	{
		jmp p[275 * 4];
	}
}
#endif
#endif

// glTexCoord2i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__276__() { __asm__("jmp *(_p+1104)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__276__() { __asm__("jmp *(_p+1104)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__276__()
{
	__asm
	{
		jmp p[276 * 4];
	}
}
#endif
#endif

// glTexCoord2iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__277__() { __asm__("jmp *(_p+1108)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__277__() { __asm__("jmp *(_p+1108)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__277__()
{
	__asm
	{
		jmp p[277 * 4];
	}
}
#endif
#endif

// glTexCoord2s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__278__() { __asm__("jmp *(_p+1112)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__278__() { __asm__("jmp *(_p+1112)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__278__()
{
	__asm
	{
		jmp p[278 * 4];
	}
}
#endif
#endif

// glTexCoord2sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__279__() { __asm__("jmp *(_p+1116)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__279__() { __asm__("jmp *(_p+1116)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__279__()
{
	__asm
	{
		jmp p[279 * 4];
	}
}
#endif
#endif

// glTexCoord3d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__280__() { __asm__("jmp *(_p+1120)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__280__() { __asm__("jmp *(_p+1120)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__280__()
{
	__asm
	{
		jmp p[280 * 4];
	}
}
#endif
#endif

// glTexCoord3dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__281__() { __asm__("jmp *(_p+1124)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__281__() { __asm__("jmp *(_p+1124)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__281__()
{
	__asm
	{
		jmp p[281 * 4];
	}
}
#endif
#endif

// glTexCoord3f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__282__() { __asm__("jmp *(_p+1128)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__282__() { __asm__("jmp *(_p+1128)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__282__()
{
	__asm
	{
		jmp p[282 * 4];
	}
}
#endif
#endif

// glTexCoord3fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__283__() { __asm__("jmp *(_p+1132)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__283__() { __asm__("jmp *(_p+1132)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__283__()
{
	__asm
	{
		jmp p[283 * 4];
	}
}
#endif
#endif

// glTexCoord3i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__284__() { __asm__("jmp *(_p+1136)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__284__() { __asm__("jmp *(_p+1136)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__284__()
{
	__asm
	{
		jmp p[284 * 4];
	}
}
#endif
#endif

// glTexCoord3iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__285__() { __asm__("jmp *(_p+1140)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__285__() { __asm__("jmp *(_p+1140)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__285__()
{
	__asm
	{
		jmp p[285 * 4];
	}
}
#endif
#endif

// glTexCoord3s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__286__() { __asm__("jmp *(_p+1144)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__286__() { __asm__("jmp *(_p+1144)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__286__()
{
	__asm
	{
		jmp p[286 * 4];
	}
}
#endif
#endif

// glTexCoord3sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__287__() { __asm__("jmp *(_p+1148)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__287__() { __asm__("jmp *(_p+1148)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__287__()
{
	__asm
	{
		jmp p[287 * 4];
	}
}
#endif
#endif

// glTexCoord4d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__288__() { __asm__("jmp *(_p+1152)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__288__() { __asm__("jmp *(_p+1152)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__288__()
{
	__asm
	{
		jmp p[288 * 4];
	}
}
#endif
#endif

// glTexCoord4dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__289__() { __asm__("jmp *(_p+1156)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__289__() { __asm__("jmp *(_p+1156)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__289__()
{
	__asm
	{
		jmp p[289 * 4];
	}
}
#endif
#endif

// glTexCoord4f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__290__() { __asm__("jmp *(_p+1160)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__290__() { __asm__("jmp *(_p+1160)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__290__()
{
	__asm
	{
		jmp p[290 * 4];
	}
}
#endif
#endif

// glTexCoord4fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__291__() { __asm__("jmp *(_p+1164)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__291__() { __asm__("jmp *(_p+1164)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__291__()
{
	__asm
	{
		jmp p[291 * 4];
	}
}
#endif
#endif

// glTexCoord4i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__292__() { __asm__("jmp *(_p+1168)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__292__() { __asm__("jmp *(_p+1168)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__292__()
{
	__asm
	{
		jmp p[292 * 4];
	}
}
#endif
#endif

// glTexCoord4iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__293__() { __asm__("jmp *(_p+1172)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__293__() { __asm__("jmp *(_p+1172)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__293__()
{
	__asm
	{
		jmp p[293 * 4];
	}
}
#endif
#endif

// glTexCoord4s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__294__() { __asm__("jmp *(_p+1176)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__294__() { __asm__("jmp *(_p+1176)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__294__()
{
	__asm
	{
		jmp p[294 * 4];
	}
}
#endif
#endif

// glTexCoord4sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__295__() { __asm__("jmp *(_p+1180)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__295__() { __asm__("jmp *(_p+1180)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__295__()
{
	__asm
	{
		jmp p[295 * 4];
	}
}
#endif
#endif

// glTexCoordPointer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__296__() { __asm__("jmp *(_p+1184)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__296__() { __asm__("jmp *(_p+1184)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__296__()
{
	__asm
	{
		jmp p[296 * 4];
	}
}
#endif
#endif

// glTexEnvf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__297__() { __asm__("jmp *(_p+1188)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__297__() { __asm__("jmp *(_p+1188)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__297__()
{
	__asm
	{
		jmp p[297 * 4];
	}
}
#endif
#endif

// glTexEnvfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__298__() { __asm__("jmp *(_p+1192)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__298__() { __asm__("jmp *(_p+1192)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__298__()
{
	__asm
	{
		jmp p[298 * 4];
	}
}
#endif
#endif

// glTexEnvi
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__299__() { __asm__("jmp *(_p+1196)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__299__() { __asm__("jmp *(_p+1196)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__299__()
{
	__asm
	{
		jmp p[299 * 4];
	}
}
#endif
#endif

// glTexEnviv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__300__() { __asm__("jmp *(_p+1200)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__300__() { __asm__("jmp *(_p+1200)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__300__()
{
	__asm
	{
		jmp p[300 * 4];
	}
}
#endif
#endif

// glTexGend
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__301__() { __asm__("jmp *(_p+1204)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__301__() { __asm__("jmp *(_p+1204)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__301__()
{
	__asm
	{
		jmp p[301 * 4];
	}
}
#endif
#endif

// glTexGendv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__302__() { __asm__("jmp *(_p+1208)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__302__() { __asm__("jmp *(_p+1208)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__302__()
{
	__asm
	{
		jmp p[302 * 4];
	}
}
#endif
#endif

// glTexGenf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__303__() { __asm__("jmp *(_p+1212)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__303__() { __asm__("jmp *(_p+1212)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__303__()
{
	__asm
	{
		jmp p[303 * 4];
	}
}
#endif
#endif

// glTexGenfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__304__() { __asm__("jmp *(_p+1216)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__304__() { __asm__("jmp *(_p+1216)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__304__()
{
	__asm
	{
		jmp p[304 * 4];
	}
}
#endif
#endif

// glTexGeni
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__305__() { __asm__("jmp *(_p+1220)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__305__() { __asm__("jmp *(_p+1220)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__305__()
{
	__asm
	{
		jmp p[305 * 4];
	}
}
#endif
#endif

// glTexGeniv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__306__() { __asm__("jmp *(_p+1224)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__306__() { __asm__("jmp *(_p+1224)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__306__()
{
	__asm
	{
		jmp p[306 * 4];
	}
}
#endif
#endif

// glTexImage1D
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__307__() { __asm__("jmp *(_p+1228)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__307__() { __asm__("jmp *(_p+1228)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__307__()
{
	__asm
	{
		jmp p[307 * 4];
	}
}
#endif
#endif

// glTexImage2D
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__308__() { __asm__("jmp *(_p+1232)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__308__() { __asm__("jmp *(_p+1232)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__308__()
{
	__asm
	{
		jmp p[308 * 4];
	}
}
#endif
#endif

// glTexParameterf
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__309__() { __asm__("jmp *(_p+1236)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__309__() { __asm__("jmp *(_p+1236)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__309__()
{
	__asm
	{
		jmp p[309 * 4];
	}
}
#endif
#endif

// glTexParameterfv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__310__() { __asm__("jmp *(_p+1240)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__310__() { __asm__("jmp *(_p+1240)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__310__()
{
	__asm
	{
		jmp p[310 * 4];
	}
}
#endif
#endif

// glTexParameteri
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__311__() { __asm__("jmp *(_p+1244)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__311__() { __asm__("jmp *(_p+1244)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__311__()
{
	__asm
	{
		jmp p[311 * 4];
	}
}
#endif
#endif

// glTexParameteriv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__312__() { __asm__("jmp *(_p+1248)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__312__() { __asm__("jmp *(_p+1248)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__312__()
{
	__asm
	{
		jmp p[312 * 4];
	}
}
#endif
#endif

// glTexSubImage1D
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__313__() { __asm__("jmp *(_p+1252)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__313__() { __asm__("jmp *(_p+1252)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__313__()
{
	__asm
	{
		jmp p[313 * 4];
	}
}
#endif
#endif

// glTexSubImage2D
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__314__() { __asm__("jmp *(_p+1256)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__314__() { __asm__("jmp *(_p+1256)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__314__()
{
	__asm
	{
		jmp p[314 * 4];
	}
}
#endif
#endif

// glTranslated
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__315__() { __asm__("jmp *(_p+1260)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__315__() { __asm__("jmp *(_p+1260)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__315__()
{
	__asm
	{
		jmp p[315 * 4];
	}
}
#endif
#endif

// glTranslatef
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__316__() { __asm__("jmp *(_p+1264)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__316__() { __asm__("jmp *(_p+1264)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__316__()
{
	__asm
	{
		jmp p[316 * 4];
	}
}
#endif
#endif

// glVertex2d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__317__() { __asm__("jmp *(_p+1268)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__317__() { __asm__("jmp *(_p+1268)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__317__()
{
	__asm
	{
		jmp p[317 * 4];
	}
}
#endif
#endif

// glVertex2dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__318__() { __asm__("jmp *(_p+1272)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__318__() { __asm__("jmp *(_p+1272)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__318__()
{
	__asm
	{
		jmp p[318 * 4];
	}
}
#endif
#endif

// glVertex2f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__319__() { __asm__("jmp *(_p+1276)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__319__() { __asm__("jmp *(_p+1276)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__319__()
{
	__asm
	{
		jmp p[319 * 4];
	}
}
#endif
#endif

// glVertex2fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__320__() { __asm__("jmp *(_p+1280)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__320__() { __asm__("jmp *(_p+1280)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__320__()
{
	__asm
	{
		jmp p[320 * 4];
	}
}
#endif
#endif

// glVertex2i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__321__() { __asm__("jmp *(_p+1284)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__321__() { __asm__("jmp *(_p+1284)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__321__()
{
	__asm
	{
		jmp p[321 * 4];
	}
}
#endif
#endif

// glVertex2iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__322__() { __asm__("jmp *(_p+1288)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__322__() { __asm__("jmp *(_p+1288)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__322__()
{
	__asm
	{
		jmp p[322 * 4];
	}
}
#endif
#endif

// glVertex2s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__323__() { __asm__("jmp *(_p+1292)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__323__() { __asm__("jmp *(_p+1292)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__323__()
{
	__asm
	{
		jmp p[323 * 4];
	}
}
#endif
#endif

// glVertex2sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__324__() { __asm__("jmp *(_p+1296)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__324__() { __asm__("jmp *(_p+1296)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__324__()
{
	__asm
	{
		jmp p[324 * 4];
	}
}
#endif
#endif

// glVertex3d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__325__() { __asm__("jmp *(_p+1300)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__325__() { __asm__("jmp *(_p+1300)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__325__()
{
	__asm
	{
		jmp p[325 * 4];
	}
}
#endif
#endif

// glVertex3dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__326__() { __asm__("jmp *(_p+1304)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__326__() { __asm__("jmp *(_p+1304)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__326__()
{
	__asm
	{
		jmp p[326 * 4];
	}
}
#endif
#endif

// glVertex3f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__327__() { __asm__("jmp *(_p+1308)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__327__() { __asm__("jmp *(_p+1308)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__327__()
{
	__asm
	{
		jmp p[327 * 4];
	}
}
#endif
#endif

// glVertex3fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__328__() { __asm__("jmp *(_p+1312)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__328__() { __asm__("jmp *(_p+1312)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__328__()
{
	__asm
	{
		jmp p[328 * 4];
	}
}
#endif
#endif

// glVertex3i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__329__() { __asm__("jmp *(_p+1316)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__329__() { __asm__("jmp *(_p+1316)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__329__()
{
	__asm
	{
		jmp p[329 * 4];
	}
}
#endif
#endif

// glVertex3iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__330__() { __asm__("jmp *(_p+1320)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__330__() { __asm__("jmp *(_p+1320)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__330__()
{
	__asm
	{
		jmp p[330 * 4];
	}
}
#endif
#endif

// glVertex3s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__331__() { __asm__("jmp *(_p+1324)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__331__() { __asm__("jmp *(_p+1324)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__331__()
{
	__asm
	{
		jmp p[331 * 4];
	}
}
#endif
#endif

// glVertex3sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__332__() { __asm__("jmp *(_p+1328)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__332__() { __asm__("jmp *(_p+1328)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__332__()
{
	__asm
	{
		jmp p[332 * 4];
	}
}
#endif
#endif

// glVertex4d
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__333__() { __asm__("jmp *(_p+1332)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__333__() { __asm__("jmp *(_p+1332)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__333__()
{
	__asm
	{
		jmp p[333 * 4];
	}
}
#endif
#endif

// glVertex4dv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__334__() { __asm__("jmp *(_p+1336)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__334__() { __asm__("jmp *(_p+1336)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__334__()
{
	__asm
	{
		jmp p[334 * 4];
	}
}
#endif
#endif

// glVertex4f
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__335__() { __asm__("jmp *(_p+1340)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__335__() { __asm__("jmp *(_p+1340)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__335__()
{
	__asm
	{
		jmp p[335 * 4];
	}
}
#endif
#endif

// glVertex4fv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__336__() { __asm__("jmp *(_p+1344)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__336__() { __asm__("jmp *(_p+1344)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__336__()
{
	__asm
	{
		jmp p[336 * 4];
	}
}
#endif
#endif

// glVertex4i
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__337__() { __asm__("jmp *(_p+1348)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__337__() { __asm__("jmp *(_p+1348)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__337__()
{
	__asm
	{
		jmp p[337 * 4];
	}
}
#endif
#endif

// glVertex4iv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__338__() { __asm__("jmp *(_p+1352)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__338__() { __asm__("jmp *(_p+1352)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__338__()
{
	__asm
	{
		jmp p[338 * 4];
	}
}
#endif
#endif

// glVertex4s
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__339__() { __asm__("jmp *(_p+1356)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__339__() { __asm__("jmp *(_p+1356)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__339__()
{
	__asm
	{
		jmp p[339 * 4];
	}
}
#endif
#endif

// glVertex4sv
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__340__() { __asm__("jmp *(_p+1360)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__340__() { __asm__("jmp *(_p+1360)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__340__()
{
	__asm
	{
		jmp p[340 * 4];
	}
}
#endif
#endif

// glVertexPointer
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__341__() { __asm__("jmp *(_p+1364)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__341__() { __asm__("jmp *(_p+1364)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__341__()
{
	__asm
	{
		jmp p[341 * 4];
	}
}
#endif
#endif

// glViewport
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__342__() { __asm__("jmp *(_p+1368)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__342__()
{
	__asm
	{
		jmp p[342 * 4];
	}
}
#endif

// wglChoosePixelFormat
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__343__() { __asm__("jmp *(_p+1372)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__343__()
{
	__asm
	{
		jmp p[343 * 4];
	}
}
#endif

// wglCopyContext
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__344__() { __asm__("jmp *(_p+1376)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__344__()
{
	__asm
	{
		jmp p[344 * 4];
	}
}
#endif

// wglCreateContext
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__345__() { __asm__("jmp *(_p+1380)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__345__()
{
	__asm
	{
		jmp p[345 * 4];
	}
}
#endif

// wglCreateLayerContext
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__346__() { __asm__("jmp *(_p+1384)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__346__()
{
	__asm
	{
		jmp p[346 * 4];
	}
}
#endif

// wglDeleteContext
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__347__() { __asm__("jmp *(_p+1388)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__347__()
{
	__asm
	{
		jmp p[347 * 4];
	}
}
#endif

// wglDescribeLayerPlane
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__348__() { __asm__("jmp *(_p+1392)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__348__()
{
	__asm
	{
		jmp p[348 * 4];
	}
}
#endif

// wglDescribePixelFormat
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__349__() { __asm__("jmp *(_p+1396)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__349__()
{
	__asm
	{
		jmp p[349 * 4];
	}
}
#endif

// wglGetCurrentContext
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__350__() { __asm__("jmp *(_p+1400)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__350__()
{
	__asm
	{
		jmp p[350 * 4];
	}
}
#endif

// wglGetCurrentDC
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__351__() { __asm__("jmp *(_p+1404)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__351__()
{
	__asm
	{
		jmp p[351 * 4];
	}
}
#endif

// wglGetDefaultProcAddress
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__352__() { __asm__("jmp *(_p+1408)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__352__()
{
	__asm
	{
		jmp p[352 * 4];
	}
}
#endif

// wglGetLayerPaletteEntries
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__353__() { __asm__("jmp *(_p+1412)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__353__()
{
	__asm
	{
		jmp p[353 * 4];
	}
}
#endif

// wglGetPixelFormat
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__354__() { __asm__("jmp *(_p+1416)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__354__()
{
	__asm
	{
		jmp p[354 * 4];
	}
}
#endif

// wglGetProcAddress
/*extern "C" __declspec(naked) void __stdcall __E__355__()
	{// INIT_GL
	__asm
	{
	jmp p[355*4];
	}
	}*/

extern "C" PROC WINAPI __E__355__( const char *name )
{
	if( (strcmp(name, "glProgramString") == 0) ||
		(strcmp(name, "glProgramStringARB") == 0) /*||
		(strcmp(name, "glProgramStringATI") == 0) ||
		(strcmp(name, "glProgramStringNV") == 0)*/ )
	{
		orig_glProgramString = (PFNGLPROGRAMSTRINGARBPROC)orig_wglGetProcAddress( name );
		return (PROC)my_glProgramString;
	}
	else if( (strcmp(name, "glBindProgram") == 0) ||
		(strcmp(name, "glBindProgramARB") == 0) )
	{
		orig_glProgramEnvParameter4d = (PFNGLPROGRAMENVPARAMETER4DARBPROC)orig_wglGetProcAddress( "glProgramEnvParameter4dARB" );
		orig_glBindProgram = (PFNGLBINDPROGRAMARBPROC)orig_wglGetProcAddress( name );
		return (PROC)my_glBindProgram;
	}

	PROC depthIntercept = DepthCapture_InterceptProc( name );
	if( depthIntercept ) return depthIntercept;

	return orig_wglGetProcAddress( name );
}


// wglMakeCurrent
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__356__() { __asm__("jmp *(_p+1424)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__356__()
{
	__asm
	{
		jmp p[356 * 4];
	}
}
#endif

// wglRealizeLayerPalette
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__357__() { __asm__("jmp *(_p+1428)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__357__() { __asm__("jmp *(_p+1428)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__357__()
{
	__asm
	{
		jmp p[357 * 4];
	}
}
#endif
#endif

// wglSetLayerPaletteEntries
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__358__() { __asm__("jmp *(_p+1432)"); }
#else
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__358__() { __asm__("jmp *(_p+1432)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__358__()
{
	__asm
	{
		jmp p[358 * 4];
	}
}
#endif
#endif

// wglSetPixelFormat
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__359__() { __asm__("jmp *(_p+1436)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__359__()
{
	__asm
	{
		jmp p[359 * 4];
	}
}
#endif

// wglShareLists
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__360__() { __asm__("jmp *(_p+1440)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__360__()
{
	__asm
	{
		jmp p[360 * 4];
	}
}
#endif

// wglSwapBuffers
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__361__() { __asm__("jmp *(_p+1444)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__361__()
{
	__asm
	{
		jmp p[361 * 4];
	}
}
#endif

// wglSwapLayerBuffers
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__362__() { __asm__("jmp *(_p+1448)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__362__()
{
	__asm
	{
		jmp p[362 * 4];
	}
}
#endif

// wglSwapMultipleBuffers
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__363__() { __asm__("jmp *(_p+1452)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__363__()
{
	__asm
	{
		jmp p[363 * 4];
	}
}
#endif

// wglUseFontBitmapsA
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__364__() { __asm__("jmp *(_p+1456)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__364__()
{
	__asm
	{
		jmp p[364 * 4];
	}
}
#endif

// wglUseFontBitmapsW
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__365__() { __asm__("jmp *(_p+1460)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__365__()
{
	__asm
	{
		jmp p[365 * 4];
	}
}
#endif

// wglUseFontOutlinesA
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__366__() { __asm__("jmp *(_p+1464)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__366__()
{
	__asm
	{
		jmp p[366 * 4];
	}
}
#endif

// wglUseFontOutlinesW
#ifdef __GNUC__
extern "C" __attribute__((naked)) void __stdcall __E__367__() { __asm__("jmp *(_p+1468)"); }
#else
extern "C" __declspec(naked) void __stdcall __E__367__()
{
	__asm
	{
		jmp p[367 * 4];
	}
}
#endif

