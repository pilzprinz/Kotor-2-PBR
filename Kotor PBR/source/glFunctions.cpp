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

#define LIBRARY_DEF
#include "glfunctions.h"

#include <fstream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "md5.h"
#include "gl_state_capture.h"
#include "file_logger.h"

#ifndef GL_FRAGMENT_PROGRAM_BINDING_ARB
#define GL_FRAGMENT_PROGRAM_BINDING_ARB 0x8873
#endif
/*  derived from the RSA Data
Security, Inc. MD5 Message-Digest Algorithm */

#define PROGRAM_ENV_FOG_INDEX	8
#define PROGRAM_ENV_FOG_ANIM_INDEX 9
#define PROGRAM_ENV_VIEWPORT_SIZE 10

class Win32File
{
	HANDLE hFile;
public:
	Win32File( std::string name, DWORD dwAccess, DWORD dwCreationDisposition ) { 
		hFile = CreateFile( name.c_str(), dwAccess, 0, 0, dwCreationDisposition, 0, 0 ); 
	}
	~Win32File() { if( isOpen() ) CloseHandle( hFile ); }

	bool isOpen() { return hFile != INVALID_HANDLE_VALUE; }
	bool write( const char *pointer, unsigned length ) { 
		DWORD t; return isOpen() && WriteFile( hFile, pointer, length, &t, 0 ) != FALSE;
	}
	bool readAll( std::string &contents ) { 
		DWORD t = 0;
		if( isOpen() ) {
			DWORD szFile = GetFileSize( hFile, 0 );
			char *buffer = (char *)HeapAlloc( GetProcessHeap(), 0, szFile );
			if( buffer ) {
				t = ReadFile( hFile, buffer, szFile, &t, 0 );
				if( t != FALSE ) {
					contents.assign( &buffer[0], &buffer[szFile] );
				}

				HeapFree( GetProcessHeap(), 0, buffer );
			}
		}

		return t != 0;
	}
};

typedef std::map< std::string, std::string > type_names;
type_names names;

// Set of fragment-program IDs whose source we replaced with a chars/objects
// PBR override (friendly name starts with "fp_model"). Used to trigger a
// mid-frame depth snapshot exactly before the engine binds one of these
// programs to draw characters — gives them current-frame depth in TMU 7.
static std::unordered_set<GLuint> s_objFpIds;

// id → friendly name (e.g. "fp_worldtex_env_reflective", "vp_static_displaced") for
// every ARB program uploaded. Used by shadow_map histogram + diagnostics.
static std::unordered_map<GLuint, std::string> s_progIdToName;

const char *GetProgramName(GLuint id)
{
    std::unordered_map<GLuint, std::string>::const_iterator it = s_progIdToName.find(id);
    return (it != s_progIdToName.end()) ? it->second.c_str() : "";
}

void InitShaderLookup()
{
	std::ifstream file( "shader_ident.txt", std::ios::in );

	while( file.good() )
	{
		std::string line;
		std::getline( file, line );
		std::string::size_type pos = line.find( '=' );

		if( pos != std::string::npos )
			names[ line.substr( 0, pos ) ] = line.substr( pos + 1 );
	}
}

