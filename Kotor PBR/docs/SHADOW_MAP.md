# Shadow map subsystem

Real shadow-mapped sun shadow for KOTOR 2. Replaces the screen-space sun shadow attempt that was cut in 0.3 (see `ENGINE_LESSONS.md` §4.2).

Cross-refs:
- `ARCHITECTURE.md` — DLL components + matrix env-slot layout
- `ENGINE_LESSONS.md` — engine + driver quirks the design has to work around
- `source/shadow_map.cpp` — implementation
- `source/shadow_map.h` — public API
- `deploy/shaders_override/vp_shadow_caster.txt` — caster VP
- `deploy/shaders_override/fp_worldtex_env_fog.txt` — sampler block (walls)

---

## 1. High-level pipeline

```
per frame
  ┌── BeginCasterPass (from glClear hook, depth-bit set)
  │     - clear shadow FBO depth
  │     - BuildLightK: read view_inv (env[90..92]) + sun (env[27])
  │       → compose K_main = lightProj * lightView
  │              K_caster = K_main * view_inv
  │     - push K_main to FP env[100..103]   (used by sampler in main FP)
  │
  ├── per opaque draw (my_glDrawElements / Arrays / Begin)
  │     - BeforeDraw:
  │         save VP id + VP/FP enabled state + viewport + colormask
  │         bind shadow FBO + viewport 2048² + colormask FALSE
  │         bind shadow caster VP, glEnable VP, glDisable FP
  │         push lightMVP = K_caster * engine_modelview to VP env[100..103]
  │     - real_draw(...)   # rasterizes into shadow FBO (depth only)
  │     - AfterDraw:
  │         unbind FBO, restore viewport + colormask + VP id + enabled state
  │     - real_draw(...)   # engine's intended draw to main FB
  │
  ├── EndCasterPass (from SwapBuffers hook)
  │     - bind shadow tex to TMU 6 (constant SHADOW_TMU)
  │     - diagnostics: per-(fp,vp) histogram + center-tile depth stats
  │
  └── main FP samples shadow tex via K_main * P5  (P5 = world position)
```

Order matters: `BeforeDraw` fires *before* the engine's intended draw so the shadow side runs first; this avoids stomping the engine's bound VP/FP state until we've grabbed it.

## 2. Matrices

Two distinct world-space transforms live in our system. Confusing them was the longest-standing bug.

| Matrix | Composition | Pushed to | Used by | Notes |
|---|---|---|---|---|
| `K_main` | `lightProj * lightView` | FP `env[100..103]` | Main FP sampler: `worldPos → light NDC` | Per frame |
| `K_caster` | `K_main * view_inv` | (computed in CPU, not pushed alone) | Multiplied by engine modelview per draw | |
| `lightMVP` | `K_caster * engine_modelview = lightProj * lightView * model` | VP `env[100..103]` | Caster VP: `vertex.position (model space) → light clip` | Per draw |

### Why two

- **Main FP sampler** transforms world-space `P5` (`vp_*` already supplies world pos in `texcoord[5]`). So it needs `worldPos → light clip` = `lightProj * lightView`.
- **Caster VP** transforms `vertex.position` (model space). The caller's engine modelview is `view * model`. We don't have `model` separately. Composition:
  ```
  lightMVP_caster = lightProj * lightView * model
                  = lightProj * lightView * view_inv * (view * model)
                  = (lightProj * lightView * view_inv) * engine_modelview
                  = K_caster * engine_modelview
  ```
- The `engine_modelview` per draw is read via `glGetFloatv(GL_MODELVIEW_MATRIX)` from inside the draw hook (the engine has the right matrix bound at that moment).

### Bug that this design replaces

Originally `K_main = K_caster` was pushed to the FP. Applying `(lightProj * lightView * view_inv) * worldPos` gives `lightProj * lightView * (view_inv * worldPos)` which is junk — `view_inv * worldPos` only has a meaning when `worldPos` is actually a view-space vector. Symptom: false shadows showing as rectangular wall patches covering large floor regions.

### Bug fixed 2026-05-30 — `view_inv` timing skew (the big "live shadows jump on camera rotation")

