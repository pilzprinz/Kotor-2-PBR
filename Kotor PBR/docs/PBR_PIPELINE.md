# PBR Pipeline

Shader-side formulas + DLL→shader parameter layout for the PBR overrides.

## Sibling texture packing

KOTOR 2 (Aspyr GL wrapper) **rejects `glActiveTexture(GL_TEXTURE11)` and above**. Only TMU 0–10 usable. 5 PBR maps (n / R / M / O / E) must compress into 3 TMUs.

| TMU | Content | Source files |
|---|---|---|
| 8 | Normal RGB + AO in `.a` | `<base>n.tga` + `<base>O.tga` |
| 9 | Roughness `.r` + Emissive mask in `.a` | `<base>R.tga` + `<base>E.tga` |
| 10 | Metallic `.r` | `<base>M.tga` |

Pack happens in `pbr_state.cpp::LoadSiblingPacked`. Alpha source resampled nearest-neighbor if dims differ.

**Synthesis fallbacks:**
- `<base>O.tga` alone → white-RGB normal synthesized so AO has a host. Flag `normalSynth=true` keeps shader from using fake normal.
- `<base>E.tga` alone → white-RGB rough synthesized. `roughSynth=true` so shader skips fake roughness.

## Env parameter layout

DLL pushes per-texture material data via `glProgramEnvParameter4d` to four registers:

| Reg | Var (in shader) | x | y | z | w |
|---|---|---|---|---|---|
| `env[20]` | `pbr` | metallic | roughness | fresnelF0 | emissiveScale |
| `env[21]` | `fl` | useNormal | useRough | useMetal | useAO |
| `env[22]` | `ns` | normalStrength | useEmissive | cavityStrength | fresnelRim |
| `env[23]` | `ux` | reflectivity | — | — | — |

`use*` flags are 0/1 booleans derived from sibling presence (and `!*Synth` for normal/rough). `metallic`/`roughness`/`emissive`/etc. come from `.txi`/`.pbr` sidecar with defaults in `ApplyPbrDefaults`.

Other engine env regs the shaders read:
- `env[0]` — global alpha
- `env[10]` — viewport (w, h, 1/w, 1/h) — used for screen-space AO UV
- `env[86..87]` — L0 light diffuse / position (KOTOR 2 stock convention)
- `env[90..92].w` — eye→world transform columns, packed into `.w` of three regs (camW reconstruction)

## Material math (in fp_worldtex_env_fog / fp_worldtex_lm_env / fp_model_env_fog)

### Material scalar resolve
```
CMP rs.x, pbr.y, 1.0, pbr.y;       # rs = (rough < 0) ? 1.0 : rough  (sentinel -1)
LRP rs.x, fl.y, rough.r, rs.x;     # if useRough, sample texture; else sidecar
LRP mt.x, fl.z, metal.r, pbr.x;    # same for metal
SGE usePbr.x, pbr.y, 0.0;          # have material info?
MAX usePbr.x, usePbr.x, fl.y;
```

### Normal blend
```
MAD nrm.xyz, nrm, 2.0, -1.0;       # decode [0,1]→[-1,1]
... normalize nrm ...
MUL N.xyz, nrm, fl.x;              # scale by normalStrength
ADD N.xyz, N, N4;                  # blend with interpolated geometric N (texcoord[4])
... normalize N ...
```

### Schlick Fresnel + F0
```
LRP F0.rgb, mt.x, d, dF0;          # F0 = lerp(0.04, diffuse, metal)
DP3 NdotV.x, N, V;
MAX NdotV.x, NdotV.x, 0.0;
SUB fres.x, 1.0, NdotV.x;
MUL tmp, fres, fres;
MUL tmp, tmp, tmp;
MUL fres.x, tmp.x, fres.x;         # (1-NdotV)^5
LRP F.rgb, fres.x, 1.0, F0;        # F = lerp(F0, 1, fres)
```

### Energy conservation
```
SUB kd.x, 1.0, fres.x;
SUB tmp.x, 1.0, mt.x;
MUL kd.x, kd.x, tmp.x;             # kd = (1-F)(1-metal)
LRP kd.x, usePbr.x, kd.x, 1.0;     # bypass for stock textures
MUL r.rgb, r, kd.x;                # scale diffuse only
```

