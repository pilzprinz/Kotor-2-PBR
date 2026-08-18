# Architecture

How the DLL works.

## DLL acts as OpenGL proxy

`opengl32.dll` (our build) ships in game folder. Windows DLL search order picks it before system `C:\Windows\System32\opengl32.dll`.

```
swkotor2.exe
    |
    | imports opengl32.dll exports (glAccum, glBindTexture, etc.)
    v
opengl32.dll (ours)
    |
    | forwards most calls via stdcall trampolines
    | intercepts: glProgramStringARB, glBindProgramARB, glFog*
    v
C:\Windows\System32\opengl32.dll (loaded by us via full path)
```

Each export in our DLL is a naked function that just `jmp`s to the real address looked up at load time. Intercepted functions run our logic before/instead of calling the original.

## Components

### `opengl32.cpp`

- `DllMain` — init on process attach
- `InitGL` — load system `opengl32.dll` via full path, populate `p[368]` jump table
- 368 stdcall trampolines `__E__N__` — each `jmp p[N*4]`
- `MyGetProcAddress` (via wglGetProcAddress hook in `glFunctions.cpp`) — returns our wrappers for ARB extension functions

### `glFunctions.cpp` (inherited from ShaderOverride)

- `my_glProgramString` — intercepts ARB shader uploads, MD5 hashes them, dumps to `shaders_original/`, substitutes from `shaders_override/` if present
- `my_glBindProgram` — sets `program.env[8]` (fog), `program.env[10]` (viewport size)
- Fog state tracking via `glFog*` hooks
- `shader_ident.txt` parser for hash → friendly name mapping

### `iat_hook.cpp` (new — PBR)

Generic Import Address Table patcher. Used to redirect `kernel32!CreateFileA` and `CreateFileW` to our hooks.

Walks the PE import descriptor table of the target module (swkotor2.exe), finds the entry matching `(dll_name, function_name)`, swaps its function pointer to our replacement.

### `file_logger.cpp` (new — step A1)

Hooks `CreateFileA`/`CreateFileW` in swkotor2.exe IAT. Logs every file open attempt to `pbr_file_log.txt`:
```
A|OK|.\override\foo.tga         <- ANSI CreateFile, succeeded
W|FAIL|.\override\bar.tga       <- Wide CreateFile, failed
```

Used to discover which textures the engine loads via direct file access (vs internal BIF/ERF reads).

### `md5.cpp` (inherited)

MD5 hashing of ARB shader source for identification.

### `depth_capture.cpp` (new — depth & AO subsystem)

Snapshots the default-framebuffer depth attachment into a private `GL_DEPTH_COMPONENT24` 2D texture bound to TMU 7 (constant `DEPTH_CAPTURE_TMU`), so fragment programs can sample world depth at any screen-space UV.

Also IAT-hooks `gdi32!SwapBuffers` and `opengl32!wglSwapBuffers` (K2 calls the gdi32 variant in practice) and exposes ARB/EXT framebuffer-object wrappers via `DepthCapture_InterceptProc` (vestigial — K2 creates 9 color-only FBOs at startup but never uses them for the scene depth pass).

Snapshot timing strategy + format details: see `DEPTH_CAPTURE.md`.

### `gl_state_capture.cpp` (new — fixed-function state mirror)

IAT-hooks GL fixed-function entry points (`glLoad/Mult/Push/PopMatrix`, `glMatrixMode`, `glLightfv`, `glEnable/Disable`, `glDepthMask`, `glClear`) so the proxy DLL mirrors the engine's matrix/light state and can drive the depth snapshot from in-frame phase signals (`glDepthMask(FALSE)` and `glClear(GL_DEPTH_BUFFER_BIT)`).

Empirically K2/Aspyr imports only 9/14 of these — `glLoadMatrixf`, `glLoadMatrixd`, `glMultMatrixd`, `glLightf`, `glLightiv` are not in the exe's IAT. The engine pushes matrices and light data directly to ARB program env params instead of using fixed-function state. The matrix-mirror code remains as scaffold; **`state.matrix.*` and `state.light[N].*` ARB built-ins are unreliable on this engine** — read `program.env[N]` slots instead.

