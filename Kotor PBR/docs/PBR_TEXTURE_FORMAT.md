# PBR Texture Format

Convention for PBR-aware textures in `<game>/Override/`.

## Texture roles

| Suffix | Role | Format | Required | Storage |
|---|---|---|---|---|
| (none) | Diffuse / albedo | TGA / TPC | Yes | TMU 0 |
| `n` | Normal map (RGB) | TGA / TPC | No (flat default) | TMU 8 RGB |
| `O` | Ambient occlusion (R-channel only) | TGA | No (defaults to 1.0) | **Packed into TMU 8 alpha** |
| `R` | Roughness (R-channel only) | TGA | No (sentinel −1 → falls back to `(1 − diffuse.alpha)` heuristic) | TMU 9 RGB |
| `E` | Emissive mask (R-channel only) | TGA | No | **Packed into TMU 9 alpha** |
| `M` | Metallic (R-channel only) | TGA | No (defaults to 0 / sidecar) | TMU 10 |

Five PBR maps compress into 3 TMUs because the Aspyr GL wrapper rejects `glActiveTexture(GL_TEXTURE11+)`. See [PBR_PIPELINE.md](PBR_PIPELINE.md) for packing details.

`<game>/Override/p_atton01.tga` is diffuse. Then siblings (no underscore — suffix appended directly to base name):
- `p_atton01n.tga` — normal map
- `p_atton01O.tga` — AO
- `p_atton01R.tga` — roughness
- `p_atton01E.tga` — emissive
- `p_atton01M.tga` — metallic

**Filename gotchas:**
- Suffix is case-insensitive on Windows (NTFS), but no separator (`p_atton01_R.tga` ≠ `p_atton01R.tga` — loader looks for the latter).
- Stray characters in extension (`p_atton01R._tga`) or trailing chars in stem (`p_atton01M_.tga`) silently skip the sibling. Confirm via `PBR|resolve|...` log line.

The `n` suffix is already established by some KOTOR2 modders (e.g., water mods ship `dan_water03n.tpc`). We adopt and extend.

## Sidecar metadata

Two formats are accepted (parsed in order — `.pbr` overrides `.txi` if both present):

- `<name>.txi` — engine-native; KOTOR 2 ignores unknown keys, so PBR keys are safe to add alongside `envmaptexture`, `bumpmaptexture`, `bumpiness`, etc.
- `<name>.pbr` — pure PBR-only file (no engine collision concerns).

Both parsed by `pbr_state.cpp::ApplyKv`. Keys are **case-insensitive**. Separator is `=` or whitespace. `#` and `//` start line comments.

```ini
# Override/p_atton01.txi (or .pbr)
metallic       = 0.0     # 0..1, default 0
roughness      = 0.5     # 0..1, default -1 (sentinel: stock alpha-derived path)
fresnelF0      = 0.04    # default 0.04, metals typically 0.5..1.0
emissive       = 1.0     # emissive scale, default 1.0 (only applied if _E packed)
normalStrength = 2.0     # bump xy multiplier, default 1.0 (1=stock, >1 stronger, 0 flat)
cavityStrength = 1.0     # AO intensity 0..1, clamped, default 0.3
fresnelRim     = 0.5     # rim-lighting boost to env reflection, default 0.0
reflectivity   = 1.0     # env reflection multiplier, default 1.0
billboardshadow = 1      # 0 (default) / 1 — see below (steam/fog/foliage shadow receive)
```

`use*Map` flags from the legacy spec are **gone**. Sibling presence auto-enables. Force-disable by simply removing/renaming the sibling.

### `normalmap` — share one normal across textures (2026-05-30)

```ini
# Override/C_drdprot02.txi   (and 03, 04, 05 …)
normalmap = C_drdprot01n     # use C_drdprot01n.tga as THIS material's normal
```

By default each diffuse `<name>` auto-loads `<name>n.tga`. The `normalmap` key (alias `normalmaptexture`) overrides that: the value is a **texture stem** and the loader loads `Override/<stem>.tga` as this material's normal. Use it for recolored variants (e.g. 5 droid skins that share one sculpt) — author **one** `C_drdprot01n.tga` and point the other four `.txi` at it, saving VRAM + disk.

- Value is a stem (no extension; a stray `.tga`/`.tpc` is stripped). It's the literal file, so include the trailing `n` if that's the filename (`C_drdprot01n`).
- The shared normal is loaded **RGB only — no AO packing** (AO is per-skin and rarely shared). Roughness/metallic still resolve per-diffuse (`<name>R/M`); only the normal is redirected.
- Verify pickup in `logs/pbr_file_log.txt` (`PBR|resolve|…|nrm=<id>`).

**Synthetic placeholders don't count as real maps.** If only `<base>O.tga` is provided (no `<base>n.tga`), the loader synthesizes a white-RGB normal as host for the AO alpha pack, but `useNormal` flag stays 0 so the shader doesn't apply the fake normal — only the AO data is used.

### `billboardshadow` — sun-shadow on billboard sheets (fog / foliage / liquid) (2026-05-30)

```ini
# Override/grass.txi   (waving grass blades), or a fog/leaf/liquid sheet …
billboardshadow = 1
```

