ShaderOverride by HappyFunTimes copyright (c) 2016
(derivative of gShaderReplacer by psycholns copyright (c) 2015 and
  of the "RSA Data Security, Inc. MD5 Message-Digest Algorithm" by Frank Thilo and RSA Data Security, Inc. copyright (c) 1991-2)
(makes use of 7-Zip copyright (c) 1999-2010 Igor Pavlov in compilation)

See LICENSE_*.txt for licenses.

#### Overview
This DLL was produced specifically for Star Wars: Knights of the Old Republic II: The Sith Lords to fix issues in 2016 with the fog.

#### Usage
When glProgramSource is called from within a program that uses it, the DLL will produce two directories: "shaders_original" and "shaders_override".  It will then proceed to dump the shaders to the "shaders_original" directory with filenames in the form of (vp/fp)(md5_shader_hash).txt.

As of version "speedblur" there is a "shader_ident.txt" file that is referenced to convert the md5 hash names into more friendly, readable names.  If you figure out what a shader does, feel free to add it this file.

The DLL will then check to see if there is a file with a matching name in the "shaders_override" directory and load it instead.  If not, it will use the original shader.

Copy any files that you want to change from original to override and make changes there.  The orignal gets overwritten every run.

#### Changes

##### Fog Issue
It seems that the fog is not processed in certain areas of the game.  There is information pointing to the idea that fog will not be handled if a vertex shader is used, requiring manual handling of the fog effect.  This should be able to be copied/pasted into most fragment shaders (fp_______.txt) right before the last "MOV result.color, r0" statement:

```
# == manual interpolation for fog ==
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
```

(This code adapted from opengl ARB_vertex_program fog reference implementation.)

Fog is tracked manually by hooking a number of glFog* functions with required values placed into environment variable 8 (which is believed to be unused).  While there are the ARB_fog_* options, I am unaware of how to both specify which one to use or turn it off when you don't need fog from within the shader.

##### Speed Blur

The blur on the speed run effect has been changed from an overlay to just a stretching around the edges.  The environment variable 10 is used to store window size and pre-calculated values.

The associated file is in "shaders_override/fp_speedblur.txt".  Remove or do not place this file to remove this change.

#### Source Code
Compilation makes use of the 7-Zip program by Igor Pavlov to add a binary resource to the DLL of the source in a 7z archive.  Due to the way 7-Zip handles executables/DLLs, this DLL can simply be opened by the 7-Zip program as if it was an archive to obtain the source code.

When recompiling, you may have to adjust the path in the Pre Build event to the 7-Zip installation directory.

#### Bugs
Not aware of any.  If the shaders cause issues, change them.

#### Resources
GLIntercept module ShaderEditor
  (https://github.com/dtrebilco/glintercept)
  to identify problem shaders and test new code.
Shader Assembly Language (ARB/NV) Quick Reference Guide for OpenGL®  
  (http://www.renderguild.com/gpuguide.pdf currently)
  good GL shader assembly reference.
ARB_vertex_program
  (http://oss.sgi.com/projects/ogl-sample/registry/ARB/fragment_program.txt)
  fog reference implementations