`my_glPushMatrix` does an additional job: at the **first non-identity push in MODELVIEW mode per frame**, it calls `glGetFloatv(GL_MODELVIEW_MATRIX)` to read the **real** GL state (not our dormant mirror) and exposes the captured matrix via `GlState_FrameViewMatrix()`. That snapshot is what the F2/Backspace sun-capture button reads to derive the camera forward vector. The fix sequence was:

1. Try the in-DLL mirror — returns identity (K2 never calls `glLoadMatrixf`).
2. Try `glGetFloatv` at swap time — returns identity (engine has already reset modelview for HUD).
3. **Try `glGetFloatv` inside our push hook, gated to first non-identity 3×3 rotation** — works.

The `IsRotIdentity` helper checks only the 3×3 rotation block (ignoring translation) because HUD draws push valid-translation, identity-rotation matrices that should be skipped.

### `pbr_tune.cpp` (new — in-game tuning overlay)

DEL toggles a slider panel rendered as quads + texture-atlas glyphs over the K2 backbuffer. Each row pushes its value to a `program.env[24..29]` slot consumed by the PBR fragment programs.

Key implementation notes:

- **No `wglUseFontBitmapsA`** — Aspyr wrapper silently no-ops it. Font built as GDI `CreateFontA` → `CreateCompatibleBitmap` → `BitBlt` into DIB → `glTexImage2D` atlas (16 cols × 6 rows, ASCII 32..127).
- **Input via `GetAsyncKeyState`** — main-loop polling, not WM_KEYDOWN, so we don't need a window-proc hook. Mouse buttons via `GetCursorPos` + `GetAsyncKeyState(VK_LBUTTON)`.
- **Env push gated by dirty flag + `glClear` re-arming**: K2 stomps env slots between FP binds, so we must re-push every frame. Setting `g_tunePushNeeded = true` from the `glClear` hook produces a guaranteed re-push window each frame without scanning state.
- **INI persist**: `pbr_tune.ini` lives next to `opengl32.dll`. Loaded at init, saved on overlay close. Each row has `iniKey` so renaming labels doesn't lose user values.
- **F2/Backspace sun capture**: reads `GlState_FrameViewMatrix()`, takes the forward vector (negated 3rd row), pushes into env[27].xyz. Captured matrix is logged with all 4 sources for diagnostic — see `logs/pbr_tune_diag.log`.
- **`DiagLog()` stdio fallback log**: writes to `<dll-dir>/logs/pbr_tune_diag.log` via plain `fopen/fprintf`. Survives even if `file_logger`'s `CreateFileA` path is broken. Exposed in `pbr_tune.h` for other modules to use.

### `shadow_map.cpp` (new — directional sun shadow map)

Real shadow-map subsystem replacing the cut SS-shadow attempt. Renders the world from the sun's POV into a 2048² `GL_DEPTH_COMPONENT24` FBO, then samples the resulting depth tex from main FPs to gate the directional-sun contribution.

Hooked into the render pipeline at three call sites:
- `glClear(DEPTH_BUFFER_BIT)` hook → `ShadowMap_BeginCasterPass()`: clear shadow FBO, rebuild `K_main`/`K_caster` from current view-inverse (env[90..92]) + sun (env[27]).
- Every `my_glDrawElements / Arrays / glBegin` in `pbr_tune.cpp` → `ShadowMap_BeforeDraw()` + real draw + `ShadowMap_AfterDraw()` + the engine's intended draw. Double-draw pattern: same vertices rendered twice, once into shadow FBO via the shadow caster VP, once into the main framebuffer with K2's VP.
- `SwapBuffers` hook → `ShadowMap_EndCasterPass()`: bind shadow tex to TMU 6 for the next frame's main FP samples.

Public API (`shadow_map.h`): `Init`, `IsAvailable`, `BeginCasterPass`, `EndCasterPass`, `BeforeDraw`, `AfterDraw`, `OnDraw` (diagnostics), `GetShadowVpId`.