Translucent **billboard** geometry (grass/leaf cards, fog/smoke sheets, the flowing
toxic-liquid plane `dro_toxn`) has its geometric normal facing the **camera**, not the
sun. The normal world-shadow path darkens a surface by `occlusion × (N·sun)`, so on a
camera-facing billboard `N·sun ≈ 0` → the shadow term vanishes and the sheet stays fully
lit even inside a shadow.

`billboardshadow = 1` (alias `volumeshadow`) adds a second darkening pass that uses
**raw sun-occlusion, ungated by the normal** (`env[23].y`, sampled in
`fp_worldtex_lm_fog_alpha`, `fp_worldtex_env_fog`, `fp_worldtex_lm_env`,
`fp_model_env_fog` — billboards on Dantooine split across the first two, so both
were needed). Strength follows the `Shadow darken`
slider; clamps to `Shadow floor`. Default `0` so opaque walls keep the correct
normal-gated behaviour (a back-facing wall must NOT darken — it has no sun anyway).

- The texture must be **name-resolvable** (`PBR|sidecar|<name>|…bbShadow=1` must appear
  in `logs/pbr_file_log.txt`). BIF-internal particle textures that log `tex=?` in the
  shadow diag can't be keyed and won't pick the flag up — put the texture (or just its
  `.txi`) in `Override\`.
- A flagged billboard only darkens where a **taller occluder** (tree canopy, cliff)
  blocks the sun. A blade in open field reads its own caster depth → "lit" → no darken
  (correct). Test under a tree/cliff shadow, ideally with `Shadow darken = 1`.

> **Note on M4-78 "steam":** `dro_toxn.tga` turned out to be the green toxic *liquid* under
> the floor grate, not the visible steam plume. The actual dro802 steam is a BIF-internal
> particle that logs `tex=?` (no name) — so it can't be keyed by this per-texture flag yet.
> Identifying/flagging the unnamed particle path is a separate follow-up.

## Defaults

From `ApplyPbrDefaults`:
- `metallic = 0`
- `roughness = -1` (sentinel triggers stock K2 `(1 − diffuse.alpha)` fallback for env weight)
- `fresnelF0 = 0.04`
- `emissive = 1.0`
- `normalStrength = 1.0` (was 0.1 + dead; now wired as bump xy multiplier in all 8 normal-aware FPs)
- `cavityStrength = 0.3`
- `fresnelRim = 0.0`
- `reflectivity = 1.0`

**No "auto from envmap"** — `metallic` is purely from sidecar/map. `envmaptexture` in `.txi` controls only which cube the engine binds to TMU 2, not material classification.

## How engine sees them

Stock engine (without DLL):
- Loads only `<name>.tga` / `<name>.tpc` (diffuse)
- Loads `<name>b.tga` if TXI says `bumpmaptexture <name>b` (engine convention)
- Ignores R/M/O/E — doesn't know about them

With our DLL:
- `CreateFileA` hook records every `Override\*.tga` open
- `glTexImage2D` hook maps `glBindTexture` ID → filename
- On `glBindTexture(diffuse_id)` → sibling loader checks Override for n/O/R/E/M
- Found siblings → packed and uploaded to TMU 8/9/10
- DLL pushes per-material params to `program.env[20..23]`
- Custom PBR fp samples those TMUs (see [PBR_PIPELINE.md](PBR_PIPELINE.md))

## TPC vs TGA

KOTOR 2 supports both:
- **TGA** — uncompressed RGB/RGBA, easy to author
- **TPC** — BioWare proprietary compressed (S3TC variants + RGBA, includes mipmaps)

For PBR siblings, **TGA is preferred** (easier to author, transparent to all tools). TPC works too but requires KOTOR-specific compression tool.

Some modders ship TPC for memory savings. Our loader supports both.

## File priority

Engine's load order for a texture name `<name>`:
1. `Override\<name>.tga`  — highest priority
2. `Override\<name>.tpc`
3. ERF archive (`texturepacks/swpc_tex_*.erf`)
4. BIF archive (`data/textures.bif`)

So putting a PBR-version in Override **always wins** over stock.

## Texture sizing

KOTOR 2 stock textures are 256×256 to 1024×1024. PBR maps should match the diffuse resolution for tiling correctness. Larger maps work but consume VRAM (3-5x per material with full PBR set).

## Authoring tips

- **Roughness map**: convert from existing `(1 - diff.alpha)` as starting point — preserves engine's reflectivity intent
- **Normal map**: standard tangent-space, DirectX convention (green down). Use Substance/Blender bake or `crazybump`.
  - **No true TBN.** The VP passes only the world-space geometric normal — no tangent/bitangent. FPs approximate via `N = normalize(tangentNormal·strength + worldNormal)`, so the bump is interpreted in **world axes**, not UV axes. Effect: strong, believable on large axis-aligned surfaces (walls/floors); on small curved meshes (droids) the world-Z bias washes detail out. Crank `normalStrength` (2–4) to make the bump readable on models. A real per-fragment TBN (ad-hoc basis from N via `XPD`) is the proper fix if subtle models still look flat — not yet implemented.
- **Metallic map**: pure black/white for stylized; gradient for wear effects
- **AO map**: bake from high-poly model or use auto-AO from albedo

## Distribution

PBR mods bundled as TSLPatcher / TSL Manager package that places all siblings in `Override\`. Our DLL picks them up automatically on first scene visit.
