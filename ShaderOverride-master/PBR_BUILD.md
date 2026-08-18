# ShaderOverride PBR — Step A1: File Logger

Diagnostic build. Hooks `CreateFileA`/`CreateFileW` in `swkotor2.exe` IAT and logs all file paths to `pbr_file_log.txt` (in game folder).

## What changed from upstream

- New: `iat_hook.cpp` / `iat_hook.h` — PE Import Address Table patching utility
- New: `file_logger.cpp` / `file_logger.h` — CreateFileA/W hook + path logger
- New: `platform.h` — Windows + OpenGL types stub (was missing from repo)
- Modified: `opengl32.cpp` — `DllMain` calls `InitFileLogger()` on attach, `ShutdownFileLogger()` on detach
- Modified: `ShaderOverride.vcxproj` — added new files to build

## Build

Same as original — Visual Studio. Output: `opengl32.dll`.

Pre-build event uses 7-Zip to embed source. If 7-Zip path doesn't match, edit `ShaderOverride.vcxproj` PreBuildEvent or remove it.

For quick test build without 7-Zip:
1. Open `ShaderOverride.vcxproj` in VS
2. Remove or comment `<PreBuildEvent>` blocks (lines 56-59, 75-80)
3. Build Release|Win32 → produces `Release\opengl32.dll`

## Install

Copy built `opengl32.dll` to game folder (replace existing). Keep `shader_ident.txt` etc as is.

## Run

1. Start game
2. Play a short scene where new textures load (e.g., enter Peragus medbay, talk to NPC, look at water)
3. Exit game (so `ShutdownFileLogger` flushes)
4. Open `<game>/pbr_file_log.txt`

## Expected output

```
=== PBR file logger started ===
hook CreateFileA=00007FFxxx CreateFileW=00007FFxxx
A|OK|swkotor2.ini
A|OK|chitin.key
A|OK|data\2da.bif
A|OK|Override\dialog.tlk
A|OK|Override\p_hk47.tga       <-- texture load
A|OK|Override\p_hk47b.tga      <-- bumpmap variant
A|OK|swkotorTextures.bif
...
```

## What to look for in log

- `Override\*.tga` opens — confirm engine reads sibling textures via CreateFileA
- `Override\*.tpc` opens — TPC variant priority
- `*.bif` opens — BIF archive access (TPC textures inside, not individual CreateFileA per texture)
- `Override\<name>b.tga` — bumpmap sibling confirmation (existing engine behavior)
- Custom paths `<name>.pbr`, `<name>R.tga` etc — should appear as FAIL (we haven't placed them yet)

## Next step (A2)

After confirming CreateFileA fires for Override textures:
- Add TLS variable in CreateFileA hook to record currently-loading path
- Add `glTexImage2D` hook to capture GL texture id ↔ filename mapping
- Build sibling resolver

If textures come ONLY from BIF (not CreateFileA), we'll need to hook deeper — `ReadFile` from BIF + parse BIF format. Less clean. Hope step A1 confirms Override\*.tga path.

## Risks

- IAT hook fires only for direct kernel32 imports. If engine resolves CreateFileA via `GetProcAddress` at runtime, hook misses. Mitigation: also hook `GetProcAddress`.
- DLL load order matters. ShaderOverride DLL loads BEFORE swkotor2.exe imports resolve? Actually opengl32.dll is loaded when game does `wglCreateContext` or similar — AFTER exe entry. Imports already resolved. Should be fine.
