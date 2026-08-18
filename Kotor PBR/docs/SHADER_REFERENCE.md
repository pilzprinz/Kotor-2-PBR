# Shader Reference

Roles of every identified ARB shader in KOTOR 2 (post-Aspyr + 3C-FD applied).

Validated via color/pattern tinting tests on multiple scenes (Peragus, Telos, Dantooine, Nar Shaddaa, M4-78, Ebon Hawk interior + crashed exterior).

## How shader resolution works

1. Engine compiles ARB program from string baked into `swkotor2.exe`
2. ShaderOverride DLL computes MD5 of that string
3. Looks up MD5 in `shader_ident.txt` → friendly name (e.g., `fp_worldtex_lm_fog_alpha`)
4. Dumps to `shaders_original/<name>.txt` (or `shaders_original/<hash>.txt` if unmapped)
5. Checks `shaders_override/<name>.txt` — if exists, substitutes content

### Hash → name aliases (don't be confused)

Hash `fpffceb52582fe3f52825bbae40d37a3df` maps to **`fp_worldtex_env_fog`** in the current `shader_ident.txt` but to `fp_worldtex_lm_env_fog` in legacy ident files (e.g., `ShaderOverride-master/Release/shader_ident.txt`). It is the **same shader** — engine compiles one ARB program, ident catalog naming changed.

If you find an old `shaders_original/fp_worldtex_lm_env_fog.txt` dump, it's the legacy name for this same program. Only create an override under the active ident name (`fp_worldtex_env_fog.txt`).

## Current PBR overrides

Files in `deploy/shaders_override/` actively replacing stock shaders:

| Override file | Replaces | Pipeline |
|---|---|---|
| `fp_model_env_fog.txt` | characters / placeables / objects | Schlick Fresnel + L0 Blinn-Phong spec + AO + emissive + energy conservation |
| `fp_worldtex_lm_env.txt` | walls + lightmap + envmap (no fog) | Same + lightmap term |
| `fp_worldtex_env_fog.txt` | walls + lightmap + envmap + fog | Same + fog + screen-space contact-shadow AO |
| `fp_worldtex_lm_fog_alpha.txt` | interior walls + LM + fog, non-env / alpha-aware (was `fp_walls_lm_fog`) | Full PBR: Schlick Fresnel, L0 Blinn-Phong spec, energy conservation, AO, emissive. Preserves diffuse alpha for punchthrough (grates, mesh). |
| `vp_static_lit_fog.txt` | static-geom lit-fog VP (no env) | Outputs world-space N → texcoord[4], world P → texcoord[5]. Pairs with `fp_worldtex_lm_fog_alpha`. |
| `fp_model_bump_env_spec.txt` | bumpmapped chars (lekku, droid plates) | Same as `fp_model_env_fog`; stock tangent-cube path replaced by world-space N + sibling normal at TMU 8 |
| `fp_model_bump_env_spec_b.txt` | variant b | Same |
| `fp_worldtex_bump_env.txt` | pure-reflective bump walls (M4-78 doors) | Stock pure-reflection path on non-PBR; PBR adds Fresnel + L0 spec |
| `fp_worldtex_bump_env_gamma.txt` | variant with `c7` gamma + `c1` tint | Same + stock gamma/tint preserved |
| `vp_static_env_fog.txt` | static-geom envmap-fog VP | Outputs world-space N → texcoord[4], world P → texcoord[5] for fp |
| `vp_worldtex_env_fog_t2.txt` | walls-env-fog VP variant | Same |
| `vp_skinned_env_lit.txt` | skinned char-env VP | Same (post-skin world-space N/P) |
| `vp_skinned_bump_env.txt` | skinned bump VP (chars, droids) | Same; replaces stock tangent-basis cube outputs (tc1/2/3) with cube reflect on tc1 + world N/P on tc4/5 |
| `vp_static_bump_env.txt` | static bump VP (walls, props) | Same |

`fp_model_armor_legacy.txt` and `fp_model_headgear_legacy.txt` **are now overridden** (added 2026-05): full sun shadow-receive + sun diffuse + self-shadow darken. These are the dominant biological-character body/head shaders (see canary map below), so without them characters cast/receive no sun.

`fp_model_diff_simple.txt` and `fp_model_diff_nolm.txt` **are now overridden** too — stock had NO sun/shadow; the override adds directional sun diffuse + sun-shadow receive (self path, TMU5 + `env[104..106]`) + sun-shadow contrast on the geometric world normal. These are the simple no-map character/object shaders.

Not yet overridden (PBR not active on these): `fp_worldtex_detail3`, `fp_model_*_gamma*`, `fp_model_skin_lm_gamma*`, `fp_model_lm_lut*`, `fp_model_weapons_legacy`, `fp_bump_screenspace`, terrain, hologram/door/UI.

See [PBR_PIPELINE.md](PBR_PIPELINE.md) for the math each override implements.

## GLSL material pair (replaces the ARB `vp_static_lit_fog` + `fp_worldtex_diffuse_main`)

`shaders_override/fp_worldtex_diffuse_main.{vs,fs}.glsl` is a **GLSL 1.20** port of that ARB pair, bound via `glUseProgram` when the engine binds it (gated by the "GLSL material" tune toggle + `glIsEnabled(GL_FRAGMENT_PROGRAM_ARB)` — see `ENGINE_LESSONS.md` §3.8). It mirrors the ARB math line-for-line; uniforms are read from the same `program.env` slots. Two intentional differences from a naive port, both for parity:

- **Fog is computed per-vertex in the VS** (`gl_FrontSecondaryColor`), matching the ARB VP's `result.color.secondary`, not per-pixel — so the wrapper mangling of fog built-ins hits both paths identically.
- **Flag-gated branches** (`fl.x/y/z/w`, `ns.y`) skip texture fetches + POWs the ARB multiplied out to zero. Output-identical because the flags are uniform per draw; saves up to 4 fetches + 2 POWs + 3 normalizes on flag-0 surfaces. This is the dominant (and sufficient) optimization — folding the remaining scalar multiplies would reorder FP rounding for ~2 ALU ops, not worth breaking parity.