`K_caster` was precomputed **once per frame** in `BuildLightK` as `K_main * view_inv`, using the `view_inv` read at frame start (the `glClear` hook). Live casters (placeables, characters, droids) then did `lightMVP = K_caster * mv` with `mv` read at **draw time**. So the full product was:

```
lightMVP = K_main * view_inv(frameStart) * mv(drawTime)
```

When the camera rotates between the clear and the caster draws, `view_inv(frameStart) * mv(drawTime)` no longer cancels the camera — it leaves a residual camera-delta rotation, so the caster's depth lands on the **wrong texel**. The receiver (model FP) places its fragment at the *correct* `K_main * P5_world`, so caster-skewed vs receiver-correct = self-shadow crept onto lit areas and cast shadows jumped, settling only when the camera stopped.

**Why the cached static geometry was immune** (and thus rock-stable): the geometry cache replays `K_main * frozenModel` — pure `K_main` times a frozen world model, **no per-frame `view_inv` multiply**. That asymmetry is exactly why "the location was perfect but everything live jittered."

**Fix:** live casters now read `view_inv` **at draw time** (same instant as `mv`), compute `model = view_inv * mv`, then `lightMVP = K_main * model` — identical math to the cache path, and it matches the receiver's `K_main * P5_world` to the texel. `s_lightK` (the precomputed `K_main * view_inv`) was deleted; `PushLightMVPForDraw` builds from `s_KmainForCache`. Confirmed in-game: live self/cast shadows no longer jump on camera rotation.

Lesson: never mix matrices captured at different moments of the frame. If two matrices must cancel a transform (here, the camera), read them at the same instant.

## 3. Reading the engine view-inverse

K2 stores its camera-to-world basis in `program.env[90..92]` of the VP target — same slots that hold `camera.x/y/z` in the `.w` lanes (already noted in `ENGINE_LESSONS.md` §2.3, but we now use the full 4 floats, not just `.w`).

```cpp
GLfloat r92[4], r91[4], r90[4];
glGetProgramEnvParameterfvARB(GL_VERTEX_PROGRAM_ARB, 92, r92);  // row 0
glGetProgramEnvParameterfvARB(GL_VERTEX_PROGRAM_ARB, 91, r91);  // row 1
glGetProgramEnvParameterfvARB(GL_VERTEX_PROGRAM_ARB, 90, r90);  // row 2
// row 3 = (0, 0, 0, 1)
```

`view_inv` is camera-to-world. Camera position is column 3 (the `.w` values). Confirms the existing `camera.x = env[92].w` finding.

## 4. World coordinate convention

KOTOR 2 world is **Z-up**. Camera Z stays constant as the player walks on the floor. Established by reading `viewInv.m[11]` (camera.z) across many frames and observing it remain near a per-scene constant (~25 for outdoor Telos, ~1 for cantina interior).

Implications:
- `lookAt` for the light view uses `up = (0, 0, 1)`, not `(0, 1, 0)`.
- The sun's "down" component is the **Z** component of `env[27]`, not Y.

## 5. Light frustum

Orthographic, follows camera position:

| Parameter | Value | Reason |
|---|---|---|
| Lateral extent | ±150 world units | Fits typical KOTOR2 outdoor scene around camera. Smaller (±80) left distant geometry outside frustum → no shadows there |
| Far plane | 300 world units | Light eye sits 100 units back along sun ray; 300 gives plenty of depth budget |
| Near plane | 0 | No reason to clip near; geometry behind light eye doesn't exist after `lookAt` flip |
| Eye | `camPos + sun * 100` | At the sun source. `sun` is direction-TO-source (matches FP `N·sun` convention); minus sign would put the eye on the wrong side → no occluders between light and scene |
| Target | `camPos` (snapped, see §6) | Light looks at where the camera looks |
| Up | `(0, 0, 1)` (world Z) | §4 |

Texel size: `(2 * 150) / 1024 = 0.293` world units. Map is 1024² (2048² doubled fragment-fill cost on the Aspyr path without clear quality gain at this extent).

**Eye-sign gotcha:** the FP lights a surface when `N·sun > 0`, i.e. `sun` points *toward* the source. The light camera must therefore sit at `camPos + sun*dist`. An earlier `camPos - sun*dist` put the light under the floor for an overhead sun, so nothing occluded the rays and shadows never appeared.

