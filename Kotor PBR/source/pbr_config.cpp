/*
PBR mod configuration — implementation.

Loads pbr_config.ini from the DLL directory using GetPrivateProfileStringA.
All settings have safe defaults so the mod works even if the INI is missing.
*/

#include <windows.h>
#include "pbr_config.h"
#include <stdio.h>
#include <string.h>

PbrConfig g_config;

static void GetDllDirPath(char *out, size_t outSize)
{
    HMODULE hMod = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&GetDllDirPath, &hMod);
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(hMod, path, MAX_PATH);
    char *slash = strrchr(path, '\\');
    if (slash) slash[1] = 0;
    else path[0] = 0;
    snprintf(out, outSize, "%s", path);
}

static int ReadInt(const char *ini, const char *section, const char *key, int defVal)
{
    char buf[32];
    buf[0] = 0;
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), ini);
    if (!buf[0]) return defVal;
    // Support hex prefix 0x
    if (buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X'))
        return (int)strtol(buf, NULL, 16);
    return atoi(buf);
}

static bool ReadBool(const char *ini, const char *section, const char *key, bool defVal)
{
    return ReadInt(ini, section, key, defVal ? 1 : 0) != 0;
}

void PbrConfig_Load()
{
    // Safe defaults — mod works even with no INI
    g_config.enablePBR          = true;
    g_config.enableShadows      = true;
    g_config.enableDepthCapture = false;
    g_config.enableFileLogger   = true;
    g_config.enableGlslPilot    = false;
    g_config.enableTuneOverlay  = true;

    g_config.logLevel = 1;
    g_config.logToFile = true;
    g_config.logToDiag = true;

    g_config.showStats         = false;
    g_config.statsHotkey       = 0x23;  // VK_END
    g_config.shadowMaxCache    = 6000;
    g_config.siblingCacheLimit = 500;

    g_config.exceptionHandler  = true;

    char dir[MAX_PATH];
    GetDllDirPath(dir, sizeof(dir));
    char ini[MAX_PATH];
    snprintf(ini, sizeof(ini), "%spbr_config.ini", dir);

    // Quick existence check — GetPrivateProfileString silently returns defaults
    // if the file doesn't exist, but we want to log whether we found it.
    DWORD attr = GetFileAttributesA(ini);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        // No config file — use defaults. This is fine.
        return;
    }

    g_config.enablePBR          = ReadBool(ini, "general", "enable_pbr",          true);
    g_config.enableShadows      = ReadBool(ini, "general", "enable_shadows",      true);
    g_config.enableDepthCapture = ReadBool(ini, "general", "enable_depth_capture", false);
    g_config.enableFileLogger   = ReadBool(ini, "general", "enable_file_logger",   true);
    g_config.enableGlslPilot    = ReadBool(ini, "general", "enable_glsl_pilot",    false);
    g_config.enableTuneOverlay  = ReadBool(ini, "general", "enable_tune_overlay",  true);

    g_config.logLevel  = ReadInt(ini, "logging", "log_level",   1);
    g_config.logToFile = ReadBool(ini, "logging", "log_to_file",  true);
    g_config.logToDiag = ReadBool(ini, "logging", "log_to_diag",  true);

    g_config.showStats         = ReadBool(ini, "performance", "show_stats",          false);
    g_config.statsHotkey       = ReadInt(ini, "performance", "stats_hotkey",        0x23);
    g_config.shadowMaxCache    = ReadInt(ini, "performance", "shadow_max_cache",    6000);
    g_config.siblingCacheLimit = ReadInt(ini, "performance", "sibling_cache_limit", 500);

    g_config.exceptionHandler  = ReadBool(ini, "safety", "exception_handler", true);
}

bool PbrConfig_ShouldLog(int level)
{
    return g_config.logLevel >= level;
}

// Vectored Exception Handler — logs the exception code via OutputDebugString
// before the game's own handler runs. We return EXCEPTION_CONTINUE_SEARCH so
// the game's handler (or the default unhandled-exception filter) still runs;
// we just wanted to log the crash for diagnosis.
LONG WINAPI PbrVectoredHandler(PEXCEPTION_POINTERS ep)
{
    if (!ep) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Only log fatal exceptions (skip C++ exceptions, breakpoints, single-step)
    if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    char buf[256];
    snprintf(buf, sizeof(buf),
        "[pbr] VEH: exception 0x%08X at 0x%p\n",
        (unsigned int)code, ep->ExceptionRecord->ExceptionAddress);
    OutputDebugStringA(buf);
    return EXCEPTION_CONTINUE_SEARCH;
}