Gated by `usePbr` so non-PBR textures stay at vanilla brightness.

### Emissive (additive, unlit)
```
MUL tmp.x, rough.a, pbr.w;         # E mask * emiScale
MUL tmp.x, tmp.x, ns.y;            # * useEmi flag
MAD r.rgb, d, tmp.x, r;            # r += diffuse * factor
```

Uses **original diffuse `d`** before `d.rgb` gets overwritten by composite. Inserted *before* envmap composite. Self-illumination — visible in shadow.

### AO (cavity)
```
SUB ao.x, nrm.a, 1.0;
MUL tmp.x, fl.w, ns.z;             # useAO * cavityStrength
MAD ao.x, ao.x, tmp.x, 1.0;        # 1 + (nrm.a - 1) * useAO * cav
MAX ao.x, ao.x, 0.0;
MIN ao.x, ao.x, 1.0;               # clamp [0,1]
```

**Clamp required.** Without it, `cavityStrength > 1` produces negative `ao` for dark pixels → downstream `LRP fog` wraps to fog color → corners turn LIGHTER not darker.

Applied twice in pipeline:
1. `MUL env.x, env.x, ao.x` — modulates env reflection (cavity = less reflection)
2. `MUL d.rgb, d, ao.x` — global mul after env composite (cavity = darker overall)

Step 2 is the visible one. Step 1 alone is too subtle when env contribution is small (indoor).

### Envmap weight
```
SUB env.x, 1.0, d.a;               # stock K2: weight by (1 - diffuse.alpha)
MUL env.x, env.x, ux.x;            # * reflectivity
SUB tmp.x, 1.0, rs.x;              # PBR: weight by (1 - rough) + fres*rim
MAD tmp.x, fres.x, ns.w, tmp.x;
MUL tmp.x, tmp.x, ux.x;
LRP env.x, usePbr.x, tmp.x, env.x; # pick stock-vs-PBR
MUL env.x, env.x, lit.x;           # modulate by local light proxy
MUL env.x, env.x, ao.x;            # AO modulates env (cavity occlusion)
```

`lit` is luma of `v` (and `v+l` for lightmapped variants) with soft floor 0.25. Prevents env reflection on shadowed pixels appearing "glowing".

### L0 specular (Blinn-Phong ^16, gated by usePbr)
```
... compute H, NdotH ...
MUL NdotH^16 (four MUL self-squares)
SUB tmp.x, 1.0, rs.x;
MUL NdotH.x, NdotH.x, tmp.x;       # rough damps spec
MUL NdotH.x, NdotH.x, usePbr.x;    # only on PBR pipeline
MUL spec.rgb, L0diff, F;
MAD r.rgb, spec, NdotH.x, r;
```

Only L0 (sun / primary directional). L1-L7 not yet wired.

## Shader files

| File | Stock K2 role | What we override |
|---|---|---|
| `fp_worldtex_env_fog.txt` | walls + lightmap + envmap + fog (most indoor surfaces) | Full PBR pipeline + screen-space contact-shadow AO |
| `fp_worldtex_lm_env.txt` | walls + lightmap + envmap (no fog) | Same minus fog and contact-shadow |
| `fp_model_env_fog.txt` | chars / placeables / objects + envmap + fog (no lightmap) | Same minus lightmap |
| `vp_static_env_fog.txt` / `vp_worldtex_env_fog_t2.txt` / `vp_skinned_env_lit.txt` | corresponding vertex programs | Add world-space N + P to texcoord[4]/[5] for per-fragment lighting |

`fp_worldtex_lm_fog_alpha` (lightmap + fog, no env) NOT yet overridden — many flat-surface indoor textures use this and stay vanilla.

## Hash collisions / renames

`fp_worldtex_env_fog` (current ident) maps to MD5 `ffceb52582fe3f52825bbae40d37a3df`. Older ident files map the same hash to `fp_worldtex_lm_env_fog`. Same shader, renamed in catalog. Don't create both overrides — only the current ident name resolves.
