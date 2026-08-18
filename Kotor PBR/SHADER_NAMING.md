# Shader override names — usage map + renaming plan

## Why this doc
The override shaders are named after the **engine ARB program's feature set** (the friendly
names in `shader_ident.txt`, keyed by MD5 of the engine shader), NOT after what surfaces
they end up on. The GLSL pilot (2026-05-31) revealed that a single pair is used FAR more
broadly than its name implies, and that the **same texture** (e.g. wood) is drawn by
**different shaders** indoors vs outdoors. The user asked to document this and rename toward
intuitive names. This is the working doc for that.

## Key finding (from the GLSL pilot breakage)
Replacing **`fp_worldtex_lm_fog_alpha` + `vp_static_lit_fog`** with a test GLSL program
turned the following black/wrong, proving they ALL use that one pair:
- interior walls (lightmap-lit), and the **same wood texture indoors** (outdoors it uses a
  different worldtex shader, so it stayed correct)
- foliage / leaf cards (alpha-tested)
- skybox (no lightmap → went purple)
- (apparently) engine projected character/droid shadows

→ `fp_worldtex_lm_fog_alpha` is effectively a **catch-all for lightmapped + alpha-tested
world geometry**, not "interior walls". Its name undersells its reach.

## ⚠ Names are LOAD-BEARING in code — do not blind-rename
Several code paths branch on the **name prefix**, so renaming requires updating them in lockstep:
- `shadow_map.cpp ClassifyCaster`: `strncmp(fpN,"fp_worldtex",11)` → `CC_GEOM` (cached static
  caster); `strncmp(fpN,"fp_model",8)` → `CC_MODEL`. The whole caster cache/cull keys off these.
- Per-receiver shadow split: world FPs (`fp_worldtex*`,`fp_door`) include `shadow_receive`
  (TMU6 complete map); model FPs (`fp_model*`) include `shadow_receive_self` (TMU5 self map).
- GLSL pilot target match (`glsl_program.cpp`) compares the exact name.
- `shader_ident.txt` (MD5 → friendly name) and the override `.txt` filename must match the name.

A rename that misses any of these silently drops shadows / casters / overrides. So: rename as a
**deliberate refactor** (update `shader_ident.txt` + file + every code reference together), not ad hoc.

## Proposed approach
1. **Usage census first (diagnostic).** Before renaming, log per receiver FP the set of
   texture names + a context hint (interior/exterior — e.g. presence of a lightmap on TMU1, or
   the area module) so we get the real surface→shader map. Reuse `PbrGetTextureName` +
   the FP name at draw time. Output one `SHADERMAP|<fp>|tex=<name>|lm=<0/1>` line per new combo.
2. **Pick an intuitive scheme** once the census is in. Candidate direction (keep a stable
   prefix the code keys on, add a clarifying middle):
   - keep `fp_worldtex_` / `fp_model_` prefixes (code-load-bearing) — do NOT drop them.
   - clarify the tail by role, e.g. `fp_worldtex_lm_fog_alpha` → `fp_worldtex_interior_lm_alpha`
     (lightmapped interior / alpha foliage), `fp_worldtex_env_fog` → `fp_worldtex_exterior_env`,
     etc. The prefix stays; only the descriptive tail changes, so `strncmp` checks keep working.
3. **Refactor** in one pass: rename file + `shader_ident.txt` entry + any exact-name code match,
   rebuild, verify shadows/casters/overrides intact.

## Census results (2026-05-31) — the real surface→shader map
Diag run covered Ebon Hawk, Dantooine, Nar Shaddaa, Telos, M4-78 (interiors + exteriors).
Method: per-draw inventory keyed by `fp + vp + aTest + blend + dMask + lm(TMU1) + tex(TMU0)`,
deduped from ~557k log lines → 428 real-FP combos. Counts below = unique combos, not frames.

### ⚠ Correction to the earlier plan: `lm` does NOT mean interior vs exterior
The pre-census guess was "lightmap present ⇒ interior". **False.** Both big world shaders are
*mostly* lightmapped:
- `fp_worldtex_lm_fog_alpha`: 138 lm combos vs 33 no-lm.
- `fp_worldtex_env_fog`: 121 lm combos vs 16 no-lm.
The real axis is **role + the paired VP**, not lightmap presence. The two worldtex shaders barely
share textures (`comm` overlap = just `dan_wn02`, `grass`) — they cover *different surface sets*.