Covers only static world geometry. **Characters and other world shaders remain ARB** (see gotcha #2 below).

## Lighting-model gotchas

### 1. Sun direction is fed RAW (un-normalized) → vector length is a hidden brightness multiplier

The sun dir lives in `env[27].xyz` and is dotted into `N·L` **without normalize** in *every* lit shader — ARB (`fp_worldtex_diffuse_main.txt:171`, `fp_model_diff_simple.txt:40`, etc.) and the GLSL FS. So the vector's **length scales the sun's brightness** on top of "Sun diff intens" (`env[28].x`). Moving a "Sun dir X/Y/Z" slider therefore changes **both angle and brightness** at once: e.g. `(0.3,0.85,0.45)` len≈1.008 vs `(0.3,0.85,1.0)` len≈1.34 ≈ +34% sun.

The shadow map does **not** share this: `ReadSunDir` `V3Norm`s its copy (`shadow_map.cpp:531`), so the shadow *angle* tracks but never brightens — the source of the asymmetry. Users exploit the raw length as a fine brightness trim (e.g. `[601dan]` saved `(0,0.2,0.2)` len 0.283 as a *dimmer* alongside `sun_diff_i=0.6`).

**Do not normalize one path only.** Normalizing the GLSL world FS while characters stay raw blew the world's sun ~3.5× brighter than the unchanged characters → world-vs-character mismatch. True decoupling requires normalizing **all** lit shaders (world ARB + GLSL + every `fp_model_*`) AND re-tuning every saved per-location calibration. Deferred; reverted. (2026-06-01)

### 2. Characters read darker than the world — structural, not a bug

Two independent reasons the same scene lights characters dimmer than the ground:

- **No lightmap.** World surfaces composite `vertexlight + lightmap` (baked brightness); `fp_model_*` characters get `vertexlight` **only** (`fp_model_diff_simple.txt:26`). The ground starts brighter before any sun is added.
- **Sun elevation favors floor normals.** A steep sun (e.g. normalized `(0,0.707,0.707)`, 45°) gives the up-facing floor `N·sun≈0.707` (strong) but a standing character's camera-facing sides face sideways → `N·sun≈0` (almost none). Raising shared "Sun diff intens" worsens the *ratio* (floor gains more than the character).

The **only mod-added darkener on characters** is the sun-shadow global contrast term (`fp_model_diff_simple.txt:47-58`): `r0 *= 1 - (1-shClip)·nl·shadowDarken`, floored at `shadow_floor`. With aggressive `shadow_darken` (0.75) + `shadow_floor=0`, self-shadow acne on a lit pixel crushes it toward ×0.25. Quick triage: drag "Shadow darken"→0 live; if characters pop brighter it's this term (fix via `shadow_bias`/`shadow_nrm_bias`), else it's the structural imbalance above (fix = a character-only fill/ambient add, not yet implemented). (2026-06-01)

## Categories

### 🧱 Worldtex / Static lit geometry

| Friendly name | Role |
|---|---|
| `fp_worldtex_lm_fog_alpha` | **Main interior walls shader, non-env / alpha-aware.** Diffuse × lightmap + fog. Used where diffuse alpha is NOT envmap mask: matte/concrete textures with no alpha, OR alpha is punchthrough (grating holes, mesh). Alpha passes through unchanged. Dielectric by default. Was named `fp_walls_lm_fog` before role was identified. |
| `fp_worldtex_env_fog` | Walls + lightmap + envmap (modulated by diff.alpha) + fog. Reflective floors, panels, windows. |
| `fp_worldtex_lm_env` | Same as above without fog. |
| `fp_worldtex_detail3` | Walls with 3-tex blend (diff + lightmap + detail). |

### 👤 Model (chars / placeables / items)

| Friendly name | Role |
|---|---|
| `fp_model_env_fog` | **Main character/object shader.** Diffuse + envmap (alpha-modulated) + fog. NPCs, droids, lightsabers, terminals, water, reflective items. |
| `fp_model_diff_simple` | Character without lightmap (outdoor Nar Shaddaa skin/body). |
| `fp_model_diff_nolm` | Wreckage / damaged objects fallback (no LM available). |
| `fp_model_armor_legacy` | Generic NPC body (clothes/armor, droid LED rings). Pre-Aspyr hash, still active. |
| `fp_model_headgear_legacy` | Heads/masks/HK-47 body. Pre-Aspyr hash. |
| `fp_model_weapons_legacy` | Weapons (legacy). |
| `fp_door` | Sliding doors. |
| `fp_hologram` | Hologram appearance shader. |
| `fp_model_diff_gamma` | Generic object diffuse with gamma (`c7` param). |
| `fp_model_diff_gamma_v` | Diff + vert color + gamma. |
| `fp_model_diff_gamma_c1` | Diff + const + gamma. |
| `fp_model_lm_gamma` | Lightmap + gamma. |
| `fp_model_lm_gamma_b` | LM + gamma variant b. |
| `fp_model_lm_gamma_dual` | LM + dual gamma DP3. |
| `fp_model_skin_lm_gamma` | Skin with LM + gamma. |
| `fp_model_skin_lm_gamma_sub` | Skin with subtract. |

### 🎭 Model with LUT (per-pixel color grading)

| Friendly name | Role |
|---|---|
| `fp_model_lm_lut` | Character + LM + LUT lookup (per-pixel color grading). |
| `fp_model_lm_lut_gamma` | Same + gamma. |
| `fp_model_lm_lut_sub` | LUT subtract variant. |
| `fp_model_lm_lut_add` | LUT additive variant. |

### 🌫️ Ambient / overlays

| Friendly name | Role |
|---|---|
| `fp_ambient_overlay` | **Global ambient/light overlay pass.** Applied on most outdoor surfaces. Without it, surfaces look unlit. |
| `fp_ambient_overlay_b` ⚠️ | Secondary ambient overlay (mid composite). *Dump exists, hash not yet mapped in shader_ident.txt.* |

### 🎨 Postprocess / fullscreen

| Friendly name | Role |
|---|---|
| `fp_post_composite_top` | Top fullscreen pass (under UI). |
| `fp_post_composite_mid` | Mid fullscreen pass. |
| `fp_post_grading` | Bottom fullscreen pass (color grading LUT). |
| `fp_post_lut3x1d` ⚠️ | Triple 1D LUT lookup. *Dump exists, hash not yet mapped in shader_ident.txt.* |
| `fp_post_lut3x1d_v` | Variant with vert color. |
| `fp_post_lut3d_sub` | 3D LUT subtract. |
| `fp_video_player` | BIK movie playback (surveillance cam / cutscene videos). |
| `fp_main_menu` | Main menu screen (color shift + POW). |
| `fp_gammaslider` | Gamma calibration slider. |
| `fp_speedblur` | Force speed edge blur. |

### ✨ Special effects

| Friendly name | Role |
|---|---|
| `fp_fx_telepathy_screen` | Krea telepathy fullscreen overlay. |
| `fp_fx_space_distort` | Telepathy edge space distortion. |
| `fp_fx_surveillance` | Door terminal / camera feed effect. |

### 🟩 Bumpmap (DOT3 normal map)

These shaders compute real DOT3 normalmap. Used only on specific surfaces (M4-78 doors, twi'lek lekku, a few armor items). Most KOTOR2 "bump" effect is actually envmap modulation via alpha — see `fp_model_env_fog`.

| Friendly name | Role |
|---|---|
| `fp_worldtex_bump_env` | Walls with DOT3 normalmap + cube reflection. |
| `fp_worldtex_bump_env_gamma` | Same with gamma. |
| `fp_model_bump_env_spec` | Character DOT3 + specular + cube. |
| `fp_model_bump_env_spec_b` | Variant b. |
| `fp_bump_screenspace` | Screen-space bump processing. |

### 🌌 Terrain / sky / special geometry

| Friendly name | Role |
|---|---|
| `fp_terrain_splat4` ⚠️ | 4-tile terrain blend (NOTE: actually used as ambient overlay — see `fp_ambient_overlay`). *Dump exists, hash not yet mapped in shader_ident.txt.* |

### 📺 UI

| Friendly name | Role |
|---|---|
| `fp_ui_2tex` | UI element with 2 textures. |
| `fp_ui_vertcolor` | UI vertex color output. |

## Vertex shaders (selected)

| Friendly name | Role |
|---|---|
| `vp_worldtex_legacy` | Walls (static geometry). |
| `vp_model_weapons_legacy` | Weapons / static envmap. |
| `vp_static_lit_fog` | Static, lights, fog (3C-FD patched). |
| `vp_static_env_fog` | Static + envmap reflect + fog. |
| `vp_worldtex_env_fog_t2` | Walls + envmap on T2 + fog. |
| `vp_skinned_lit_fog` | Bone-skinned characters + fog. |
| `vp_skinned_env_lit` | Skinned + envmap + lights. |
| `vp_skinned_bump_env` | Skinned + DOT3 normalmap + envmap. |
| `vp_skinned_displaced` | Skinned with displacement. |
| `vp_static_bump_env` | Static + DOT3 + envmap. |
| `vp_static_displaced` | Static + displacement. |
| `vp_gui_icon` | GUI icon vertex shader. |
| `vp_fog_volume` | Fog volume / atmosphere shader. |

## Key behavioral notes

### Envmap intensity = diffuse alpha

In most stock `*_env_fog` and `walls_lm_env` shaders, cube reflection is modulated by `(1 - diffuse.alpha)`. So:
- `diff.a = 1.0` → no reflection (concrete, fabric)
- `diff.a = 0.5` → medium reflection (dull metal)
- `diff.a = 0.0` → full mirror (polished metal, water)

This is how KOTOR encodes reflectivity without a separate spec map. PBR overrides keep this stock formula on the non-PBR path (when no `_R` sibling and no `roughness` sidecar entry), so existing stock textures don't lose their reflective look. The PBR path replaces it with `((1 − roughness) + Fresnel·rim) · reflectivity`.

For authoring PBR conversions:
- `roughness` map / sidecar replaces `1 − diff.alpha`
- `metallic` must be set explicitly (sidecar or `_M` map) — there is no auto-from-`envmaptexture` heuristic

### Bound cube ≠ TXI `envmaptexture` — an area can force it (2026-05-30)

The cube a surface actually reflects is **not always** the one its TXI names. A specific area/module can hard-bind a single cube for the whole scene that **overrides** the per-texture `envmaptexture` (and any loose Override of it). Confirmed on M4-78 **dro802**: surfaces authored for `CM_804DRO` reflect a forced greenish low-detail `cm_m478` instead — so reflections there look green/blurry while the same texture elsewhere reflects crisp `CM_804DRO`. This is **not a shader bug** (the FP reflects whatever cube is bound). Diagnose via file-log `LOCATION|envmap|id=N|name=?` (a constant `name=?` cube = stock/BIF, area-forced; a `PBR|forget|name=cm_X` means that cube is unloaded). Fix is content-side: drop `Override/<forced-name>.tga` (+ `.txi` `cube 1`), e.g. copy `CM_804DRO.tga` → `cm_m478.tga`. Full detail in `ENGINE_LESSONS.md` §1.9.

### Alpha semantics differ by shader family

`d.a` (diffuse alpha) carries different meaning depending on which shader the engine selects:

| Shader family | Alpha role |
|---|---|
| `fp_*_env_fog`, `fp_worldtex_lm_env`, `fp_*_bump_env*` (envmap variants) | Envmap reflectivity mask. Low `d.a` → mirror. High `d.a` → matte. Output alpha typically forced to `c[0].a`. |
| `fp_worldtex_lm_fog_alpha`, `fp_*_diff_*`, `fp_model_diff_nolm` (non-env variants) | Punchthrough / transparency. `d.a = 0` → cut out (grate hole, mesh gap). Output alpha **must** pass through unchanged. |

**Override implication**: env-shaders may safely overwrite output `d.a`; non-env shaders must preserve it. Wrong alpha in punchthrough shader → black holes where transparent cutouts should be.

### Metallic default by shader family

DLL global default `metallic = 0.0` is correct for **non-env** shaders (dielectric: walls_lm_fog_alpha, diff_simple, concrete/fabric). For **env-aware** shaders (`fp_*_env_fog`, bump variants), most stock K2 textures with `envmaptexture` are metallic by intent (chrome panels, droid plating, lightsaber blades). Future direction: either per-shader metallic default in the override FP itself, or auto-set `metallic=1` in DLL when `envmaptexture` key present in TXI.

Until DLL learns the per-shader convention, authors should set `metallic 1` explicitly in sidecar for stock env-textures during PBR conversion.

### Engine selects shader per material

Same model can use different shaders for different mesh parts (per-material). Example:
- Twi'lek body → `fp_model_env_fog`
- Twi'lek lekku → `fp_model_bump_env_spec` (separate material with bumpmap TXI)
- Big-robot body → `fp_model_env_fog`; its belt/tubes → `fp_model_armor_legacy` (canary 2026-05-30)
- A few droid bodies → a non-env/non-armor FP entirely (e.g. HK-47, one unique droid) — render unpainted in the canary test

### FP→object map (canary-validated 2026-05-30)

Tinting `fp_model_env_fog` **green** and `fp_model_armor_legacy` **blue** (constant `MUL` on final color), then walking the whole game, produced this map. Anything neither green nor blue uses some *other* FP (headgear/diff/gamma/lut) and is listed as "unpainted".

| Object | FP (this map) | Notes |
|---|---|---|
| Biological humanoid **bodies** (Exile, party humans, NPCs, Mandalore, Bao-Dur) | `fp_model_armor_legacy` (blue) | The dominant character-body shader. |
| **Droids** (GO-TO, T3-M4, generic droids, big walkers) | `fp_model_env_fog` (green) | Plus most reflective metal. |
| **Placeables** (generator, crates, terminals, floating remotes) | `fp_model_env_fog` (green) | |
| **Weapons** (lightsaber blades, blasters, vibroblades) | `fp_model_env_fog` (green) | Held items, not bodies. |
| **Masks / visors** (Exile mask), **hair** (Visas) | `fp_model_env_fog` (green) | Reflective accessory meshes. |
| Reflective **floor + ceiling** in a Dantooine interior | `fp_model_env_fog` (green) | Rendered as model/placeable, not worldtex — surprising but confirmed. |
| Biological **heads** (Bao-Dur head, one ship NPC head) | unpainted | → `fp_model_headgear_legacy` or a `diff` variant. Visas head was blue (armor) — head FP varies per character. |
| **HK-47** (whole body, everywhere incl. broken on the ship) | unpainted | Uses a non-env/non-armor FP (headgear/diff). Only character entirely outside both. |
| Exile **body + head in some ship scenes** | unpainted | Cutscene/small-space path swaps to a non-overridden FP. Visually no difference yet. |
| Belt/tubes on one large robot | `fp_model_armor_legacy` (blue) | Per-material split inside one droid. |

**Takeaways:**
- "Biological = blue (`armor_legacy`), mechanical/reflective = green (`env_fog`)" is the rule of thumb, with frequent per-material exceptions.
- A few meshes (HK-47, some heads, ship-scene Exile, one unique droid body) ride FPs we have **not** overridden → they get no PBR/shadow treatment. Tracked for later; not critical.
- Reload sanity: every object visibly took the tint after a **full restart**, confirming override `.txt` re-read works (see ENGINE_LESSONS §3.5 — only a full process restart re-uploads programs; save/zone reload reuses the cached compiled program).

### Shader routing by TXI / model material flags (hypothesis)

Engine inspects TXI sidecar + model material flags before binding shader. Routing heuristics observed:

| TXI / material state | Routed shader family |
|---|---|
| `envmaptexture <cube>` + matte diffuse | env-family (`fp_*_env_fog`, `fp_*_lm_env`) — alpha = env reflectivity mask |
| `bumpmaptexture` + envmap | bump-family (`fp_*_bump_env_*`) — DOT3 normal × env |
| no `envmaptexture`, alpha used for cut-outs | non-env / alpha-aware (`fp_worldtex_lm_fog_alpha`, `fp_*_diff_*`) — alpha = punchthrough |
| `proceduretype cycle` (animated) + glow alpha | **possibly** different shader path — env ignored, alpha = additive glow. See dro_cpanel case below |
| Static gamma tint (no env, no LM) | `fp_model_diff_gamma*` family with `c1`/`c7` params |

**dro_cpanel observation**: TXI has `envmaptexture CM_baremetal` + `proceduretype cycle numx 2 numy 2 fps 3`. Visually no envmap — instead alpha channel glows (white / self-tint), giving bloom-like panel highlights. Hypotheses:

1. Engine treats `proceduretype cycle` as additive-glow signal, overriding env routing.
2. Material flag in model (additive blend / self-illumination) forces non-env shader; envmaptexture in TXI is then ignored.
3. Alpha is interpreted as emission mask by whichever shader engine selects.

Not confirmed — needs engine-side reverse engineering to determine exact decision tree. Useful starting point: dump shader name on a known animated panel surface.

### World vs char/obj shader duplicates

For each major feature set, K2 ships TWO shaders — one for world geometry (has lightmap), one for chars/objects (no lightmap, per-vertex Lambert):

| Feature set | World variant (has LM) | Char/Obj variant (no LM) | Key difference |
|---|---|---|---|
| envmap + fog | `fp_worldtex_env_fog` (hash `ffceb525…`) | `fp_model_env_fog` (hash `629e81cb…`) | World: T1=LM 2D, T2=cube. Obj: T1=cube directly. |
| envmap (no fog) | `fp_worldtex_lm_env` | _(no direct dup)_ | World only — chars always get fog or per-vertex. |
| no envmap, matte | `fp_worldtex_lm_fog_alpha` | `fp_model_diff_simple`, `fp_model_diff_nolm` | World: LM × diffuse. Char: per-vertex × diffuse. |
| bump + env | `fp_worldtex_bump_env`, `fp_worldtex_bump_env_gamma` | `fp_model_bump_env_spec`, `fp_model_bump_env_spec_b` | World variant has no skinning; char variant has bone-weighted VP. |
| LM-based gamma | `fp_model_lm_gamma`, `_b`, `_dual` | `fp_model_skin_lm_gamma`, `_sub` | World skips bone weights; skin variant has skinning + subtract. |

**Why duplicates?** Different lighting model, not different quality:
- **World** = static geometry → engine bakes per-vertex/-texel lightmap offline → shader samples LM once per pixel. Cheap at runtime, high-quality static GI.
- **Chars/objs** = dynamic (skinned or moving) → can't bake LM (would have to re-bake per pose) → shader uses 3 dynamic point lights computed per-vertex in VP, output in `fragment.color.primary`. Slightly cheaper math but lower quality (no GI, no shadows).

Same visual fidelity goal, different geometric constraints. Not "world cheap, char pretty" — both equally optimized for their use case.

**Override implication**: each PBR override needs TWO files (or one parameterised) — one per variant. Currently the deploy ships pair-matched overrides for env+fog and bump families.

### Lightmap presence routes shader

Without baked LM (Nar Shaddaa outdoor, crashed Ebon Hawk):
- Characters → `fp_model_diff_simple`
- Objects → `fp_model_diff_nolm`

With baked LM:
- Characters → `fp_model_armor_legacy` / `fp_model_env_fog`
- Objects → `fp_worldtex_lm_*` / `fp_worldtex_env_fog`

### Postprocess stack order (bottom → top)

1. `fp_post_grading` (color grading LUT)
2. `fp_post_composite_mid` (ambient mid)
3. `fp_post_composite_top` (final composite)
4. UI layer

---

## PBR Override Pipeline (this mod)

Overrides replace selected stock ARB programs with PBR-aware versions. Sibling textures (`<name>_n`, `_r`, `_m`) bound to extra TMUs by wrapper hooks.

### Active overrides (FP)

| Friendly name | Role in PBR pipeline |
|---|---|
| `fp_worldtex_env_fog` | Walls/lightmapped + envmap + PBR. Diffuse × (vertex tint + lightmap), Schlick Fresnel, spec L0, spec baked sun, detail normal, roughness-LOD cube. Adds contact-shadow 4-tap on TMU 7 depth. |
| `fp_worldtex_lm_env` | Same as `fp_worldtex_env_fog` minus fog and contact shadow. |
| `fp_model_env_fog` | Characters/objects + envmap + PBR. Same feature set; uses `texcoord[1]` for cube reflect coord (vs world's `texcoord[2]`), texture[1] for cube. |
| `fp_model_bump_env_spec` | Bumpmapped chars/droids. Stock tangent-cube path discarded; uses sibling normal at TMU 8 + world N from VP. Same PBR math as `fp_model_env_fog`. |
| `fp_model_bump_env_spec_b` | Variant of above. |
| `fp_worldtex_bump_env` | Pure-reflective bumpmapped walls (M4-78). Stock path = env cube unmodulated (= stock 1:1). PBR adds Fresnel-conditioned env weight + L0 spec. |
| `fp_worldtex_bump_env_gamma` | Same + stock `c7` gamma + `c1` tint preserved. |

### Active overrides (VP) — Phase C wiring

VP outputs world-space normal/position so FP can do per-fragment lighting. Otherwise stock VP only outputs eye-space and per-vertex Lambert in `fragment.color.primary`.

| Friendly name | PBR additions |
|---|---|
| `vp_static_env_fog` | Adds `result.texcoord[4]=wNorm`, `texcoord[5]=wPos` (world). Stock outputs preserved. |
| `vp_worldtex_env_fog_t2` | Same — world normal/pos on tc4/5. |
| `vp_skinned_env_lit` | Skinned (bone-weighted) variant. Computes world normal/pos after bone blend. Pairs with `fp_model_env_fog` for NPCs/droids. |
| `vp_skinned_bump_env` | Skinned bump variant. Stock tangent-basis cube outputs (tc1/2/3) replaced with cube reflect on tc1 + world N/P on tc4/5. Pairs with `fp_model_bump_env_spec*`. |
| `vp_static_bump_env` | Static bump variant. Same output layout. Pairs with `fp_worldtex_bump_env*` and static `fp_model_bump_env_spec` uses. |

### FP attribute layout (after override)

```
fragment.color.primary    = stock per-vertex Lambert + ambient (v)
fragment.color.secondary  = fog factor (f)
fragment.texcoord[0]      = diffuse UV (T)
fragment.texcoord[1]      = cube reflect coord (obj) / extra UV (world)
fragment.texcoord[2]      = cube reflect coord (world)
fragment.texcoord[4]      = world-space normal (N4)   ← Phase C
fragment.texcoord[5]      = world-space position (P5) ← Phase C
```

### TMU bindings (set by wrapper)

| TMU | Content | Source |
|---|---|---|
| 0 | Diffuse | Stock |
| 1/2 | Cube envmap | Stock (TXI `envmaptexture`) |
| 8 | Normal map | Sibling `<name>_n.tga` |
| 9 | Roughness | Sibling `<name>_r.tga` (R channel) |
| 10 | Metallic | Sibling `<name>_m.tga` (R channel) |

TMU 11+ rejected by Aspyr wrapper — packed extra channels:
- normal.w = AO
- rough.w = emissive mask

### program.env slot map (PBR-relevant)

| Slot | Use |
|---|---|
| `env[20]` | `pbr` = (metallic, roughness, _, emissive) sidecar scalars |
| `env[21]` | `fl` = (useNrm, useRough, useMetal, useAO) feature gates |
| `env[22]` | `ns` = (normalStrength, emissiveStrength, cavityStrength, fresnelRim) |
| `env[23]` | `ux` = (reflectivity, _, _, _) |
| `env[86]` | L0 diffuse color |
| `env[87]` | L0 world position |
| `env[89..92]` | Inverse view matrix (world transform). `.w` columns = camera world pos |

### PBR FP composition order (current — conservative ~70 instr budget)

```
1. Sample d/m, optionally l (lightmap). Cube on TMU 1 (obj) or 2 (walls/world).
2. Sample nrm/rough/metal siblings on TMU 8/9/10.
3. Material scalars:
     rs = pbr.y < 0 ? 1.0 : pbr.y   (CMP fallback)
     rs = lerp(rs, rough.r, fl.y)
     mt = lerp(pbr.x, metal.r, fl.z)
4. usePbr = (pbr.y >= 0) OR fl.y  — gates env/kd between stock vs PBR paths.
5. Unpack normal, perturbed world normal N = normalize(N4 + nrm·fl.x).
6. World view V from env[92..90].w - P5.
7. F0 = lerp(0.04, d, mt). Schlick fresnel; F = lerp(F0, 1, fres^5).
8. Stock K2 base: r = (v+l)·d  (obj has no l)
9. Energy conservation: r *= lerp(1, (1-F)·(1-mt), usePbr).
10. Emissive add: r += d · rough.a · pbr.w · ns.y.
11. Light proxy: lit.x = clamp(luma(v+l)·0.75+0.25, 1) — gated by usePbr.
12. AO from normal.a: ao = clamp(1 + (nrm.a - 1)·fl.w·ns.z, 0, 1).
13. Envmap weight:
     stock = (1 - d.a) · ux
     pbr   = (1 - rs) · ux + fres · ns.w
     env   = lerp(stock, pbr, usePbr) · lit.x · ao
14. L0 spec (Blinn-Phong ^16, F-tinted, ×(1-rs)·fl.x). Added to r.
15. Envmap composite: s = lerp(m, m·d, mt) · env;  d.rgb = r + s.
16. Apply AO globally: d.rgb *= ao.
17. (world variant) Contact shadow 4-tap on TMU 7 depth, count-gated.
18. (optional) ACES luminance tonemap.
19. Fog blend with state.fog.color, alpha set from c[0].
```

**Key gates summary:**
- `fl.x` (useNrm): perturb normal, enable L0 spec
- `fl.y` (useRough): sample rough.r, drives `usePbr`
- `fl.z` (useMetal): sample metal.r, enable metal tint
- `fl.w` (useAO): apply normal.a AO
- `usePbr` (derived): gate envmap formula + energy conservation + light proxy

### Critical correctness rules

- **No per-fragment Lambert.** Stock VP already does per-vertex Lambert routed through `fragment.color.primary`. Adding per-fragment = double-count → overbright.
- **Envmap NOT multiplied by Schlick F.** Dielectric F=0.04 would kill reflection. Use stock-style `lerp(m, m*d, metallic) × refl` instead; Fresnel only as edge boost on `refl.x` via `fresnelRim`.
- **Spec gated by `fl.x` (useNrm).** Otherwise spec leaks onto every untouched stock texture.
- **Envmap path gated by roughness-info presence, NOT by `fl.x`.** Compute `usePbr = fl.y OR (pbr.y ≥ 0)`. Textures with only `_n.tga` (normal map, no roughness sidecar/sibling) keep stock K2 envmap behavior — otherwise `rs=1` default makes `(1-rs)·ux = 0` and surface goes matte.
- **Metal albedo zero gated by `fl.z` (useMetal).** Default `pbr.x` may be non-zero in unconfigured state; multiplying `d.rgb * (1-mt)` without gate can darken stock textures unexpectedly.
- **Energy conservation `kd = (1-F)·(1-mt)` gated by `usePbr`.** Stock textures (no rough info) get `kd=1` (no drain). PBR-configured surfaces get proper energy split — diffuse loses what spec/env takes.
- **`fresnelRim` defaults to 0.0.** Non-zero default floods stock textures with orange rim.
- **Detail normal blend on `.xy` only.** Whiteout: `MAD nrm.xy, nrmD, weight, nrm` then renormalize. Integer scales (×4) cause visible cross tiling — use ×8 weight 0.3.
- **AO must clamp `[0,1]` after cavityStrength blend.** `cavityStrength > 1` can push AO negative (`MAX ao, 0` + `MIN ao, 1`). Negative AO inverts downstream `LRP` blends — corners lighten instead of darken.
- **Cube LOD blur gated by `usePbr`, NOT `fl.x` (fixed 2026-05-30).** The roughness-LOD bias is `rs · lod_scale · usePbr`. Gating by `fl.x` (useNrm) was a bug: on the stock path `usePbr=0` forces `rs=1` (rough sentinel), so a stock texture that merely *has an authored normal map* (`fl.x=1`) but no roughness got bias `1·5·1 = 5` = **maximum cube blur** — reflections lost all detail (visible on the generator + many droids, worst in dim zones where only the blurred gradient remains). Gating by `usePbr` keeps stock textures at bias 0 (sharp, stock-K2) and only blurs surfaces with real roughness info. Applies to all 7 env FPs.

### Stock K2 envmap pipeline (confirmed from dumps)

All three envmap shaders (`fp_worldtex_env_fog`, `fp_model_env_fog`, `fp_worldtex_lm_env`) use the same model:

```
r = (v + l) · d            ; diffuse base (l absent for obj variant)
s = m · (1 - d.a)          ; envmap weighted by INVERTED alpha
out = r + s                ; ADDITIVE composite
```

**Key:** `d.a` is the **inverted** reflectivity mask. `d.a=1` (opaque) → zero envmap (matte). `d.a=0` → full mirror. Stock textures with metallic surfaces store low alpha in `.a` channel. This is **NOT** a LERP replacement — `r` (diffuse) is always added, just goes near zero for fully-reflective pixels where artist stored `d.rgb≈0` in source.

**PBR override path** runs the same ADD model but replaces the `(1-d.a)` weight with `(1-rs)·ux + fres·ns.w` when `usePbr=1`.

### `pbr.y = -1` sentinel handling

`pbr_state.cpp` initialises `roughness = -1.0f` when sidecar missing. Shader must convert sentinel to a usable scalar:

```
CMP rs.x, pbr.y, 1.0, pbr.y   ; pbr.y < 0 → rs = 1.0 (fully rough, no GGX/Blinn spec)
LRP rs.x, fl.y, rough.r, rs.x ; if R sibling, sample it instead
```

**Do not** fall back to `d.a` for roughness — `d.a` is reflectivity mask in stock K2 (see envmap pipeline above). Mixing semantics breaks both modes.

### `fl.x/y/z/w` derived from sibling **file presence**, NOT txi keys

Per `pbr_state.cpp:430-435`:
```cpp
PushAllEnvParams(s->params,
    (s->normalId && !s->normalSynth) ? 1.0f : 0.0f,    // useNrm
    (s->roughId  && !s->roughSynth)  ? 1.0f : 0.0f,    // useRough
    s->metalId                       ? 1.0f : 0.0f,    // useMetal
    s->aoPacked                      ? 1.0f : 0.0f,    // useAO
    s->emiPacked                     ? 1.0f : 0.0f);
```

Sidecar key `usenormalmap=0/1` in txi is **ignored** — flag depends on whether `<name>_n.tga` exists on disk. To "disable" normal map, rename or remove the file. Synthetic placeholders (white-RGB host created to package alpha-only siblings) explicitly skipped so they don't masquerade as real maps.

## ARB FP1.0 driver limits (HARD constraints)

ARB_fragment_program 1.0 minimum guarantees (Aspyr wrapper may enforce strictly):

| Resource | Min guaranteed | Notes |
|---|---|---|
| ALU instructions | 72 | Total `MUL`, `MAD`, `LRP`, etc. |
| Texture instructions | 24 | `TEX`, `TXB`, `TXP` |
| Total instructions | 96 | ALU + TEX combined |
| Temporaries | 32 | Some legacy drivers cap at 16 |
| Program parameters | 24 | Including literals declared inline |
| Attribs | 10 | `fragment.color.*`, `texcoord[N]` |

**Failure mode:** if shader exceeds limit, `glProgramString` sets `GL_INVALID_OPERATION`. Aspyr wrapper does **not** check or log this — engine binds non-functional program → black output where shader is active. No console error, no in-game indication. Symptoms identical to "renders to zero".

**Diagnosing budget overflow:**
```bash
grep -c -E "^[A-Z]{3,4}[[:space:]]" deploy/shaders_override/fp_*.txt
```
Stays under ~70 for safety margin. Strip features (extra spec lights, detail normal, ACES tonemap) first if approaching limit.

**Disallowed opcodes (FP):**
- `DST` — `ARB_vertex_program` only. Use MAD chain for `1 + lin·d + quad·d²` attenuation:
  ```
  MUL dist.x, distSq.x, invDist.x        ; dist = dist²·(1/dist)
  MAD atten.x, distSq.x, Lat.z, Lat.x    ; const + quad·dist²
  MAD atten.x, dist.x, Lat.y, atten.x    ; + lin·dist
  RCP atten.x, atten.x
  ```

**LRP gotchas:**
- Literal scalars in LRP middle slot work but some drivers picky: `LRP F.rgb, fres.x, 1.0, F0` valid per spec, broadcasts `1.0` to vec4.
- Reading and writing same register OK per spec: `LRP tmp.rgb, fl.x, tmp, 1.0`.

## Hash-mapped vs unmapped shaders

| Status | Examples |
|---|---|
| **Mapped + overridden** | `fp_worldtex_env_fog`, `fp_model_env_fog`, `fp_worldtex_lm_env`, `fp_worldtex_lm_fog_alpha`, `fp_model_bump_env_spec`/`_b`, `fp_worldtex_bump_env`/`_gamma` |
| **Mapped, no override** | `fp_worldtex_detail3`, `fp_model_diff_*`, `fp_model_skin_lm_gamma*`, `fp_model_lm_lut*`, `fp_model_weapons_legacy`, etc. (`fp_model_armor_legacy`/`fp_model_headgear_legacy` are now overridden — shadow receive) |
| **Unmapped (dumped only)** | `fp_ambient_overlay_b`, `fp_post_lut3x1d`, `fp_terrain_splat4` — friendly-named dumps with no shader_ident hash entry. Source unclear (likely manual renames from older ident catalog). |
| **Stale alias dumps** | Historical: pre-rename names `fp_walls_lm_env_fog.txt` and `fp_char_env_fog.txt` had same MD5 as current `fp_worldtex_env_fog` / `fp_model_env_fog`. Already deleted from repo. |
| **Unknown VP hashes** | `vp1f50e05b...`, `vpabadd933...`, `vpfb3e592e...` — three legacy/skinned variants, see [Categories](#-vertex-shaders-selected) for content sketch. |

**Real PBR coverage gap** — most non-env shaders (`fp_worldtex_detail3`, `fp_model_diff_*`, `fp_model_lm_gamma*`, etc.) still on stock. `fp_worldtex_lm_fog_alpha` now has a DEBUG override (red-tint test). Strip red and expand sibling-driven additions when ready.

## Recent additions (extended pipeline)

Current shaders include features beyond Phase C baseline:

- **Roughness-LOD cube reflection** — env cube sampled via `TXB` with bias `rs · 5 · usePbr`. Polished surfaces (rs=0) → sharp mip 0. Matte (rs=1) → mip 5 blur. Applied across all 7 env-shaders. Stock textures (usePbr=0) keep bias 0 → identical to stock K2.
- **Perturbed cube reflect (30% mix) + env boost** — `R = 2(N·V)N - V` with 30% perturb on PBR path (fl.x=1), pure VP on stock. Env weight ×3 when fl.x=1 to compensate scatter-loss → reflection brightness matches stock K2 strength while gaining bump-direction modulation. Edit `MUL tmp.x, fl.x, 2.0;` literal to tune boost (2.0 = 3× total; higher = brighter PBR reflections).
- **Baked sun spec** — secondary Blinn-Phong ^16 highlight at hardcoded sun direction `(0.3, 0.85, 0.45)` overhead-front. F-tinted, ×0.15 weight, gated by `fl.x`. Adds ambient sheen on rims/edges independent of dynamic lights. Applied to all 8 PBR shaders.
- **Detail normal (8× UV)** — same `_n.tga` resampled at 8× UV tiling, whiteout xy-blend into base normal with weight 0.3. High-frequency surface detail without extra TMU. Wasted ALU on stock textures (fl.x=0 → N=N4), but safe (no propagation).
- **Environment irradiance (diffuse IBL)** — env cube sampled at mip 5 (max blur) along world N. Ambient diffuse contribution `irr · d · (1-F) · (1-mt)` gated by `usePbr` (zero on stock textures, preserves K2 baseline). Surfaces in shadow now pick up ambient colour from surrounding environment. Applied to 5 env shaders with diffuse: `fp_model_env_fog`, `fp_worldtex_env_fog`, `fp_worldtex_lm_env`, `fp_model_bump_env_spec`/`_b`. Skipped on pure-reflective walls_bump variants (no diffuse).
- **Per-shader metallic default via sentinel** — DLL initialises `metallic = -1.0` (sentinel). Shader uses `CMP mt.x, pbr.x, default, pbr.x` to pick: env-shaders default `1.0` (stock K2 env-textures are typically metallic), non-env (`fp_worldtex_lm_fog_alpha`) defaults `0.0` (dielectric). Explicit `metallic=X` in TXI sidecar overrides.
- **AO from normal.w** — packed in normal sibling alpha, blended toward 1 by `useAO · cavityStrength`. Applied globally to `d.rgb` (direct + indirect; K2 vertex light mixes both, can't split).
- **Emissive from rough.w** — packed in rough sibling alpha. Additive, not modulated by env/lit (glows in shadow). Scaled by `pbr.w · ns.y`.
- **Energy conservation** — diffuse multiplied by `(1-F) · (1-metal)` on PBR path. Gated by `usePbr` so stock textures retain K2 brightness.
- **Light proxy `lit.x`** — `luma(v+l) · slope + floor, MIN 1` gated by `usePbr`. Modulates `env` so dark areas show less reflection. Stock path unmodified to preserve K2 baseline.
  - World FPs: `slope 0.75, floor 0.25` (`envFlr`).
  - **Model FPs (`fp_model_env_fog`, `fp_model_bump_env_spec`/`_b`): `slope 0.5, floor 0.5`** (changed 2026-05-30). Reason: on the PBR path model reflections were gated hard by *local vertex light*, so in dim/red-fog zones (dro802) the same droid/placeable reflected ~35% of a bright zone. Halving the vertex-light dependence keeps dim-zone reflections at ~58% (the cube map already encodes environment brightness; gating again double-darkens).
  - **CAVEAT (learned 2026-05-30):** the `lit` gate is *itself* gated by `usePbr` (`LRP lit.x, usePbr.x, lit.x, 1.0`). Stock textures with no roughness sibling have `usePbr=0` → `lit=1` → **the envFlr change does nothing for them.** Most stock droids/placeables are stock-path, so their dim-zone dimness comes from a **dark cube map**, not the `lit` gate. The real lever for them: **`envBoost` (`tnC.x`, "Env reflect boost" slider) — now wired into the model FPs UNGATED** (`MAD env.x, env.x, tnC.x, env.x`), so it brightens stock-path reflections too (previously env boost was walls-only). Raise the slider in dim zones.
- **Envmap diffuse tint when metallic** — `s = LRP(metal, m*d, m)`. Non-metals reflect colour-neutral; metals tint the reflection by the diffuse albedo.
- **Contact shadows** (world variant only) — 4-tap depth on TMU 7, count-gated to distinguish silhouettes from corners. Sample depth on TMU 7, accumulate positive diffs in window `[0, falloff_px]`, count occluders ≥ `min_diff`, multiply final occlusion by `clamp(count - 1.5)`. Adds ~25 instructions.

  **Known limitation:** Depth-only without view-space position reconstruction cannot cleanly distinguish a contact diff (corner valley) from a silhouette diff (object edge). Multi-direction count gate mitigates partially — corners that occlude in 2-4 cardinal directions pass, silhouettes (1 direction) get suppressed. But near-camera silhouettes where char depth is close to background depth still slip through.

  Real fix requires pushing the projection matrix to env params and reconstructing view-space position per sample — pending matrix-push DLL work. See `DEPTH_CAPTURE.md` for status.

### Tonemap

Removed. Earlier iterations included a Narkowicz ACES luminance rolloff:
```
T = L·(2.51L+0.03) / (L·(2.43L+0.59)+0.14)
d.rgb *= T/L
```
It boosted midtones (L≈0.2-0.5) ~15-25% before compressing highlights, which washed out shadows and weakened the K2 baseline contrast. Hard clamp at 1.0 (stock GL behaviour) matches the original game look more closely. Restore via the formula above only if a separate HDR display path is added later.

### VP/FP pairing

| FP | VP (PBR override) | Stock VP fallback |
|---|---|---|
| `fp_worldtex_env_fog` | `vp_worldtex_env_fog_t2` | walls (legacy) |
| `fp_worldtex_lm_fog_alpha` | `vp_static_lit_fog` | walls (legacy) |
| `fp_model_env_fog` (static) | `vp_static_env_fog` | weapons (legacy) |
| `fp_model_env_fog` (skinned) | `vp_skinned_env_lit` | (skinned legacy) |
| `fp_worldtex_lm_env` | `vp_worldtex_env_fog_t2` | walls (legacy) |
| `fp_model_bump_env_spec` (skinned) | `vp_skinned_bump_env` | (legacy) |
| `fp_model_bump_env_spec` (static) | `vp_static_bump_env` | (legacy) |
| `fp_model_bump_env_spec_b` | same as above per-instance | same |
| `fp_worldtex_bump_env` | `vp_static_bump_env` | walls (legacy) |
| `fp_worldtex_bump_env_gamma` | `vp_static_bump_env` | walls (legacy) |

If FP override active but VP not overridden, `texcoord[4]/[5]` undefined → garbage lighting. Both must ship together.

### Sibling resolution

Wrapper hooks `CreateFileA/W` via IAT. When game opens `foo.tga`, wrapper also opens `foo_n.tga`, `foo_r.tga`, `foo_m.tga` if present, uploads to TMU 8/9/10. Sidecar `foo.txi` extensions parsed for scalar overrides (`metallic`, `roughness`, `reflectivity`, `fresnelRim`, `emissive`, `normalstrength`, `cavityStrength`, `useNormalMap`).

### TGA orientation gotcha

KOTOR engine expects bottom-up texture data (GL convention). TGA descriptor bit 5 (`0x20`) = top-left origin. Loader must V-flip when bit is set, NOT when clear. Saving sibling normals as top-left without flip = upside-down normals (highlights inverted, "circles" pattern on flat surfaces).

### Siblings MUST be power-of-2 (2026-05-30)

Sibling textures (`_n`/`_r`/`_m`) **must be power-of-2** (256, 512, 1024, 2048, 4096…). NPOT siblings cannot form a complete mip chain on the Aspyr/K2 driver, so a minified normal map samples its base texel grid → a fine regular **moiré "mesh"** across all normal-mapped surfaces, revealed by directional sun (`sun.z > ~0.1` makes the sun term non-zero, which exposes the aliased normals). Symptoms reported as "зернистость/сетка". `detail_blend=0` does **not** remove it (the *base* normal aliases too), and a screen-space dither change barely touches it — both tells that it's texture aliasing, not the shadow dither.

`TgaUploadAsTexture` now logs `[pbr] WARNING: sibling texture WxH is non-power-of-2 …` per offending file and falls back to plain `GL_LINEAR` (no mip) for NPOT so the texture is at least *complete* — but it still aliases under minification. **Real fix is content:** re-export the normal at a clean POT size matching the diffuse's aspect (e.g. 2048² or 4096²), no padding. This also fixes content-shift misalignment if the export tool padded to a non-POT width (e.g. an AI generator emitting 2080×2048 — both the mesh aliasing *and* the bump-vs-diffuse misalignment vanish once it's a clean POT).
