/*
File access logger for PBR diagnostic step A1.
Hooks CreateFileA/W in swkotor2.exe IAT, logs all file paths to pbr_file_log.txt.
*/

#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include <windows.h>

// Install IAT hooks for CreateFileA + CreateFileW on the main exe (swkotor2.exe).
// Opens pbr_file_log.txt in cwd for append.
void InitFileLogger();

// Close log file. Called on DLL_PROCESS_DETACH.
void ShutdownFileLogger();

// Write arbitrary line to the PBR log (used by other subsystems for diagnostics).
void PbrLogLine(const char *line);

// Current game module (area) name, lowercased, e.g. "601dan", "801dro", "003ebo".
// Detected from the engine's `...\modules\<name>.mod` / `...\currentgame\<name>.mod`
// opens seen by the CreateFile hook. Empty string until the first area loads. Used to
// bind per-location light/shadow tuning. Location_Version() bumps on every change so
// consumers can poll cheaply.
const char *Location_Module();
long        Location_Version();

#endif