### World shaders (sample complete map, `shadow_receive`)
| Engine FP | dominant VP | role / surfaces seen | lm? |
|---|---|---|---|
| `fp_worldtex_lm_fog_alpha` | `vp_static_lit_fog` (170/171) | **Primary diffuse world surface + catch-all.** Lightmapped interiors broadly, PLUS non-lm Dantooine nature: `dan_sky1/2/3` (skybox→went purple in pilot), `dan_leaf02`/`dan_grass07`/`grass` (alpha foliage→blackened), `dan_bark04`, rocks, stone, walls, flags. Alpha-tested. | mostly, but no-lm on sky/foliage |
| `fp_worldtex_env_fog` | `vp_worldtex_env_fog_t2` (133/137) | **Env-reflective architecture** (dual-texcoord t2). Heavy on M4-78 `dro_*`: metal panels, floors, cables, roofs, hex plating; some Dantooine `dan_trim/wall`. This is the "outdoor wood stayed fine" shader. | mostly lm |
| `fp_worldtex_bump_env` | `vp_skinned_bump_env` | bump+env, skinned VP — rare (3 combos). | — |
| `fp_door` | `vp_static_lit_fog` / `vp_static_env_fog` | door surfaces, never lightmapped (dynamic). | none (5/5 no-lm) |

### Model/char shaders (sample self map, `shadow_receive_self`)
| Engine FP | VPs | surfaces seen |
|---|---|---|
| `fp_model_env_fog` | `vp_static_env_fog` (60), `vp_static_lit_fog` (10), `vp_skinned_lit_fog` (2) | droids/chars (`c_drd*`,`c_prot*`,`n_astromech`), placeables (`plc_barrel/kiosk/pgen/bdroid`), some env trim. Biggest model shader. |
| `fp_model_diff_simple` | `vp_skinned_lit_fog` | skinned characters, plain diffuse. |
| `fp_model_diff_nolm` | `vp_static_lit_fog` | static models, no lightmap. |
| `fp_model_armor_legacy` | `vp_skinned_env_lit` | armored chars w/ env reflection (`pfbn10` body). |
| `fp_model_headgear_legacy` | `vp_static_env_fog` / `vp_static_lit_fog` | headgear/static accessories. |

> ⚠ **`lm` is unreliable for MODEL shaders.** On models TMU1 is usually an envmap or 2nd diffuse,
> not a true lightmap, so the 47 "with-lm" combos under `fp_model_env_fog` are not real lightmaps.
> The `lm` field is only trustworthy for `fp_worldtex*` (where TMU1 genuinely = lightmap).

Not seen in this census (exist in override set, just weren't drawn in the visited modules):
`fp_worldtex_bump_env_gamma`, `fp_worldtex_lm_env`, `fp_model_bump_env_spec(_b)`.
Post FPs seen (not receivers): `fp_post_composite_top/mid`, `fp_post_grading`.

## Role-based rename — APPLIED 2026-05-31 (with the GLSL port)
Dropped the misleading "interior/exterior" idea. Renamed by ROLE; kept the load-bearing
`fp_worldtex_`/`fp_model_` prefixes so `ClassifyCaster`'s `strncmp` and the per-file `@include`
shadow-split are unaffected. Each rename touched only: `shader_ident.txt` RHS + the override
`.txt` filename + the exact-name match in `glsl_program.cpp` (now `fp_worldtex_diffuse_main`).
| old | new | why |
|---|---|---|
| `fp_worldtex_lm_fog_alpha` | `fp_worldtex_diffuse_main` | THE primary lightmapped+alpha world surface, not "interior". |
| `fp_worldtex_env_fog` | `fp_worldtex_env_reflective` | env-reflective architecture (t2). |
| `fp_model_env_fog` | `fp_model_env_reflective` | matches worldtex naming for env surfaces. |
| (others) | kept | already role-clear (`diff_simple`, `armor_legacy`, `door`...). |

## Faithful GLSL port — `fp_worldtex_diffuse_main` (2026-05-31)
The `vp_static_lit_fog` + `fp_worldtex_diffuse_main` pair now has a faithful GLSL replacement in
`shaders_override/fp_worldtex_diffuse_main.{vs,fs}.glsl`, bound via `glUseProgram` override when the
engine binds that ARB pair. Ports both ARB stages instruction-for-instruction: 3-light LIT lighting,
world N/P, Schlick Fresnel, L0 Blinn-Phong spec, energy conservation, AO, directional sun + 4-tap
PCF shadow, sun-shadow contrast, fog. Every uniform is read CPU-side from the SAME `program.env`
slot the ARB read (`glGetProgramEnvParameterfvARB`), so inputs are identical to the ARB path.
- **Quirk reproduced for parity:** the ARB FP's "AO from normal.a" actually reads the clobbered
  `nrm.w` (= 1/len of the normal, ~1), not the texture alpha. The GLSL reproduces this exactly.
- **Toggle:** "GLSL material" tune slider → FRAGMENT `env[26].w`; default OFF, so A/B vs ARB in-game.
- Other env FPs (`fp_worldtex_env_reflective`, `fp_model_env_reflective`) were renamed only, not ported.

## Reference: all VPs observed
`vp_static_lit_fog`, `vp_static_env_fog`, `vp_static_displaced` (UI/legal),
`vp_worldtex_env_fog_t2`, `vp_skinned_lit_fog`, `vp_skinned_env_lit`, `vp_skinned_bump_env`,
`vp_shadow_caster`, `vp_shadow_caster_skinned`.
