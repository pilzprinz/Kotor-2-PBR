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
#include <stdio.h>
#include <string.h>

typedef HANDLE (WINAPI *PFNCREATEFILEA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *PFNCREATEFILEW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

static PFNCREATEFILEA s_origCreateFileA = NULL;
static PFNCREATEFILEW s_origCreateFileW = NULL;
static HANDLE s_logFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION s_logCrit;
static bool s_logCritInit = false;

static void WriteLogLine(const char *line)
{
    if (s_logFile == INVALID_HANDLE_VALUE) return;
    if (!s_logCritInit) return;
    EnterCriticalSection(&s_logCrit);
    DWORD written = 0;
    WriteFile(s_logFile, line, (DWORD)strlen(line), &written, NULL);
    LeaveCriticalSection(&s_logCrit);
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
        }
    }

    return h;
}

void InitFileLogger()
{
    InitializeCriticalSection(&s_logCrit);
    s_logCritInit = true;

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