Internal CPU matrix math (`M4Identity/Mul/Ortho/LookAt`) composes:
```
K_main   = lightProj * lightView        → FP env[100..103] (worldPos sampler)
K_caster = K_main * view_inv             → multiplied by glGetFloatv(MODELVIEW_MATRIX) per draw → VP env[100..103] (model-space caster)
```

State save/restore in `BeforeDraw/AfterDraw` is per-draw and precise (viewport, colormask, VP id, VP/FP enabled flags). K2 changes viewport mid-pass and sometimes draws with no VP bound; cached cross-draw state would corrupt.

Light frustum: orthographic ±150 lateral, far=300. Camera-following with light-axis texel-grid snap (project camPos onto light `r/u` axes, snap, reconstruct) to suppress swimming shadows.

Design + tradeoffs: `SHADOW_MAP.md`.

### `platform.h`

Windows + OpenGL types stub (was missing in upstream repo).

### `opengl32.def`

Module definition file listing all 368 exports with stdcall name and ordinal. Used by linker to produce correctly-named exports matching system `opengl32.dll`.

## Load sequence

1. Windows loader starts `swkotor2.exe`
2. Resolves imports — finds `opengl32.dll` in game folder, loads ours
3. Our `DllMain(DLL_PROCESS_ATTACH)` runs:
   - `InitGL()` — load real `opengl32.dll` from System32, populate jump table
   - `InitShaderLookup()` — read `shader_ident.txt` into hash→name map
   - `InitFileLogger()` — open `pbr_file_log.txt`, install IAT hooks for `CreateFileA`/`W`
   - `PbrHooksInit()` — install texture hooks
   - `DepthCapture_InstallSwapHook()` — IAT hooks for `gdi32!SwapBuffers` and `opengl32!wglSwapBuffers`
   - `GlStateCapture_Init()` — IAT hooks for matrix / light / depth state
4. Game runs, calls OpenGL functions — most pass through trampolines, intercepted ones go through our wrappers
5. First `SwapBuffers` after GL context is alive — `OnFirstSwap()` latches and calls `ShadowMap_Init()` (FBO + depth tex creation, shadow caster VP compile). Can't run at `DllMain` time because no GL context exists yet.
7. Game exits — `DllMain(DLL_PROCESS_DETACH)`:
   - `ShutdownFileLogger()` — close log file

## Why IAT hook for CreateFileA?

