/*
IAT hook utility for ShaderOverride PBR.
Patches Import Address Table of swkotor2.exe to redirect kernel32 API calls.
*/

#ifndef IAT_HOOK_H
#define IAT_HOOK_H

#include <windows.h>

// Replace IAT entry for (moduleName, functionName) in the target module's import table.
// Returns previous function pointer on success, NULL on failure.
// Target module is identified by hTarget (use GetModuleHandle(NULL) for the main exe).
FARPROC IatHook(HMODULE hTarget, const char *moduleName, const char *functionName, FARPROC newFunc);

#endif
