# Install

## Prerequisites

### 1. Apply 3C-FD-Patcher (REQUIRED)

Download from: https://deadlystream.com/files/file/2734-fog-fix-more-3c-fd-patcher/

Without 3C-FD applied, fog and envmap reflections are broken in modern KOTOR 2 versions (Aspyr update regression). Also, shader hashes won't match our `shader_ident.txt` if 3C-FD isn't applied first.

### 2. Backup your game folder

Specifically:
- `<game>/opengl32.dll` (if present from a prior mod)
- `<game>/shader_ident.txt` (if present)

## Step 1 — Copy DLL

Copy `deploy/opengl32.dll` → `<game>/opengl32.dll`

Windows DLL search order makes the game load this proxy before the system `opengl32.dll`.

## Step 2 — Copy shader identifier map

Copy `deploy/shader_ident.txt` → `<game>/shader_ident.txt`

Maps shader content hashes (MD5) → human-friendly names. Without it, dumps use raw hashes.

## Step 3 — Optional override folder

Copy or create `<game>/shaders_override/` (empty by default).

Place modified ARB shaders here (e.g. `fp_worldtex_lm_fog_alpha.txt`). The DLL will substitute them at shader compile time.

## Verify

1. Start game
2. Exit
3. Check `<game>/`:
   - `pbr_file_log.txt` should exist (file access log)
   - `shaders_original/` folder should appear with `.txt` files (dumped shaders, human-named)
4. Open `pbr_file_log.txt` — first lines should show:
   ```
   === PBR file logger started ===
   hook CreateFileA=0xXXXXXXXX CreateFileW=0xXXXXXXXX
   ```

If `pbr_file_log.txt` is missing or shader dumps don't appear — DLL not loaded. Check that `opengl32.dll` is in the game's main folder (next to `swkotor2.exe`), not a subfolder.

## Uninstall

1. Restore backed-up `opengl32.dll` (or delete ours)
2. Optionally delete: `shader_ident.txt`, `shaders_original/`, `shaders_override/`, `pbr_file_log.txt`

No game files modified.

## Compatibility

| Item | Status |
|---|---|
| KOTOR 2 Steam | ✅ |
| KOTOR 2 GOG | ✅ |
| KOTOR 2 retail | ✅ |
| M4-78 Enhancement Project | ⚠️ Verify — 3C-FD documents incompatibility with original M4-78 |
| TSL Restored Content Mod (TSLRCM) | ✅ |
| Other ShaderOverride installs | ❌ Replace — only one `opengl32.dll` proxy at a time |
