# Troubleshooting + Gotchas

Field-discovered issues + diagnostics for the PBR pipeline.

## Build

**Toolchain:** w64devkit at `C:\Program Files (x86)\w64devkit`. KOTOR 2 is 32-bit → `i686-w64-mingw32-g++` required (built into w64devkit).

```powershell
$env:PATH = "C:\Program Files (x86)\w64devkit\bin;$env:PATH"
Set-Location "<repo>\Kotor PBR\source"
mingw32-make
```

Output → `Kotor PBR\deploy\opengl32.dll`. `mingw32-make clean` nukes `.o` files.

## Shader changes don't take effect

K2 caches compiled ARB programs in process memory. Reload save / location does **not** trigger recompile. Override TXT file is re-read by `my_glProgramString` only when engine calls `glProgramStringARB` — which happens at program-object creation, not per-frame.

**Fix:** Fully exit and relaunch swkotor2.exe after editing any `shaders_override/*.txt`.

DLL changes always require relaunch (LoadLibrary at process start).

## Sidecar changes don't take effect

`PbrGetSiblings` caches the `SiblingSet` (including parsed `PbrParams`) in `g_siblingCache`. Sidecar `.txi`/`.pbr` is reparsed only on cache miss. Cache cleared by `glDeleteTextures` hook OR full DLL reinit (process restart).

**Fix:** Restart game to reparse sidecar. In-session save reload won't pick up `.txi` changes if the texture has been bound already.

## File naming — sibling suffix is case-insensitive on Windows but exact

Loader expects exact suffix letters: `n`, `O`, `R`, `E`, `M` (concatenated to base name, no underscore).

| Filename | Loads as | Notes |
|---|---|---|
| `DRO_Floor5n.tga` | normal RGB → TMU 8 | ✓ |
| `DRO_Floor5o.tga` | AO → packed into TMU 8 alpha | Windows case-insensitive matches `O` suffix |
| `DRO_Floor5R.tga` | rough.r → TMU 9 | ✓ |
| `DRO_Floor5r._tga` | **rejected** — bad extension (extra `.`) | Silent no-load |
| `DRO_Floor5m_.tga` | **rejected** — trailing `_` in stem | Silent no-load |
| `DRO_Floor5_R.tga` | **rejected** — underscore before suffix | Loader looks for `<base>R.tga`, not `<base>_R.tga` |

**Diagnostic:** check log for `PBR|resolve|<name>|nrm=N(synth=0 ao=1) rgh=M(synth=0 emi=1) mtl=K`. Zeros mean sibling missing/malformed.

## "I don't see any AO"

Multiple causes, in order of frequency:

1. **Texture has no `_O` sibling.** Log `ao=0` → cavity slider has no input. Need to author `<base>O.tga`.

2. **Wrong texture in scene.** AO only modulates pixels of textures that have packed AO. If your camera mostly shows other textures, you won't see the effect even at extreme `cavitystrength` values.

3. **Painted-in shadows confused with shader AO.** Many K2 stock diffuse textures have baked shadows in the albedo. These look like AO but don't respond to `cavitystrength`. Toggle `cavitystrength = 0` vs `5` and compare — if no visible change, AO isn't running (or wrong texture).

4. **Shader not reloaded** (see above).

5. **Texture AO masked by screen-space contact shadow** in `fp_worldtex_env_fog`. Contact shadow runs unconditionally on depth-buffer comparison. May dominate already-dark grooves so additional texture AO contribution is invisible. Test with `csCfg.z = 0` (disable contact-shadow strength) to isolate texture AO.

**Debug visualization:** Temporarily replace
```
MOV result.color, d;
```
with
```
MOV result.color, nrm.aaaa;
```
at end of `fp_worldtex_env_fog.txt`. Restart game. Floor renders as grayscale of packed AO. White = no AO data, dark grooves = AO present. Revert when done.

## "Floor turned bright/light at high cavity"

Old shader bug — `cavityStrength > 1` produces negative `ao.x`, then `LRP fog` wraps to fog color. Fixed by `MAX/MIN` clamp:
```
MAX ao.x, ao.x, 0.0;
MIN ao.x, ao.x, 1.0;
```

Recommended `cavitystrength` range: **0.3 – 1.0**. Higher values saturate (clamped) without further visual change.

## "Polished metal floor turned matte after EC patch"

Energy conservation kills diffuse on metals: `kd = (1-F)(1-metal)`. `metallic = 1` → `kd = 0` → no diffuse, only spec + env reflection. If the env cubemap on that scene is dark / the `_R` rough map is high → metal looks flat black.

**Fixes:**
- `metallic = 0.5..0.8` (semi-metal — keeps some diffuse for visual interest)
- Provide low-value `_R` (dark = polished) so env reflection dominates
- Verify scene cubemap is bright enough; check `envmaptexture CM_<name>` in `.txi`

EC is gated by `usePbr` — stock textures without any sidecar / no rough sibling skip EC entirely.

## "Emissive map doesn't glow"

Requires:
- `<base>E.tga` present (alone OK — loader synthesizes white rough host)
- `emissive` in sidecar ≥ 0 (default 1.0 after recent change)
- Log shows `emi=1` on resolve line

Emissive formula: `r += d * rough.a * emissiveScale * useEmi`. Result is **proportional to diffuse color** — black pixels in diffuse won't glow regardless of E mask. For colored glow on dark surfaces, add a colored emissive layer to diffuse and modulate via E mask.

## Shader hash collision / rename

`fp_worldtex_env_fog` (current ident) and `fp_worldtex_lm_env_fog` (legacy ident, e.g., `ShaderOverride-master/Release/shader_ident.txt`) share MD5 `ffceb52582fe3f52825bbae40d37a3df`. Same shader, renamed in catalog.

Don't ship both override files — only the name in the active `shader_ident.txt` resolves.

## Log file locations

`logs/pbr_file_log.txt` is opened with `CREATE_ALWAYS` → **overwritten each game launch**. Path is relative to game CWD (`<game>/logs/pbr_file_log.txt`), NOT the project dir.

If you copy logs to project for review, they get stale immediately on next launch.

## Useful log greps

```
grep "SHADER|override"        # which overrides got picked up this session
grep "PBR|resolve|<texname>"  # sibling pack status for one texture
grep "PBR|sidecar|<texname>"  # parsed sidecar values
```

`SHADER|override` count = number of distinct shaders the scene needed. If a stock shader doesn't appear, no override file matched its hash (or no override exists).