Tradeoff: ortho extent vs shadow resolution. ±150 with 1024² map = 0.293 ed/texel. Bigger map sharpens but costs fill (the caster rasterizes 800–1200 draws/frame).

## 6. Anchor + global-grid snap (anti-swim)

Two swim sources, two fixes.

### 6a. Anchor at the look-at point, not the camera

K2's camera **orbits the player** when you rotate the view, so `camPos` traces an arc. Centering the light frustum on `camPos` makes the whole frustum swing under pure rotation → shadows swim. Instead center on the camera's look-at point (≈ the player), which stays put while orbiting:

```cpp
// camForward (world) = -(viewInv column 2) = -(m[2], m[6], m[10])
float fwd[3] = { -viewInv.m[2], -viewInv.m[6], -viewInv.m[10] };  V3Norm(fwd);
float anchor[3] = { camPos + fwd * kOrbitR };   // kOrbitR ≈ camera-player dist
```

With `kOrbitR` = the orbit radius, `camPos + fwd*R` lands exactly on the player regardless of orbit angle, because `camPos = player − fwd*R`. An approximate `R` only shifts the frustum center, which the ±150 extent absorbs.

### 6b. Snap the anchor to a coarse world grid

Per-texel snap still leaves a 1-texel step every time the anchor moves a texel — visible as low-level shimmer while walking. Snap the anchor to a **coarse grid that is a whole number of texels** instead:

```cpp
const float kSnap = texel * kSnapTexels;   // e.g. 32 texels ≈ 9.4 world units
// project anchor onto light r/u (world-fixed axes from constant sun + up)
camR = floorf(dot(r,anchor) / kSnap + 0.5f) * kSnap;
camU = floorf(dot(u,anchor) / kSnap + 0.5f) * kSnap;
camF = dot(f, anchor);                      // depth axis: no snap needed
camPos = camR*r + camU*u + camF*f;
```

- **Texel-aligned** (`kSnap` is an integer × texel) → no sub-texel shimmer.
- **World-fixed cell**: inside one `kSnap`-sized cell the light frame is byte-identical frame to frame → zero swim under both rotation and translation.
- **One discrete jump** when the anchor crosses a cell boundary (~every `kSnap` units walked). Bigger `kSnapTexels` = rarer but larger jumps.

Because `r`/`u` come from the constant sun direction, the grid is genuinely in world space — this *is* "anchor to global coordinates", quantized so the frustum still travels with the player over long distances.

## 7. Caster pass + double draw

Caster pass = a parallel "render the world from the sun's POV into the shadow FBO". K2 doesn't know about it. We piggy-back on every opaque world draw by binding the shadow FBO + caster VP, calling `real_draw(...)`, restoring state, then calling `real_draw(...)` again normally.

State save/restore is precise:

```cpp
// BeforeDraw
glGetIntegerv(GL_VIEWPORT, savedVp);
glGetBooleanv(GL_COLOR_WRITEMASK, savedColorMask);
glGetIntegerv(GL_VERTEX_PROGRAM_BINDING_ARB, &savedVpId);
savedVpEn = glIsEnabled(GL_VERTEX_PROGRAM_ARB);
savedFpEn = glIsEnabled(GL_FRAGMENT_PROGRAM_ARB);

bind FBO + viewport(0,0,2048,2048) + colormask FALSE
bind shadow caster VP + glEnable(VP) + glDisable(FP)
push lightMVP

// AfterDraw — exact reverse, including restoring savedVpId=0 and enabled state
```

The "restore enabled state precisely" matters because K2 sometimes draws with **no VP bound** (fixed function). If we only restore `glBindProgram` but not the enabled flag, our shadow VP stays bound and the next draw renders garbage.

## 8. Caster pass empty frames

Sometimes the diagnostic logs `tex center: min=max=avg=1.0` for a frame — meaning nothing was drawn into the shadow FBO. Investigated; these are legitimate cutscene / dialogue frames where K2 issues only menu / particle draws (no `vp_static_*` or `vp_skinned_*`). The world FP doesn't sample shadows in those moments either, so no visible bug — just diagnostic noise.

## 9. Caster filters

`BeforeDraw` rejects a draw (and the caller does the engine draw only) when:

1. **Sub-window viewport** (`w < 1280 || h < 720`) — K2's inventory previews, character close-ups, HUD compositing. Rendering them into the shadow tex stomps useful content. Same threshold the depth-capture system uses.
2. **Non-world FP** — only FPs whose name starts with `fp_worldtex` or `fp_model` pass. Post-process (`fp_post_*`), UI (`fp_main_menu`), and no-FP (fixed-function) draws are full-screen / near-plane quads whose depth ≈ 0 would poison the whole shadow tex (every later compare fails → entire scene reads shadowed). This was the bug behind "shadows vanished / wall-shaped false shadows".
3. **No caster VP compiled** — nothing useful to render.

## 9b. Alpha-tested (punchthrough) casters (2026-05-30)

Grates, mesh, and foliage (leaves) are alpha-masked cutouts. The depth-only caster
renders their quads solid → solid rectangular shadows instead of leaf/grate shapes.
Fix:

- Both caster VPs (`kShadowVpSource`, `kShadowVpSkinnedSource`) now pass the diffuse
  UV through to `result.texcoord[0]`.
- A new alpha-test caster FP (`kShadowFpAlphaSource`) samples the diffuse at
  `texture[0]` (the one the engine bound at TMU 0 for this draw) and `KIL`s fragments
  with `alpha < 0.5`, so transparent texels write no depth.
- `BeforeDraw` binds that FP **only when the engine has `GL_ALPHA_TEST` enabled** for
  the draw — the engine's own punchthrough signal. Opaque draws keep the FP disabled
  (cheap depth-only). Critically this avoids punching **reflective env materials**,
  where a low diffuse alpha means *mirror*, not cutout (see `SHADER_REFERENCE.md`
  "Alpha semantics differ by shader family").
- Punchthrough draws are **excluded from the geometry cache** (`TryCaptureCaster`
  returns false when `GL_ALPHA_TEST` is on), because the cache replay is depth-only;
  they take the live path so the alpha-test FP applies. Minor per-frame cost on
  foliage-heavy scenes, but correct cutout shadows.

Threshold is a fixed 0.5; if a material's cutout ref differs the shadow edge may be
slightly thicker/thinner than the lit surface — refine by reading `GL_ALPHA_TEST_REF`
if needed.

**Cached punchthrough (fixed 2026-05-30).** First cut *excluded* `GL_ALPHA_TEST` draws
from the geometry cache and forced them live. But on KOTOR the engine keeps
`GL_ALPHA_TEST` on for **most** world geometry (walls/rocks too, not just cutouts),
so this dumped nearly the whole level onto the live double-draw path → world shadows
**vanished in tactical pause** (engine stops re-submitting world draws while paused →
nothing live-casts) and FPS cratered (155–417 live caster draws/frame). Fix: cache
everything again, and the **cache replay itself alpha-tests** — each `CachedDraw`
stores `alphaTest` + the diffuse tex id + its texcoord0 array; the replay binds the
alpha-test FP + diffuse + UVs for alpha entries (solid surfaces have alpha=1, so KIL
is a no-op). The replay save/restores the texcoord-array + TMU0 + FP-binding state it
touches. Result: foliage keeps cutout shadows, the static map persists through pause,
and the per-frame cost returns to the cached (stand-still) baseline.

## 10. Shadow tex format

```
GL_DEPTH_COMPONENT24, 1024×1024
GL_TEXTURE_WRAP_S/T = GL_CLAMP_TO_EDGE
GL_TEXTURE_MIN/MAG_FILTER = GL_LINEAR  ← bilerp raw depth for soft PCF
GL_DEPTH_TEXTURE_MODE = GL_LUMINANCE   ← critical, see below
GL_TEXTURE_COMPARE_MODE = GL_NONE      ← critical
```

`GL_DEPTH_TEXTURE_MODE = GL_LUMINANCE` makes `TEX` ops return the raw depth as `.r`. Without it the depth comes through as `.a` and the rest are zeros — silently breaks all sample math that reads `.x`.

`GL_TEXTURE_COMPARE_MODE = GL_NONE` disables hardware shadow compare (`sampler2DShadow`). ARB FP 1.0 doesn't have a clean `sampler2DShadow` binding from `texture[N]` so we compare in shader manually.

