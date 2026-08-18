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

#endif
