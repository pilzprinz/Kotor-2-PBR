/*
CreateFileA/W IAT hook + path logger.

For step A1 diagnostic: every file open by swkotor2.exe is logged to pbr_file_log.txt.
Used to identify which textures the engine reads from Override/, BIF, etc.

Output format per line:
  A | <return_handle_status> | <path>
  W | <return_handle_status> | <path>

return_handle_status: OK or FAIL (based on returned handle != INVALID_HANDLE_VALUE).
*/

#include "file_logger.h"
#include "iat_hook.h"
#include "pbr_state.h"
#include <stdio.h>
#include <string.h>

typedef HANDLE (WINAPI *PFNCREATEFILEA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *PFNCREATEFILEW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

static PFNCREATEFILEA s_origCreateFileA = NULL;
static PFNCREATEFILEW s_origCreateFileW = NULL;
static HANDLE s_logFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION s_logCrit;
static bool s_logCritInit = false;

// --- current game module (area) detection ----------------------------------
// The engine opens `...\modules\<name>.mod` / `...\currentgame\<name>.mod` when it
// loads an area. We sniff those opens to track the active module name, used as the
// per-location key for light/shadow tuning. Lowercased so the key is case-stable.
static char s_module[64] = "";
static long s_moduleVer  = 0;

static void WriteLogLine(const char *line);   // defined below

const char *Location_Module()  { return s_module; }
long        Location_Version() { return s_moduleVer; }

static void DetectModuleFromPath(const char *path)
{
    if (!path) return;
    char low[1024];
    size_t n = 0;
    for (; path[n] && n < sizeof(low) - 1; n++)
        low[n] = (char)(path[n] >= 'A' && path[n] <= 'Z' ? path[n] + 32 : path[n]);
    low[n] = 0;
    // Must be a module file (lives under modules\ or currentgame\).
    if (!strstr(low, "currentgame\\") && !strstr(low, "modules\\")) return;
    const char *slash = strrchr(low, '\\');
    const char *base  = slash ? slash + 1 : low;
    const char *dot   = strrchr(base, '.');
    if (!dot || strcmp(dot, ".mod") != 0) return;        // only the .mod module file
    size_t blen = (size_t)(dot - base);
    if (blen == 0 || blen >= sizeof(s_module)) return;
    if (blen >= 4 && strncmp(base + blen - 4, "_loc", 4) == 0) return;  // skip lips/localization
    char name[64];
    memcpy(name, base, blen);
    name[blen] = 0;
    if (strcmp(name, s_module) != 0) {
        memcpy(s_module, name, blen + 1);
        s_moduleVer++;
        char line[128];
        snprintf(line, sizeof(line), "[loc] module=%s\n", s_module);
        WriteLogLine(line);
    }
}

static void WriteLogLine(const char *line)
{
    if (s_logFile == INVALID_HANDLE_VALUE) return;
    if (!s_logCritInit) return;
    EnterCriticalSection(&s_logCrit);
    DWORD written = 0;
    WriteFile(s_logFile, line, (DWORD)strlen(line), &written, NULL);
    LeaveCriticalSection(&s_logCrit);
}

void PbrLogLine(const char *line)
{
    WriteLogLine(line);
}

static HANDLE WINAPI MyCreateFileA(LPCSTR lpFileName, DWORD dwAccess, DWORD dwShare,
                                   LPSECURITY_ATTRIBUTES sa, DWORD dwCreation,
                                   DWORD dwFlags, HANDLE hTemplate)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    if (s_origCreateFileA)
        h = s_origCreateFileA(lpFileName, dwAccess, dwShare, sa, dwCreation, dwFlags, hTemplate);
    else
        h = CreateFileA(lpFileName, dwAccess, dwShare, sa, dwCreation, dwFlags, hTemplate);

    if (lpFileName)
    {
        char buf[1024];
        snprintf(buf, sizeof(buf), "A|%s|%s\n",
                    (h != INVALID_HANDLE_VALUE) ? "OK" : "FAIL", lpFileName);
        WriteLogLine(buf);

        // PBR: capture Override texture filename for id↔name association on next glTexImage2D
        if (h != INVALID_HANDLE_VALUE)
            PbrCaptureLoadingFilename(lpFileName);

        DetectModuleFromPath(lpFileName);
    }

    return h;
}

static HANDLE WINAPI MyCreateFileW(LPCWSTR lpFileName, DWORD dwAccess, DWORD dwShare,
                                   LPSECURITY_ATTRIBUTES sa, DWORD dwCreation,
                                   DWORD dwFlags, HANDLE hTemplate)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    if (s_origCreateFileW)
        h = s_origCreateFileW(lpFileName, dwAccess, dwShare, sa, dwCreation, dwFlags, hTemplate);
    else
        h = CreateFileW(lpFileName, dwAccess, dwShare, sa, dwCreation, dwFlags, hTemplate);

    if (lpFileName)
    {
        char buf[1024];
        char narrow[768];
        int n = WideCharToMultiByte(CP_UTF8, 0, lpFileName, -1, narrow, sizeof(narrow) - 1, NULL, NULL);
        if (n > 0)
        {
            narrow[n - 1] = '\0';
            snprintf(buf, sizeof(buf), "W|%s|%s\n",
                        (h != INVALID_HANDLE_VALUE) ? "OK" : "FAIL", narrow);
            WriteLogLine(buf);
            DetectModuleFromPath(narrow);
        }
    }

    return h;
}

void InitFileLogger()
{
    InitializeCriticalSection(&s_logCrit);
    s_logCritInit = true;

    // Write under logs/ if dir exists or can be created; else root.
    CreateDirectoryA("logs", NULL);
    const char *path = "logs\\pbr_file_log.txt";
    s_logFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ,
                            NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s_logFile == INVALID_HANDLE_VALUE)
        s_logFile = CreateFileA("pbr_file_log.txt", GENERIC_WRITE, FILE_SHARE_READ,
                                NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (s_logFile != INVALID_HANDLE_VALUE)
    {
        const char *banner = "=== PBR file logger started ===\n";
        DWORD written = 0;
        WriteFile(s_logFile, banner, (DWORD)strlen(banner), &written, NULL);
    }

    HMODULE hExe = GetModuleHandleA(NULL);
    s_origCreateFileA = (PFNCREATEFILEA)IatHook(hExe, "kernel32.dll", "CreateFileA", (FARPROC)MyCreateFileA);
    s_origCreateFileW = (PFNCREATEFILEW)IatHook(hExe, "kernel32.dll", "CreateFileW", (FARPROC)MyCreateFileW);

    char status[256];
    snprintf(status, sizeof(status),
                "hook CreateFileA=%p CreateFileW=%p\n",
                (void*)s_origCreateFileA, (void*)s_origCreateFileW);
    WriteLogLine(status);
}

void ShutdownFileLogger()
{
    if (s_logFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(s_logFile);
        s_logFile = INVALID_HANDLE_VALUE;
    }
    if (s_logCritInit)
    {
        DeleteCriticalSection(&s_logCrit);
        s_logCritInit = false;
    }
}