`GL_LINEAR` (no compare mode) bilerps the raw depth values; combined with the 2-tap PCF the shadow edge is soft and the texel-snap step is hidden under camera motion.

1024² balances fill cost vs detail: with 800–1200 caster draws/frame, 2048² doubled the depth-only rasterization cost without a clear quality gain at the ±150 ortho extent.

## 11. Shadow tex bind point

TMU 6 (constant `SHADOW_TMU`). Picked because:
- TMU 0/1/2/3 are used by various FP variants (diffuse / lightmap / cube reflect / detail).
- TMU 4/5 are used by some bumpmap-env FPs.
- TMU 7 is the depth-capture system's slot.
- TMU 8/9/10 are PBR siblings (n/R/M).
- TMU 11+ is the Aspyr driver rejection wall (silent fail on `glActiveTexture(GL_TEXTURE0+11)`).

→ 6 is the only unused slot that's also legally reachable.

Bind happens at `EndCasterPass` (just before the main world draws), not per-draw. K2's world-FP path doesn't touch TMU 6 between caster pass end and the first sample.

## 12. Main FP sampler block

Present in `fp_worldtex_env_fog`, `fp_worldtex_lm_fog_alpha`, and `fp_model_env_fog`. Same block copy-pasted (ARB FP 1.0 has no includes). Pattern:

```
ATTRIB P5 = fragment.texcoord[5];          # vp pushes wPos here
PARAM tnF = program.env[29];               # x=strength, y=viz, z=floor, w=bias
PARAM lightK[4] = { program.env[100..103] };
PARAM pcfStep = { 0.000977, 0.000977, 0.0, 0.0 };  # 1/1024

# Project worldPos to light clip → NDC → UV
MOV shClip.xyz, P5;
MOV shClip.w, 1.0;
DP4 tmp.x, lightK[0], shClip;
DP4 tmp.y, lightK[1], shClip;
DP4 tmp.z, lightK[2], shClip;
DP4 tmp.w, lightK[3], shClip;
RCP shZ.w, tmp.w;
MUL tmp.xyz, tmp, shZ.w;
MAD shUV.xy, tmp, 0.5, 0.5;
MAD shZ.x, tmp.z, 0.5, 0.5;
MIN shZ.x, shZ.x, 0.99;        # far-plane fragments → lit
MAX shZ.x, shZ.x, 0.0;

# UV-bounds check (chain of SGEs) → tmp.x = 1 if shUV ∈ [0,1]², else 0

# 2-tap PCF: two diagonal samples, accumulate compares
SUB tmp.z, shZ.x, tnF.w;       # frag depth - bias
TEX shDepth, shUV±pcfStep, texture[6], 2D;  ×2
SGE accum, shDepth.x, tmp.z;   ×2
MUL shadowed.x, accum, 0.5;

# Out-of-bounds → force lit (overrides PCF result)
LRP shadowed.x, tmp.x, shadowed.x, 1.0;

# Floor + strength
SUB tmp.x, 1.0, tnF.z;
MAD shadowed.x, shadowed.x, tmp.x, tnF.z;   # 0 → floor, 1 → 1
LRP shadowed.x, tnF.x, shadowed.x, 1.0;     # strength

# Apply to sun terms only (NOT global d.rgb)
MUL NdotH.x, NdotH.x, shadowed.x;   # baked sun spec
MAD r.rgb, F, NdotH.x, r;
...
MUL tmp.x, tmp.x, shadowed.x;       # sun diffuse
MAD r.rgb, spec, tmp.x, r;
```

### Sun-only application

The shadow factor multiplies into the sun's specular and diffuse contributions **before** they accumulate into `r`. Not the final `d.rgb`. Reasons:

- L0 (the engine's positional light at env[86]/[87]) isn't directional, doesn't share the shadow map.
- Ambient (lightmap, cube reflection, vertex color) shouldn't be killed by sun occlusion — surfaces in shadow still receive sky / bounce light.
- Back-facing surfaces have `N·L ≤ 0` so the sun terms are zero anyway. Multiplying zero by the shadow factor is a no-op — they neither falsely darken nor falsely brighten on the back side.

### CLAMP_TO_EDGE leak

The UV bounds check exists because `GL_CLAMP_TO_EDGE` returns the boundary texel for any out-of-`[0,1]` sample. If the boundary happens to have geometry rasterized (depth < 1), the leaked value falsely shadows huge regions of the screen as rectangular "wall" shapes that don't correspond to real scene geometry.

The check is an SGE chain that produces 1 if `shUV ∈ [0,1]²`, 0 otherwise. When 0, the final `LRP` forces `shadowed.x = 1` (lit).

## 13. 2-tap PCF

Two diagonal samples at `shUV ± (1/1024)`. The compare runs per sample; results average via `MUL shadowed, accum, 0.5`. Output values: `{0, 0.5, 1.0}`. Soft ~1-texel edge at half the texture-fetch cost of the 4-tap version (dropped for perf — the ARB FP path is fill-bound on the Aspyr driver).

PCF also masks the residual texel-snap step: when the snap boundary crosses, the average shifts gradually instead of stepping a full unit. Combined with the `GL_LINEAR` depth filter (§10) the walking shadow looks fluid rather than ratcheted.

## 14. Tuning parameters (env[29])

```
env[29].x = strength    0..1     0 disables, 1 applies floor↔1 lerp at full effect
env[29].y = viz         0..1     ≥0.99 → grayscale debug override on d.rgb
env[29].z = floor       0..1     shadow darkness (0=black, 1=invisible)
env[29].w = bias        0..0.05  added to shDepth before compare; tunes acne
```

Defaults (see `g_params` in `pbr_tune.cpp`): strength=0.7, floor=0.35, bias=0.005, viz=0.

Visible-shadow quick check: strength=1, floor=0.3, viz=0. Walk past a wall — expect a clearly darker band on the floor on the lee side.

## 15. Why this works where SS-shadow didn't

The screen-space attempt (cut in 0.3) relied on the captured scene depth tex, which:
- Suffers from K2's hyperbolic z (`1/(1-z)` blows up at the far end).
- Has the `glCopyTexSubImage2D` quirk forcing full-Copy each frame.
- Needs a throttle, which ghosts dynamic geometry.

The shadow map captures depth in **light's** linear ortho space, not eye-space hyperbolic depth. No `1/(1-z)`. No need for `glCopyTex*` — we render into a depth FBO via the engine's existing rasterizer. Cost is `~draw_count × depth-only-rasterize` per frame, which the GPU handles fine.

## 16. Known limitations / next steps

- **Sampler now in all major world + model FPs** (updated 2026-05). World FPs (`fp_worldtex_env_fog`, `fp_worldtex_lm_fog_alpha`, `fp_worldtex_lm_env`, `fp_worldtex_bump_env`, `fp_worldtex_bump_env_gamma`) sample **TMU 6** with **last frame's** `K_main` (`env[100..103]`) — the complete ping-pong map. Model/character FPs (`fp_model_env_fog`, `fp_model_bump_env_spec`/`_b`, `fp_model_armor_legacy`, `fp_model_headgear_legacy`) sample **TMU 7** with **this frame's** `K_main` (`env[104..107]`) — the in-progress map that already holds the object's own just-rendered depth, so dynamic self-shadows are current rather than 1 frame late. Earlier note "sampler in 3 FPs" is obsolete.
- **Horizontal sun = weak floor shadows.** With a near-horizontal sun (`sun.z ≈ 0`), the floor's `N·sun` is tiny so it gets almost no sun term; gating ~0 by the shadow factor is invisible. Shadows read strongly only where `N·sun` is large (walls facing the sun, ground under a high sun). Tune `Sun dir Z` up for floor shadows.
- **Cell-cross pop.** The coarse global-grid snap (§6b) trades per-frame shimmer for one discrete jump each `kSnapTexels*texel` (~9.4u) of travel. Bigger cells = rarer but larger pops. A cross-fade between two grid positions would hide it but doubles the caster pass.
- **No cascades.** Single ±150 ortho. Detail is uniformly low because the texel is set by the wide extent. CSM would help but multiplies caster cost.
- **L0 not shadowed.** Engine's positional light doesn't go through the directional map.
- **Caster cost.** Double-draw adds ~all opaque world+model draws per frame (800–1200 on busy scenes). 1024² + 2-tap PCF keep it tolerable but it's the dominant new cost. Skinning every animated mesh into the tex each frame is part of this.
