/*
PBR mod configuration system.

Loads pbr_config.ini from the DLL directory at startup. Provides runtime flags
to enable/disable subsystems without recompiling. All settings have safe defaults
so the mod works even if the INI is missing or malformed.

INI format (pbr_config.ini):
  [general]
  enable_pbr=1           ; PBR texture hooks + sibling loading
  enable_shadows=1       ; directional sun shadow map
  enable_depth_capture=0 ; depth buffer snapshot (currently disabled)
  enable_file_logger=1   ; CreateFile IAT hook + path logging
  enable_glsl_pilot=0    ; GLSL material replacement pilot
  enable_tune_overlay=1  ; in-game DEL tuning overlay

  [logging]
  log_level=1            ; 0=off, 1=normal, 2=verbose
  log_to_file=1          ; pbr_file_log.txt
  log_to_diag=1          ; pbr_tune_diag.log (always on for shadow/glsl)

  [performance]
  show_stats=0           ; FPS + draw call counter overlay
  stats_hotkey=0x23      ; VK_END (hex) — toggle stats overlay
  shadow_max_cache=6000  ; max cached caster meshes
  sibling_cache_limit=500 ; max cached sibling sets (0=unlimited)

  [safety]
  exception_handler=1    ; wrap DllMain init in SEH to prevent game crash
*/

#ifndef PBR_CONFIG_H
#define PBR_CONFIG_H

#include <windows.h>

struct PbrConfig
{
    // --- Subsystem enables ---
    bool enablePBR;
    bool enableShadows;
    bool enableDepthCapture;
    bool enableFileLogger;
    bool enableGlslPilot;
    bool enableTuneOverlay;

    // --- Logging ---
    int  logLevel;       // 0=off, 1=normal, 2=verbose
    bool logToFile;
    bool logToDiag;

    // --- Performance ---
    bool showStats;
    int  statsHotkey;    // VK code
    int  shadowMaxCache;
    int  siblingCacheLimit;

    // --- Safety ---
    bool exceptionHandler;
};

extern PbrConfig g_config;

// Load pbr_config.ini from the DLL directory. Called once from DllMain.
// Falls back to defaults if the file is missing or unreadable.
void PbrConfig_Load();

// Check if a log line at the given verbosity should be written.
// level: 1=normal, 2=verbose. Returns true if g_config.logLevel >= level.
bool PbrConfig_ShouldLog(int level);

// Vectored Exception Handler — logs unhandled exceptions before the game dies.
// Installed from DllMain when g_config.exceptionHandler is true.
LONG WINAPI PbrVectoredHandler(PEXCEPTION_POINTERS ep);

#endif