// Expand "# @include <name>" lines in shader source with the contents of
// shaders_override/<name>.inc. Lets a shared block (the directional-shadow sample)
// live in ONE file instead of being copy-pasted into every receiver FP — the drift
// between those copies is what produced the model-path shadow bug. The marker sits
// in an ARB comment ("#"), so a missing .inc leaves a harmless comment (logged) and
// the program still compiles. ARB has no native include, hence this text splice.
static void ExpandShaderIncludes( std::string &src )
{
	const std::string marker = "@include ";
	std::string::size_type pos = 0;
	while( ( pos = src.find( marker, pos ) ) != std::string::npos )
	{
		// Must be inside a comment: a '#' must precede the marker on this line.
		std::string::size_type ls        = src.rfind( '\n', pos );
		std::string::size_type lineStart  = ( ls == std::string::npos ) ? 0 : ls + 1;
		std::string::size_type hashPos    = src.find( '#', lineStart );
		if( hashPos == std::string::npos || hashPos > pos ) { pos += marker.size(); continue; }

		std::string::size_type eol     = src.find( '\n', pos );
		std::string::size_type lineEnd = ( eol == std::string::npos ) ? src.size() : eol;
		std::string name = src.substr( pos + marker.size(), lineEnd - ( pos + marker.size() ) );
		while( !name.empty() && ( name[name.size()-1]=='\r' || name[name.size()-1]==' ' || name[name.size()-1]=='\t' ) )
			name.erase( name.size()-1 );
		while( !name.empty() && ( name[0]==' ' || name[0]=='\t' ) )
			name.erase( 0, 1 );

		std::string incText;
		Win32File inc( "shaders_override/" + name + ".inc", GENERIC_READ, OPEN_EXISTING );
		if( inc.readAll( incText ) )
		{
			src.replace( lineStart, lineEnd - lineStart, incText );
			pos = lineStart + incText.size();
		}
		else
		{
			std::string warn = "SHADER|include MISSING|" + name + ".inc\n";
			PbrLogLine( warn.c_str() );
			pos = lineEnd;
		}
	}
}

void __stdcall my_glProgramString( GLenum target, GLenum format, GLsizei len, const void *string )
{
	std::string hash = md5( std::string( (char*)string, &((char*)string)[len] ) );
	hash.insert( 0, target == GL_FRAGMENT_PROGRAM_ARB ? "fp" : ( target == GL_VERTEX_PROGRAM_ARB ? "vp" : "un" ) );

	// Use find() instead of operator[] — operator[] inserts an empty string
	// for every unknown hash, growing the map unboundedly with one entry per
	// unique shader upload (hundreds across a playthrough).
	type_names::iterator it = names.find( hash );
	if( it != names.end() && it->second.length() > 0 )
		hash = it->second;

	// Record program id → friendly (or hash) name. The just-uploaded source
	// lands in whatever program is currently bound to `target`.
	#ifndef GL_VERTEX_PROGRAM_BINDING_ARB
	#define GL_VERTEX_PROGRAM_BINDING_ARB 0x864A
	#endif
	if( orig_glGetIntegerv ) {
		GLenum bindEnum = (target == GL_FRAGMENT_PROGRAM_ARB) ? GL_FRAGMENT_PROGRAM_BINDING_ARB
		                                                       : GL_VERTEX_PROGRAM_BINDING_ARB;
		GLint curId = 0;
		orig_glGetIntegerv( bindEnum, &curId );
		if( curId > 0 ) s_progIdToName[ (GLuint)curId ] = hash;
	}

	CreateDirectory( "shaders_original", 0 );
	Win32File originalFile( "shaders_original/" + hash + ".txt", GENERIC_WRITE, CREATE_ALWAYS );
	originalFile.write( (const char *)string, len );

	CreateDirectory( "shaders_override", 0 );

	Win32File overrideFile( "shaders_override/" + hash + ".txt", GENERIC_READ, OPEN_EXISTING );
	std::string contents;

	if( overrideFile.readAll( contents ) )
	{
		ExpandShaderIncludes( contents );   // splice in shaders_override/<name>.inc at "# @include <name>"
		std::string msg = "SHADER|override|" + hash + "\n";
		PbrLogLine(msg.c_str());
		orig_glProgramString( target, format, contents.length(), contents.c_str() );

		// Tag this program ID if it's a character/object FP override so the
		// matching glBindProgram can trigger a mid-frame depth snapshot. The
		// program currently bound to GL_FRAGMENT_PROGRAM_ARB receives the
		// uploaded source.
		if( target == GL_FRAGMENT_PROGRAM_ARB && hash.compare(0, 8, "fp_model") == 0 )
		{
			GLint curId = 0;
			if( orig_glGetIntegerv )
				orig_glGetIntegerv( GL_FRAGMENT_PROGRAM_BINDING_ARB, &curId );
			char dbg[160];
			if( curId > 0 ) {
				s_objFpIds.insert( (GLuint)curId );
				snprintf(dbg, sizeof(dbg),
				         "[obj-fp] tagged id=%d for %s (total=%zu)\n",
				         curId, hash.c_str(), s_objFpIds.size());
			} else {
				snprintf(dbg, sizeof(dbg),
				         "[obj-fp] FAILED tag for %s (curId=%d glGetIntegerv=%p)\n",
				         hash.c_str(), curId, (void*)orig_glGetIntegerv);
			}
			PbrLogLine(dbg);
		}
		return;
	}

	return orig_glProgramString( target, format, len, string );
}

