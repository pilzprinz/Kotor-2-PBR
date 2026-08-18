/*
IAT hook implementation.
Walks PE import descriptors, finds matching DLL + function, swaps thunk.
*/

#include "iat_hook.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

FARPROC IatHook(HMODULE hTarget, const char *moduleName, const char *functionName, FARPROC newFunc)
{
    if (!hTarget) return NULL;

    BYTE *base = (BYTE *)hTarget;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;

    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;

    IMAGE_DATA_DIRECTORY *importDir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir->Size == 0) return NULL;

    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + importDir->VirtualAddress);

    for (; desc->Name != 0; ++desc)
    {
        const char *dllName = (const char *)(base + desc->Name);
        if (_stricmp(dllName, moduleName) != 0) continue;

        IMAGE_THUNK_DATA *origThunk = (IMAGE_THUNK_DATA *)(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA *firstThunk = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);

        for (; origThunk->u1.AddressOfData != 0; ++origThunk, ++firstThunk)
        {
            if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;

            IMAGE_IMPORT_BY_NAME *importByName = (IMAGE_IMPORT_BY_NAME *)(base + origThunk->u1.AddressOfData);
            if (strcmp((const char *)importByName->Name, functionName) != 0) continue;

            // Found target thunk. Patch it.
            DWORD oldProtect = 0;
            if (!VirtualProtect(&firstThunk->u1.Function, sizeof(void *), PAGE_READWRITE, &oldProtect))
                return NULL;

            FARPROC oldFunc = (FARPROC)firstThunk->u1.Function;
            firstThunk->u1.Function = (uintptr_t)newFunc;

            VirtualProtect(&firstThunk->u1.Function, sizeof(void *), oldProtect, &oldProtect);
            return oldFunc;
        }
    }

    return NULL;
}