Cleaner than inline detours:
- Doesn't modify code at runtime
- Only affects target module (swkotor2.exe imports)
- System DLLs unaffected
- Reversible (we could swap back, though we don't bother)

Limitation: only catches direct kernel32 imports. If engine dynamically resolves `CreateFileA` via `GetProcAddress`, hook misses. Mitigation could be a `GetProcAddress` hook returning our function. Not needed yet — engine uses static imports.

## What's planned next

### Step A2 — texture name ↔ GL id mapping

- TLS variable `g_currentLoadingTexture` — last filename opened
- Hook `glTexImage2D` — when called, record `texIdToName[currentBoundTexture] = g_currentLoadingTexture`

### Step A3 — sibling texture loader

- Lazy cache: `name → SiblingSet { roughnessId, metallicId, aoId, normalId, pbrParams }`
- On first `glBindTexture(diffId)` with known name, look up siblings in Override
- TGA decoder (minimal, uncompressed only initially)
- TPC decoder (BioWare format)
- `.pbr` INI parser

### Step A4 — env param injection

- In `my_glBindProgram`, after standard fog/viewport setup, push PBR params (`metallic`, `roughness`, `useNormalMap` flags) to `program.env[20..25]`

### Step A5 — custom PBR fragment programs

- Replacement ARB fp for `fp_model_env_fog`, `fp_worldtex_lm_fog_alpha`, `fp_worldtex_lm_env_fog`
- Sample diffuse + normal + roughness + metallic from TMUs
- Cook-Torrance approximation in ARB1.0 (instruction budget ~50)
- Output composited PBR result to `result.color`

## Compatibility constraints

### ARB FP 1.0 minimum guarantees (driver enforced)

| Resource | Min | Notes |
|---|---|---|
| ALU instructions | 72 | `MUL`, `MAD`, `LRP`, etc. |
| Texture instructions | 24 | `TEX`, `TXB`, `TXP` |
| Total | 96 | Combined |
| Temporaries | 32 | Some old drivers 16 |
| Program parameters | 24 | Includes inline literals |
| Texture indirections | 4 | Dependent texture chain depth |

**Failure mode:** Aspyr wrapper does not log compile errors. Exceeding limits → `glProgramString` sets `GL_INVALID_OPERATION`, engine binds non-functional program → silent black output. Suspect budget overflow when:
- Symptom: entire shader class (e.g. all reflective walls) renders black after edits
- Quick check: `grep -c -E "^[A-Z]{3,4}[[:space:]]" deploy/shaders_override/fp_*.txt` — stay < 72 for safety
- Bisect: strip features one-by-one (extra lights, detail normal, tonemap) until rendering returns

### Disallowed FP opcodes

`DST` is ARB_vertex_program **only**. Common gotcha when porting attenuation formulas from VP. Replace with explicit MAD chain.

### Naked-trampoline conversion is dangerous

`opengl32.cpp` exports 368 stdcall trampolines as naked `__E__N__` functions that just `jmp p[N*4]`. Two of them — `wglGetProcAddress` (`__E__355__`) and `wglSwapBuffers` (`__E__361__`) — are useful intercept points.

**Only `__E__355__` (wglGetProcAddress) was successfully converted** to a non-naked C wrapper. Attempts to convert `__E__72__` (glDrawArrays), `__E__74__` (glDrawElements), `__E__361__` (wglSwapBuffers) all crashed the game on launch (single-flash + no main window). Cause not fully diagnosed — likely mingw stdcall-fixup misbehaviour around the high-frequency hot path, but worth treating as a hard rule:

> **Don't convert naked exports.** Use IAT hooks on `swkotor2.exe`'s imports for the same effect — same observability, no risk to the export table.

### wglGetProcAddress recursion trap

Resolving an ARB/EXT function name with `GetProcAddress(GetModuleHandleA("opengl32.dll"), "wglGetProcAddress")` returns **our own `__E__355__` wrapper**, not the system one. If our wrapper then dispatches the same name back to itself, you get unbounded recursion → stack overflow → instant crash at load.

Fix: always resolve through `orig_wglGetProcAddress` (declared in `glFunctions.h`, populated by `InitGL` from the system `opengl32.dll` import).

### K2 imports SwapBuffers from gdi32.dll, not opengl32.dll

Standard for older GL apps. Hook `gdi32!SwapBuffers` via IAT (primary) and `opengl32!wglSwapBuffers` (fallback). The latter typically resolves to a `NULL` IAT entry in K2 — game never calls it.

### Aspyr K2 bypasses GL fixed-function state

The exe imports `glMatrixMode`, `glLoadIdentity`, `glPushMatrix`, `glPopMatrix`, `glMultMatrixf`, `glEnable`, `glDisable`, `glDepthMask`, `glClear`, `glLightfv` — but **not** `glLoadMatrixf`, `glLoadMatrixd`, `glMultMatrixd`, `glLightf`, `glLightiv`.

Combined with the lack of `state.matrix.modelview.row[N]` correctness, the inference is that Aspyr pushes transforms directly to ARB program env params. The fixed-function matrix stack is initialised but never populated with meaningful data.

Implications for shaders:
- `state.matrix.*` returns identity / zero in FPs — **don't use it**. Read `program.env[N]` slots set by VPs or by `my_glBindProgram`.
- `state.light[N].*` similarly inert — light data lives in `program.env[86]/[87]` and friends.

### K2 doesn't use FBOs for the scene depth pass

K2 creates 9 color-only `glGenFramebuffersEXT` objects at startup (texture ids 29–40 attached) and never binds any during a frame. The scene draws to the WGL default backbuffer; depth lives in the default framebuffer's implicit attachment.

Implications:
- Depth capture via FBO blit (RenderDoc-style) won't work — use `glCopyTexImage2D` from the default framebuffer instead.
- ReShade's "Generic Depth" handle (`0x8218...`) is its own internal abstraction, not a GL FBO.

### TMU layout (this mod)

| TMU | Use |
|---|---|
| 0 | Diffuse (stock) |
| 1 | Cube envmap (obj) / lightmap (walls/world) |
| 2 | Cube envmap (walls/world) |
| 6 | Shadow map (sun depth from light POV, 2048² `GL_DEPTH_COMPONENT24`) |
| 7 | Depth buffer (contact shadow input — vestigial; depth-capture disabled) |
| 8 | Normal map sibling (`_n.tga`) — `.w` = AO |
| 9 | Roughness sibling (`_R.tga`) — `.w` = emissive |
| 10 | Metallic sibling (`_M.tga`) |

TMU 11+ rejected by Aspyr wrapper — that's why PBR data packed into TMU 8/9/10 with alpha channel reuse.

### `program.env[]` slot map

| Slot | Use |
|---|---|
| 0 | Output alpha base |
| 8 | Fog state (stock) |
| 10 | Viewport size + reciprocal `(w, h, 1/w, 1/h)` |
| 20 | PBR scalars: `(metallic, roughness, F0, emissive)` |
| 21 | Feature gates: `(useNrm, useRough, useMetal, useAO)` |
| 22 | Strengths: `(nrmStr, useEmi, cavityStr, fresnelRim)` |
| 23 | `(reflectivity, _, _, _)` |
| 24 | Tune A: `(sheenStr, detBlend, detUV, alphaShiftK)` |
| 25 | Tune B: `(perturbMix, lodScale, L0specStr, sunSpecStr)` |
| 26 | Tune C: `(envBoost, L0specExp, sunSpecExp, _)` |
| 27 | Tune D: `(sunDir.xyz, pbrDiffDim)` |
| 28 | Tune E: `(sunDiffIntens, sunR, sunG, sunB)` |
| 29 | Shadow: `(strength, viz, floor, bias)` |
| 73-87 | Stock K2 lights (L0/L1/L2 pos/diff/atten) |
| 86-87 | L0 (primary local light): pos in 87, diffuse in 86 |
| 89-92 | View-inverse (camera-to-world) 4×4: rows 0..2 in env[92]/[91]/[90].xyzw, row 3 implicit `(0,0,0,1)`. `.w` lanes = camera world pos. Row mapping is reverse: env[92]=row 0, env[91]=row 1, env[90]=row 2 |
| 100-103 (VP target) | `lightMVP` for shadow caster: `K_caster * engine_modelview`, pushed per draw |
| 100-103 (FP target) | `K_main = lightProj * lightView`: pushed per frame, used by sampler |

## Testing infra

- `validation_tint*` folders contain per-shader color/pattern markers used to identify shader roles
- See `SHADER_REFERENCE.md` for validated mappings
- See `DEPTH_CAPTURE.md` for the depth/AO subsystem details + empirical findings on K2's render order

## Debugging black-render regressions

Step-by-step when a previously-working shader override starts producing black output:

1. **Verify deployment:** `cp deploy/shaders_override/*.txt "<game>/shaders_override/"` actually wrote files; check timestamps.
2. **Confirm wrapper picked up override:** tail `logs/pbr_file_log.txt` for `SHADER|override|<name>` entries on map load.
3. **Check instruction count:** > 72 ALU likely silent compile fail.
4. **Compare against stock dump:** `cat shaders_original/<name>.txt` — verify your override matches stock attribute layout (especially `ATTRIB` declarations and `texcoord[N]` indices).
5. **Strip and bisect:** revert to minimal stock-equivalent shader (~15 instr). If that renders, add features incrementally; binary-search the breaking change.
6. **VP/FP pairing check:** if override uses `texcoord[4]/[5]` (world normal/pos), the paired VP must also be overridden to write those slots — otherwise garbage data → undefined lighting (often black).