void __stdcall my_glBindProgram( GLenum target, GLuint program )
{
	orig_glBindProgram( target, program );

	if( target == GL_FRAGMENT_PROGRAM_ARB )
	{
		// If the engine is about to draw using a character/object PBR FP,
		// take a mid-frame depth snapshot now (idempotent per frame) so the
		// FP samples *current-frame* world depth in TMU 7, not the stale
		// previous-frame depth from the swap-time fallback.
		if( s_objFpIds.find(program) != s_objFpIds.end() )
			GlState_TriggerCharSnapshotIfNeeded();

		if( bFogOn )
			orig_glProgramEnvParameter4d( GL_FRAGMENT_PROGRAM_ARB, PROGRAM_ENV_FOG_INDEX,
				fogBehavior[0], fogBehavior[1], fogBehavior[2], fogBehavior[3] );
		else
			orig_glProgramEnvParameter4d( GL_FRAGMENT_PROGRAM_ARB, PROGRAM_ENV_FOG_INDEX, 0.0, 0.0, 0.0, 1.0 );

		GLint viewport[4];
		orig_glGetIntegerv( GL_VIEWPORT, viewport );
		orig_glProgramEnvParameter4d( GL_FRAGMENT_PROGRAM_ARB, PROGRAM_ENV_VIEWPORT_SIZE,
			viewport[2], viewport[3], 1.0 / ((double)viewport[2]), 1.0 / ((double)viewport[3]) );
	}
}

void fogRecalculate()
{	
	// {DENSITY/LN(2), DENSITY/SQRT(LN(2)), -1/(END-START), END/(END-START)}

	double endStart = fogEnd - fogStart;

	switch( fogMode )
	{
	case GL_LINEAR:
		fogBehavior[0] = 0;
		fogBehavior[1] = 0;
		if( endStart != 0 )
		{
			fogBehavior[2] = -1 / endStart;
			fogBehavior[3] = fogEnd / endStart;
		}
		else
		{
			fogBehavior[2] = 0;
			fogBehavior[3] = 1;
		}
		break;
	case GL_EXP:
		fogBehavior[0] = fogDensity / 0.693147180559945;
		fogBehavior[1] = 0;
		fogBehavior[2] = 0;
		fogBehavior[3] = 1;
		break;
	case GL_EXP2:
		fogBehavior[0] = 0;
		fogBehavior[1] = fogDensity / 0.832554611157697;
		fogBehavior[2] = 0;
		fogBehavior[3] = 1;
		break;
	};

	/*
	PARAM p = program.env[8];
	TEMP fogFactor;
	MUL fogFactor.x, p.x, fragment.fogcoord.x;
	EX2_SAT fogFactor.x, -fogFactor.x;
	MUL fogFactor.y, p.y, fragment.fogcoord.x;
	MUL fogFactor.y, fogFactor.y, fogFactor.y;
	EX2_SAT fogFactor.y, -fogFactor.y;
	MAD_SAT fogFactor.z, p.z, fragment.fogcoord.x, p.w;
	MUL fogFactor.x, fogFactor.y, fogFactor.x;
	MUL fogFactor.x, fogFactor.z, fogFactor.x;
	LRP r0.rgb, fogFactor.x, r0, state.fog.color;
	MOV result.color, r0;
	*/
}