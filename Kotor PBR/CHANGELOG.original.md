# Changelog

## 0.2.0-dev (current — end-to-end PBR pipeline)

- **Step A2:** `pbr_state.cpp/.h` — texture id ↔ filename mapping via global state (single-thread OK)
- **Step A3:** `tga_loader.cpp/.h` — minimal TGA decoder (uncompressed + RLE, 24/32 bit + grayscale)
- **Step A3:** PBR sibling resolver — auto-loads `<name>n.tga` (normal), `<name>R.tga` (rough), `<name>M.tga` (metal), `<name>O.tga` (AO), `<name>E.tga` (emissive) from Override
- **Step A3:** `.pbr` sidecar parser — INI metadata for per-texture PBR params
- **Step A4:** env params injection — `program.env[20..22]` carries PBR params for fp shaders
- **Step A5:** example PBR fp shader `fp_char_env_fog.txt` (samples TMU 8/9/10 for normal/rough/metal)
- `pbr_hooks.cpp/.h` — `glBindTexture` and `glTexImage2D` interception
- Re-entry guard `g_pbrLoadingInProgress` to prevent recursion during sibling uploads
- Updated `opengl32.def` — `glBindTexture` and `glTexImage2D` route through PBR hooks
- Updated `platform.h` — GL_TEXTURE0..11, GL_ACTIVE_TEXTURE, GL_TEXTURE_BINDING_2D constants
- Updated `Makefile` — builds new PBR files

## 0.1.0-dev

- Forked from ShaderOverride 2016 (HappyFunTimes)
- Added `iat_hook.cpp/.h` — generic PE IAT patching
- Added `file_logger.cpp/.h` — CreateFileA/W hook + log to `pbr_file_log.txt`
- Added `platform.h` stub (was missing in upstream repo)
- Modified `opengl32.cpp` DllMain — calls InitFileLogger/ShutdownFileLogger
- Modified opengl32.cpp inline asm — added GCC `__attribute__((naked))` paths for MinGW build
- Updated `glFunctions.cpp` — fixed `std::ifstream` openmode for stricter compilers
- Added `Makefile` for MinGW-w64 build
- Added VS Code build task (`.vscode/tasks.json`)
- Expanded `shader_ident.txt` from 8 to ~50 mappings (post-Aspyr shader identification)
- Added documentation: README, INSTALL, SHADER_REFERENCE, PBR_TEXTURE_FORMAT, ARCHITECTURE, BUILD

## Roadmap

- 0.3 — TPC decoder (BioWare compressed texture format)
- 0.4 — PBR fp for walls (`fp_walls_lm_fog`, `fp_walls_lm_env_fog`)
- 0.5 — Bumpmap (`n`) sample integration into PBR fp (currently sampled but not used in math)
- 0.6 — IBL approximation via env probe sampling
- 0.7 — Performance pass: cache `glGetIntegerv` queries, batched env param updates
- 1.0 — Stable release + example PBR texture pack for one location
