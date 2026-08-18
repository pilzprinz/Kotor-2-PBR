# Engine lessons — KOTOR 2 / Aspyr OpenGL wrapper

What we learned about K2's renderer, the Aspyr GL wrapper, and the mod's own quirks. Use this as the single rolling reference when starting new work on the codebase.

Cross-refs:
- `ARCHITECTURE.md` — DLL structure, hook strategy
- `DEPTH_CAPTURE.md` — depth subsystem + driver quirks (detailed)
- `SHADER_REFERENCE.md` — per-shader analysis + screen-space shadow autopsy
- `SHADOW_MAP.md` — real shadow-map subsystem (replaces SS-shadow)

---

## 1. The engine

### 1.1 K2 is the Aurora engine ported by Aspyr

The native KOTOR 2 (2004) used a Direct3D 9 renderer through Aurora. Aspyr's 2015 GoG/Steam rerelease ships an **OpenGL renderer** that is, in effect, a D3D9-style command stream translated to GL. The wrapper is Aspyr proprietary code, not a generic translator like DXVK.

Consequences:
- Many GL features are present but behave like the equivalent D3D feature, not "real" GL.
- Driver-level enums some real-GL apps would never set (e.g. `GL_DEPTH_TEXTURE_MODE`) are honored.
- Some entrypoints are accepted at the call site but silently no-op (`wglUseFontBitmapsA`).
- Driver crashes / `GL_INVALID_VALUE` errors hit on combinations a native GL driver would handle (see depth-Sub quirk below).

### 1.2 Render order per frame

```
glClear(DEPTH|COLOR)
  └─ skybox draw                       (depth writes off)
  └─ opaque world (walls / floors)     fp_worldtex_env_fog, fp_worldtex_lm_env_fog, ...
  └─ opaque chars/objects              fp_model_env_fog, ...
  └─ glDepthMask(FALSE)                ← opaque → transparent transition
  └─ transparency (particles, alpha)
  └─ UI overlay
  └─ multiple sub-renders for HUD/inventory previews (small viewports)
SwapBuffers (gdi32!SwapBuffers, NOT wglSwapBuffers)
```

The `glDepthMask(FALSE)` call is a reliable "scene-opaque done" signal. The mid-frame `glBindProgram(fp_model_*)` is the "world walls finished, characters starting" signal.

### 1.3 K2 does NOT use FBOs for the scene depth pass

Scene draws straight to the WGL default backbuffer. Depth is the default framebuffer's implicit attachment. The 9 color-only FBOs K2 creates at startup are scaffolding it never uses.

→ Depth capture must use `glCopyTex*` from the default FB. FBO blit is not an option here.

### 1.4 K2 does NOT use GL fixed-function matrix/light state

K2's exe imports `glMatrixMode`, `glLoadIdentity`, `glPushMatrix`, `glPopMatrix`, `glMultMatrixf`, `glEnable`, `glDisable`, `glDepthMask`, `glClear`, `glLightfv` — but **not** `glLoadMatrixf`, `glLoadMatrixd`, `glMultMatrixd`, `glLightf`, `glLightiv`.

Transforms are loaded directly into ARB program env params. The fixed-function matrix stack is touched (push/pop) but never has meaningful data loaded into it.

Consequences for shaders:
- **`state.matrix.*` returns identity / zero** in fragment programs. Don't use it.
- **`state.light[N].*` is similarly inert.** Light data lives in `program.env[86]/[87]` and friends.
- Reading the real view matrix from inside the proxy DLL **only** works via `glGetFloatv(GL_MODELVIEW_MATRIX)` from inside a hook fired during the world draw — not at swap time (engine has already reset matrices for HUD).

### 1.5 K2 uses `gdi32!SwapBuffers`, not `wglSwapBuffers`

Standard for older GL apps. IAT-hook both for safety; only `gdi32!SwapBuffers` actually fires.

### 1.6 K2 issues many `SwapBuffers` per real frame

Inventory item portraits, equipment previews, NPC dialog cameos: each is a small viewport draw followed by what flows through our swap hook. Observed viewport sizes in one second:

```
1920x1080 / 1920x1079  (main scene + HUD slight-vertical-shrink pass)
189x354    (inventory mini-render)
201x354
225x390
207x372
...
```

Any "do something at swap" code must filter by viewport size — typically reject anything smaller than `(1280, 720)` — or it will fire dozens of times per real frame and trash performance.

### 1.7 TMU range limit

`glActiveTexture(GL_TEXTURE0 + N)` is silently rejected for `N >= 11` on this driver. PBR siblings are forced into TMUs 8/9/10 with alpha-channel reuse:

```
TMU 8: _n.tga  — RGB normal, alpha = AO
TMU 9: _R.tga  — R = roughness, alpha = emissive mask
TMU 10: _M.tga — R = metallic
```

Depth capture takes the highest still-available slot, **TMU 7**.

### 1.8 ARB FP 1.0 hard limits in this build

| Resource | Practical limit | Notes |
|---|---|---|
| ALU instructions | ~72 | Drivers report higher; exceeding causes silent compile fails |
| Texture instructions | ~24 | TEX, TXB, TXP combined |
| Temporaries | 32 | Old drivers might be 16 |
| Texture indirections | 4 | Dependent texture chain depth |

**No `DST` op** — that's vertex-program only. Common gotcha when porting attenuation formulas.

**Compile errors are silent.** When `glProgramString` rejects an FP, the engine binds a non-functional program and the affected geometry renders black. There is no error log; you bisect by reverting features.

### 1.9 An area can force an envmap cube that overrides the per-texture TXI (2026-05-30)

The cube map a reflective surface samples is **not always** the one named in that texture's TXI `envmaptexture`. A specific area/module can **hard-bind a different cube for the whole scene**, and that binding wins even when the texture's TXI (or a loose Override) points at another cube.

Confirmed on **M4-78 dro802** (red-fog zone): the planet's reflective surfaces are authored for `CM_804DRO`, but dro802 forces `cm_m478` (a greenish, low-detail stock cube) onto everything. Symptom: reflections there are green / low-detail / "blurry" while the *same texture* in other M4-78 rooms reflects the crisp `CM_804DRO`. Dropping a loose `Override/cm_m478.tga` (better cube) **changed the reflection in dro802** — proving the area binds `cm_m478`, not the TXI's `CM_804DRO`.

**Why our shader fixes didn't move it:** the FP reflects whatever cube the engine bound; the cube *content* is the limiter, not the shader math. Chasing this in the shader (LOD bias, env weight, vertex-light gate) was barking up the wrong tree — though the LOD-bias fix (see `SHADER_REFERENCE`) was a real, separate correctness bug.

**How to diagnose which cube is actually bound** (file log, `LOCATION|envmap|id=N|name=?`):
- `name=<loose name>` (e.g. `cm_804dro`) → the bound cube is a **loose Override** file.
- `name=?` → the bound cube came from a **BIF archive** (stock); our CreateFileA name-map never saw it. A constantly-bound `name=?` cube in a scene = an area-forced/stock cube.
- A `PBR|forget|...|name=cm_X` for the cube you *expected* means it was unloaded — it's not the one in use.

**Fix is content-side, not DLL/shader:** drop a loose `Override/<forced-cube-name>.tga` (+ `.txi` containing `cube 1`) to replace the low-detail forced cube — e.g. copy the good `CM_804DRO.tga` to `cm_m478.tga`. Where the area forces the cube (`.are`/`.git`/room model) isn't fully reverse-engineered; the empirical override is enough.

### 1.10a Generic vertex attribs (weights/bone indices) — glBindAttribLocation (2026-08-11)

**Critical for skinned character GLSL ports.** KOTOR2 submits bone weights and bone
indices via `glVertexAttribPointerARB` on **generic attribute slots 1 and 4** — NOT
conventional arrays (glWeightPointerARB doesn't exist; the engine uses generic attribs).

The ARB VP reads them as `vertex.attrib[1]` (weights) and `vertex.attrib[4]` (bone indices).

**GLSL 1.20 has NO built-in for generic attribs.** The conventional built-ins map to
DIFFERENT slots:
- `gl_SecondaryColor` = attrib[4] (NOT attrib[1])
- `gl_MultiTexCoord4` = attrib[12] (NOT attrib[4])
- `gl_Color` = attrib[3]
- `gl_Normal` = attrib[2]
- `gl_Vertex` = attrib[0]

So the naive port (`gl_SecondaryColor` for weights, `gl_MultiTexCoord4` for bone indices)
is **wrong** — it reads the wrong vertex data → vertices fly to random bone transforms →
the "hedgehog" artifact (polygons spiking in all directions from skinned characters).

**Fix:** declare named attributes and bind them via `glBindAttribLocation` BEFORE linking:
```glsl
attribute vec4 aWeight;   // bound to slot 1
attribute vec4 aBoneIdx;  // bound to slot 4
```
```cpp
glBindAttribLocation(prog, 1, "aWeight");
glBindAttribLocation(prog, 4, "aBoneIdx");
// then glLinkProgram
```
This tells the GL driver "route generic attrib[1] → aWeight, attrib[4] → aBoneIdx", so
the engine's `glVertexAttribPointerARB(1, ...)` and `(4, ...)` feed the correct data.

**Lesson for tessellation / future vertex work:** any KOTOR2 vertex data submitted via
generic attribs (not conventional texcoord/color/normal arrays) requires this binding.
The engine never calls `glBindAttribLocation` itself, so the GLSL program must do it at
link time. Conventional arrays (glTexCoordPointer → gl_MultiTexCoordN, glColorPointer →
gl_Color, glNormalPointer → gl_Normal) work without binding — only generic attribs need it.

### 1.10b Bone skinning port: `getBoneRow` double-×3 (2026-08-11)

**Critical for skinned character GLSL ports — the bug AFTER glBindAttribLocation is fixed.** Once bone weights and indices flow correctly, a second porting error appears: the bone-matrix lookup helper multiplies the stride internally, but the caller has already multiplied it — producing a *double* scaling that maps each bone to the wrong matrix.

**ARB ground truth** (e.g. `vp_skinned_env_lit.txt`):
```
MUL   vReg0, vertex.attrib[4], program.env[16].zzzz;   // bone_number × env[16].z
ARL   A0.x, vReg0.x;                                    // A0 = floor(offset)
DP4   vReg1.x, vertex.position, boneArray[A0.x + 0];    // boneArray[A0 + row]
```
`PARAM boneArray[51] = {program.env[18..68]}`. `env[16].z = 3.0` — the stride (3 rows per bone). Raw bone numbers are multiplied by 3 to become the OFFSET into the bone array.

**Naive GLSL port (BUGGY):**
```
getBoneRow(bone, row) = uBone[bone*3 + row];   // helper multiplies by 3
caller: i0 = int(aBoneIdx * uBoneCfg.z)          // caller ALSO multiplies by 3
getBoneRow(i0, 0) → uBone[i0*3 + 0] = uBone[bone*9 + 0]  // DOUBLE
```
Result: bone 0 → correct (0×9=0). Bone 1 → uBone[9] (data for bone 3). Bone 2 → uBone[18] (data for bone 6). Limbs attach to wrong matrices → **legs and arms fold inward** (coherent-but-wrong, unlike the random spikes of the hedgehog).

**Fix** — mirror ARB semantics: helper takes the OFFSET (already ×3), returns `uBone[off + row]`. Body is identical — only comparison values change (`bone == N` → `off == N×3`). Caller unchanged (`i0 = int(boneIdx.x)` = offset). Matches ARB `boneArray[A0+row]` exactly regardless of `env[16].z` actual value.

**Lesson:** when porting ARB `ARL + array[A0 + row]`, `A0` holds the flat-array **offset** (already scaled by the stride). The GLSL helper must use it directly — never multiply by the stride again.

### 1.10c Cross-program uniform location bug + `SetEnv4ForActive` pattern (2026-08-11)

**Critical for multi-program GLSL loader architecture.** Uniform *locations* returned by `glGetUniformLocation` are **per-program** — location 5 in program A may be `pbr` while location 5 in program B is `uK0` (shadow matrix). Reusing a location from one program while another is active writes to whatever uniform B has at that index → cross-program corruption.

**Bug example:** pre-cached location structs (`s_u`, `s_er`) resolved against `s_progDiffuseMain` were reused when `s_progEnvRefl` was active. `SetEnv4(GL_FRAGMENT_PROGRAM_ARB, 100, s_u.k0)` wrote to `s_progEnvRefl`'s location for `pbr` instead of `uK0` → shadow-K went into PBR params, camLight into nothing → no character shadows at all.

**Fix (`SetEnv4ForActive`):** resolve `glGetUniformLocation` from the **currently active program** on every call:
```cpp
static void SetEnv4ForActive(GLenum target, GLuint envSlot, const char *name) {
    GLint prog = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    GLint loc  = (prog != 0) ? pglGetUniformLocation((GLuint)prog, name) : -1;
    if (loc < 0) return;
    GLfloat e[4]; pglGetEnvFv(target, envSlot, e);
    pglUniform4fv(loc, 1, e);
}
```
Every per-draw upload now resolves against the active program.

**Same-class bug in `UploadPerDrawSkinnedLit`:** `uBone[0]` and `uBoneCfg` were resolved on a hardcoded fallback program (`s_progDiffSimple ? s_progDiffSimple : (s_progDiffNolm ? ...)`). When multiple lit_fog programs load, fallback ≠ active → bone palette writes wrong program → character collapses. Fix: use `GL_CURRENT_PROGRAM` (matching `UploadPerDrawEnvRefl`). Only `diff_simple` had linked at the time, so fallback==active (latent — would break when `diff_nolm`/`headgear` came online).

**Rule: never resolve a uniform location against any program except the currently active one.** Pre-cached locations are valid for a single program only. `SetEnv4ForActive` is the mandatory pattern.

### 1.10d PushEnvParams slot overflow — shadow sentinel guards (2026-08-11)

**Bug that killed ALL shadows (ARB + GLSL) simultaneously.** `PbrTune_PushEnvParams` collects tuning sliders into a local `slot[12][4]` array indexed `envSlot - 24`. Expanding from 8 to 12 rows (for the camera-light group at `env[34]`) ran the push loop over all 12 slots UNCONDITIONALLY — including `env[32]` and `env[33]`.

`env[32]` and `env[33]` are **shadow sentinels** — `shadow_map.cpp` writes `uPcf` to FRAGMENT `env[32]` (PCF step = 1/resolution). The push loop overwrote them with `{0,0,0,0}` (unused rows with `g_tuneIsSet==false` push zero) → PCF step zeroed → all shadows disappeared.

**Fix:** skip sentinel slots in the push loop (`s==8||s==9` → env[32..33]), and expand `slot[8]` → `slot[12]` for the 3 camera-light sliders at `env[34]`.

**Rule: every new env-slot consumer must audit the full push loop for overlap.** The per-bind re-push exists because the engine stomps env state between FP binds — any slot the push writes to is overwritten, even if set by a different subsystem.

### 1.10e Camera-light gate on stock textures — `rs=1` sentinel (2026-08-11)

**Bug that made the camera-light slider do nothing on stock (non-PBR) textured surfaces.** The camera-light spec term was gated by `(1.0 - clamp(rs, 0.0, 1.0))` — multiplying the spec by `1 - roughness`. Stock textures have no roughness sibling → engine sets `pbr.y = -1.0` → shader sentinel: `float rs = (pbr.y < 0.0) ? 1.0 : pbr.y;` → `rs = 1.0`. Then `1.0 - clamp(rs, 0.0, 1.0) = 0` → camera light multiplied by zero → disabled on every stock-textured surface. The slider worked only on PBR surfaces (5% of the game), defeating its purpose as a universal visual aid.

**Fix:** gate camera light on `usePbr` instead of roughness:
```glsl
float camSpec = … * mix(1.0, 1.0 - clamp(rs, 0.0, 1.0), usePbr);
```
When `usePbr=0` (stock), `mix(1.0, …, 0) = 1.0` → un-gated. When `usePbr=1` (PBR), roughness still gates the spec. The slider now works on ALL surfaces.

**Rule:** when gating a "universal tool" feature (camera light, rim light, debug viz), NEVER use a gate that evaluates to zero for the stock-texture default. Stock surfaces are 95% of the game. Gate on `usePbr` so stock gets full contribution, PBR uses authored gate.

### 1.10 GLSL **is** available through the Aspyr wrapper (2026-06-01)

The feasibility probe (`GlslProbe_Init`, `glsl_program.cpp`) **succeeded** on the target driver (NVIDIA RTX, GL 4.6 compat). Despite the wrapper being a D3D9-style command stream, full GLSL is exposed — contradicting the earlier NO-GO worry in the plan.

- Every GLSL entry point resolves via `orig_wglGetProcAddress`: `glCreateShader, glShaderSource, glCompileShader, glGetShaderiv/InfoLog, glCreateProgram, glAttachShader, glLinkProgram, glGetProgramiv/InfoLog, glUseProgram, glGetUniformLocation, glUniform1i/1f/3fv/4fv, glUniformMatrix4fv, glDeleteShader/Program`. None NULL.
- `GL_SHADING_LANGUAGE_VERSION` ≥ 1.20; trivial VS+FS compiles + links.
- **Compatibility-profile built-ins all work** — `gl_ModelViewProjectionMatrix`, `gl_ModelViewMatrix`, `gl_Vertex`, `gl_Normal`, `gl_MultiTexCoord0/1`, `gl_FrontColor`, `gl_FrontSecondaryColor`, `gl_TexCoord[]`, `gl_Color`, `gl_SecondaryColor`, `gl_FragCoord`. So the engine's existing fixed-function vertex submission feeds a GLSL program **unchanged** — no VBO/attrib wiring needed.
- **Caveat:** `gl_Fog` / `gl_FogFragCoord` are mangled by the wrapper (unreliable). Read fog state via `glGetFloatv(GL_FOG_START/END/COLOR)` and carry the factor yourself (we pass it per-vertex through `gl_FrontSecondaryColor`, the same register the ARB used). See §3.8.

This unblocks the loops/branches that ARB FP 1.0 categorically cannot do (POM/SSR raymarch, dynamic light loops).

---

## 2. The Aspyr GL driver quirks

### 2.1 `glCopyTexSubImage2D` rejects depth textures

After the first frame succeeds, every subsequent `glCopyTexSubImage2D` into a `GL_DEPTH_COMPONENT` texture fails with `GL_INVALID_VALUE` (0x0501). Identical arguments, no state changes.

```c
// Frame 1
glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, 1920, 1080, 0,
             GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0,0, 0,0, 1920,1080);  // OK, err = 0
// Frame 2
glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0,0, 0,0, 1920,1080);  // err = 0x0501
```

Workaround: full `glCopyTexImage2D` every frame. At 1080p that's ~6 MB allocation + copy per snapshot, which drops FPS to 4 without throttle. With 1-in-5-frame throttle FPS stays at 50+.

### 2.2 `wglUseFontBitmapsA` silently no-ops

The call returns success but produces no glyph display lists. The in-game tuning overlay (`pbr_tune.cpp`) uses GDI → DIB → `glTexImage2D` texture atlas instead.

### 2.3 Camera world position lives in env-param `.w` columns

```
camera.x = program.env[92].w
camera.y = program.env[91].w
camera.z = program.env[90].w
```

Standard pattern in fragment programs:
```
MOV camW.x, program.env[92].w;
MOV camW.y, program.env[91].w;
MOV camW.z, program.env[90].w;
```

**Extended:** `env[90..92]` is the full camera-to-world (view-inverse) 4×4 with rows 0..2 in the `.xyzw` of each slot. Row 3 is implicit `(0,0,0,1)`. The shadow-map subsystem reads all 12 floats via `glGetProgramEnvParameterfvARB` to compose its light-space transform. See `SHADOW_MAP.md` §3.

### 2.4 K2 world is Z-up

`viewInv.m[11]` (camera world Z) stays constant as the player walks the floor. ≈25 on Telos surface, ≈1 on cantina/interior floors. The vertical "up" axis in this engine is the world Z axis, not Y. Anything doing `lookAt` or world-aligned math has to use `up = (0, 0, 1)`.

Easy way to be misled: GL textbooks mostly assume Y-up. Carrying the assumption into a `lookAt` call here produces a rotated light view that doesn't match the scene.

### 2.5 Z-buffer precision is hyperbolic and clusters near 1.0

Outdoor scene at typical distances: most fragments have window-space depth in `[0.97, 1.0]`. The full `[0, 1]` range is heavily compressed at the near end.

Implications for any depth-based effect:
- 1-tap depth-diff comparisons fundamentally **cannot** distinguish "scene depth growing" from "occluder closer". Both produce sub-0.001 differences.
- Linear-z conversion via `1/(1-z)` is exponential at `z → 1.0`. Tiny noise becomes huge linear-z noise.
- Multi-tap with per-tap scaled bias is fragile — at sky pixels (`z=0.9999`) the bias would need to be 10000+.

→ Screen-space sun shadow ray-march was **cut** after multiple iterations. See SHADER_REFERENCE for the autopsy. Replaced by real shadow map (`SHADOW_MAP.md`) — uses light-space linear depth, sidesteps the hyperbolic-z problem entirely.

---

## 3. The proxy DLL (our mod)

### 3.1 Replaces `opengl32.dll` via search order

Our 32-bit DLL ships next to `swkotor2.exe`. Windows loader finds it before `C:\Windows\System32\opengl32.dll`. We `LoadLibraryA` the system one by full path at `DllMain` time.

### 3.2 368 stdcall trampolines, only one is converted

`opengl32.cpp` defines 368 naked `__E__N__` functions that `jmp p[N*4]` into the real GL. Listed in `opengl32.def` with their stdcall names and ordinals, so the linker emits correctly-decorated exports matching the real DLL's ABI.

**Only `__E__355__` (wglGetProcAddress)** has been successfully converted to a non-naked C wrapper. Attempts to convert `__E__72__` (glDrawArrays), `__E__74__` (glDrawElements), `__E__361__` (wglSwapBuffers) crashed the game on launch. **Rule:** add hooks via IAT patching of `swkotor2.exe`'s imports, not by editing exports.

### 3.3 IAT hooking is the safe interception path

`source/iat_hook.cpp` walks the PE import descriptor of `swkotor2.exe`, finds the entry matching `(dll_name, function_name)`, and swaps its function pointer. Used for:

- `kernel32!CreateFileA/W` → file load logging
- `gdi32!SwapBuffers` → frame-end snapshot
- `opengl32!wglSwapBuffers` → fallback frame-end (typically `NULL` in K2)
- 9 fixed-function entry points (matrix, light, depth-state) → state mirror

No code patching at runtime. Reversible. Only affects the target module's imports.

### 3.4 `wglGetProcAddress` recursion trap

If you resolve `wglGetProcAddress` itself via `GetProcAddress(GetModuleHandleA("opengl32.dll"), ...)` you get **our own `__E__355__` wrapper**, not the system one. If our wrapper then dispatches a name back to itself for a function we don't have, infinite recursion → stack overflow at load.

Fix: cache the system pointer at `InitGL` time (`orig_wglGetProcAddress` in `glFunctions.h`) and always resolve through that.

### 3.5 Shader override flow

1. Engine calls `glProgramStringARB(target, format, len, src)`.
2. Our `my_glProgramString` MD5-hashes `src`, looks up `shader_ident.txt` for a friendly name.
3. Dumps the original to `shaders_original/<name>.txt` if not already there.
4. Checks `shaders_override/<name>.txt`. If present, substitutes `src` with the override file's contents.
5. Forwards to real `glProgramStringARB`.

Logging in `logs/pbr_file_log.txt` reports each substitution. If a shader doesn't render correctly, **first verify the override was picked up** (grep for `SHADER|override|<name>`).

**The override `.txt` is read ONLY at step 1 — when the engine calls `glProgramStringARB` to upload the program.** The engine compiles each ARB program once and **caches the GL program object**; it does not re-upload on a save-load or a zone transition (it reuses the cached id). So editing a `shaders_override/*.txt` and reloading a save changes **nothing** — you'll swear the edit "did nothing." Only a **full process restart** (quit to desktop → relaunch) guarantees the engine re-uploads every program and re-reads your file. (Cost us a full debugging detour on 2026-05-30: a baked constant edit appeared to be a no-op purely because the game hadn't been restarted.)

**Canary tinting to map FP→object** (validated method): to learn *which* FP draws a given in-game object, add a constant `MUL <out>.rgb, <out>, {r,g,b}` just before `MOV result.color` in the candidate FP(s) — a different colour per FP — deploy, restart, and look. Objects drawn by that FP take the tint; everything else renders normally. One restart maps the whole game. See `SHADER_REFERENCE.md` "FP→object map (canary-validated 2026-05-30)".

### 3.6 Tuning overlay (DEL key)

Slider panel with 19 parameters in 7 groups (surface detail, material, reflection, local light, sun direction, sun intensity, sun color). Each row pushes its value to a `program.env[24..28]` slot consumed by every PBR fragment program.

- **Input**: `GetAsyncKeyState` polling, no message hook.
- **Drawing**: GDI-built font atlas → `glTexImage2D` → textured quads.
- **Persist**: `pbr_tune.ini` next to the DLL. Loaded at init, saved on overlay close. INI keys are stable across label renames.
- **Re-push timing**: `glClear` hook sets `g_tunePushNeeded = true`. Env params are state-bound to the FP target (not per-program), and K2 stomps them between FP binds, so we must re-push every frame. The `glClear` hook is the cleanest re-arming point.
- **F2/Backspace sun capture**: reads `GlState_FrameViewMatrix()`, derives forward vector, pushes to env[27].xyz. The captured matrix only exists because we read `glGetFloatv(GL_MODELVIEW_MATRIX)` from inside `glPushMatrix` at the first non-identity rotation per frame (see §1.4).

### 3.7 `DiagLog` is the failsafe diagnostic channel

`pbr_tune.h` exposes `DiagLog(fmt, ...)` writing to `logs/pbr_tune_diag.log` via plain stdio. Use it for anything that needs to survive a broken `file_logger` path. The main `pbr_file_log.txt` requires `CreateFileA` to work correctly, which has occasionally been the broken thing under diagnosis.

### 3.8 GLSL material override — replaces an ARB pair via `glUseProgram` (2026-06-01)

`glsl_program.cpp` replaces the engine's `(vp_static_lit_fog + fp_worldtex_diffuse_main)` ARB pair with a faithful GLSL port (`shaders_override/fp_worldtex_diffuse_main.{vs,fs}.glsl`). A live GLSL program bound with `glUseProgram` overrides **both** the ARB VP and FP for that draw — the ARB enables don't need touching. Toggled live by the "GLSL material" tune row (FRAGMENT `env[26].w`).

- **10-program loader** (2026-08-11): the system now loads **10 GLSL program pairs** covering the major render paths:
  - **Model (skinned):** `(vp_skinned_env_lit + fp_model_env_reflective)`, `(vp_skinned_env_lit + fp_model_bump_env_spec)`, `(vp_skinned_lit_fog + fp_model_diff_simple)`, `(vp_skinned_lit_fog + fp_model_diff_nolm)`, `(vp_skinned_lit_fog + fp_model_headgear_legacy)`, `(vp_skinned_env_lit + fp_model_armor_legacy)`
  - **World (static):** `(vp_static_lit_fog + fp_worldtex_diffuse_main)`, `(vp_static_env_fog + fp_worldtex_env_reflective)`, `(vp_static_env_fog + fp_worldtex_lm_env)`
  - **Door:** `(vp_static_env_fog + fp_door)`
- **Upload architecture** — three tiers: (1) **Per-program stable** uniforms via `UploadStableForProg` + `s_stableProg` guard — frame-invariant tune params (tonemap, sun, shadow K, camera-light) uploaded once per program per frame. (2) **Per-draw** uniforms via `UploadPerDraw*` — bone palette, per-draw lights, PBR params, fog. (3) **Uniform names** resolved via `SetEnv4ForActive` on `GL_CURRENT_PROGRAM` at each upload (§1.10c).
- **Tonemap 10× compression + bloom clamp `min(1.0)`** (2026-08-11): the shoulder tonemap uses knee=0.9 with 10× compression (`excess / (1 + excess*10)`) to preserve normal-map contrast below 0.9 while rolling off specular spikes. A final `min(rgb, 1.0)` prevents bloom from over-bright specular/sun terms.
- **Match by program NAME, not id alone.** `GlslMaterial_Apply` reads the currently-bound ARB VP/FP ids (`glGetIntegerv(GL_*_PROGRAM_BINDING_ARB)`), maps id→friendly name (cached `strcmp`), and applies only when **both** match the target pair (`glsl_program.cpp:516,525`).
- **CRITICAL — gate on `glIsEnabled(GL_FRAGMENT_PROGRAM_ARB)`, not the bound id.** K2 draws the **skybox and grass with the ARB programs DISABLED** (fixed-function) but leaves the *previous* draw's program **binding id** in place. So the id→name check passes on those draws (stale name = last terrain program `fp_worldtex_diffuse_main`), and because `glUseProgram` overrides fixed-function too, the GLSL program hijacked draws the engine never meant to shade → washed/overbright sky, camera-dependent grass. **The bound program *id* is NOT authoritative for whether a program is active; `glIsEnabled(GL_FRAGMENT_PROGRAM_ARB)` is.** One-line gate fixed sky + grass at once. Tradeoff: those fixed-function draws get no GLSL sun shadow (accepted). This was the root cause behind a long fog/secondary-color chase — the sky never should have reached the shader at all.
- **Uniforms come from the SAME `program.env` slots the ARB read** — `glGetProgramEnvParameterfvARB(target, slot, …)` so inputs are bit-identical to the ARB path. **VERTEX and FRAGMENT env are SEPARATE arrays:** e.g. light0 world pos is VERTEX `env[87]` (the VS reads it) AND FRAGMENT `env[87]` (the FS reads it) — different GL state, both captured. Per-material `env[20..23]` pushed per-draw (`UploadPerDraw`); global tune/sun `env[24..30]` cached per-frame (`UploadStable`).
- **GLSL source is file-loaded** (`shaders_override/*.glsl`, read at link time) → editable, **restart-only, no DLL rebuild** — same ergonomics as the ARB `.txt` overrides. The `.cpp` infra itself needs a rebuild.
- **GLSL and ARB coexist; only the static-world pair is GLSL.** Characters (skinned → `vp_skinned_*` + `fp_model_*`) and every other world shader stay ARB. **Consequence:** any change to a lighting *convention* (e.g. normalizing the sun dir) applied to one path but not the other produces a visible **world-vs-character mismatch**. Change all lit shaders or none. See `SHADER_REFERENCE.md` "Lighting-model gotchas".
- **Wrapper fog quirk handled here:** `glFogf` forwards to real GL (`opengl32.cpp`), so `glGetFloatv(GL_FOG_*)` returns the engine's live values; the FS gets the per-vertex fog factor through `gl_FrontSecondaryColor` (the ARB VP's `result.color.secondary` register) so both paths get identical wrapper treatment.

---

## 4. Features tried and cut

### 4.1 Contact shadow (between characters and walls)

**Status: working, then cut.** The 2-snapshot depth setup (mid-frame `fp_model` bind + end-of-frame swap) gave character-on-wall AO at zero lag. Cut from the world FP during the screen-space sun shadow attempt; never restored. The depth-capture subsystem still exists and could be re-enabled.

### 4.2 Screen-space sun shadow

**Status: cut after multiple iterations.** Attempted:

1. 1-tap depth-diff along sun screen direction → noise looks like edge halos.
2. Multi-tap (4 samples, scaled bias) with linear-z `1/(1-z)` → bias blows up at far depths; sun pseudo-spotlight gets killed by false occlusion in many areas.
3. Throttled depth snapshots (1 in 5 frames) → "doubled silhouette" ghost of dynamic geometry from older frames.

Root causes (all of them load-bearing):
- K2's hyperbolic z and outdoor depth clustering near 1.0 (§2.4).
- `glCopyTexSubImage2D` quirk forced full Copy and full-Copy at 1080p needed throttle (§2.1).
- Throttle introduced temporal ghosting on dynamic geometry; without throttle FPS = 4.

The depth subsystem is left in place but `s_snapshotEnabled = false` until a different depth-driven feature lands. The Shadow * tune rows have been removed from the overlay.

### 4.3 Sun "spotlight" appearance

Not a bug — view-dependent **specular** highlight `(N·H)^exp` where `H = normalize(L + V)`. The patch tracks the surface where `H` aligns with the sun direction; sliding `Sun dir Z` (vertical component) moves and resizes the patch.

To get pure parallel-ray sun illumination without the highlight, set `Sun spec strength` (env[25].w) to 0. The diffuse term (env[28].x) handles N·L parallel-ray sun separately and is unaffected.

### 4.4 Real shadow map (working, ongoing)

**Status: shipped across world + model + character FPs.** Directional sun shadow with skinned casters, geometry cache + static/dynamic split, ping-pong dual-map (last-frame for world receivers, current-frame for model self-shadow), resolution slider (1024–8000), per-pixel PCF dither. See `SHADOW_MAP.md`.

What we learned implementing it:

- **Two distinct world transforms.** `K_main = lightProj * lightView` for the main FP (samples on world pos `P5`). `K_caster = K_main * view_inv` for the caster VP (multiplied by engine modelview to reach light clip from model-space vertex). Pushing `K_caster` to both slots is the easy bug; it sends junk `view_inv * worldPos` into the sampler and shows up as rectangular wall-shaped false shadows.
- **`env[90..92]` is the engine view-inverse**, not just camera position. The shadow code reads all 12 floats; depth code only used the `.w` lanes.
- **Z-up.** §2.4. The light view's `up` vector is `(0,0,1)`.
- **CLAMP_TO_EDGE leaks depth.** Out-of-`[0,1]` UV samples return the boundary depth. If the boundary has rasterized geometry (depth < 1), out-of-bounds fragments falsely shadow as huge wall-shaped patches. Fix: in-shader UV bounds check forces lit when out.
- **Light-axis texel snap, not world-axis.** Snapping cam position to integer multiples in world XYZ leaves fractional drift on diagonal motion because world axes don't align with the light frustum's lateral plane. Snap in `r/u` axes derived from sun + up.
- **Sun-only application.** Multiplying the shadow factor into final `d.rgb` darkens ambient + L0 + cube reflection too, which makes back-facing surfaces (no sun on them anyway) look wrongly tinted. Apply only to the sun spec / diffuse contributions before they accumulate.
- **Empty caster passes are normal.** Dialogue / cutscene frames sometimes draw only HUD-equivalent stuff; the shadow tex stays at the cleared value. Not a bug — those frames' world FP doesn't sample either.
- **Per-draw state save**, not per-pass. K2 changes viewport mid-pass (HUD compositing, inventory sub-renders) and may toggle the colormask for stencil tricks. Cached `BeginCasterPass` values go stale.
- **Never mix matrices captured at different moments of the frame (2026-05-30).** Live casters built `lightMVP = K_main · view_inv(frameStart) · mv(drawTime)`. When the camera rotates between the frame-start `glClear` and the actual draw, the stale `view_inv` doesn't cancel the live `mv`'s camera → a residual camera-rotation skews the caster's depth onto the wrong texel → live self/cast shadows jump on camera rotation and creep onto lit areas. The cached static geometry was immune because it replays `K_main · frozenModel` (no per-frame `view_inv`), which is exactly why "the level was rock-stable but everything dynamic jittered." Fix: read `view_inv` at **draw time**, same instant as `mv`, so `view_inv·mv = world` cancels the camera exactly and matches the receiver's `K_main·worldPos`. Rule: if two matrices must cancel a transform, sample them at the same instant. See `SHADOW_MAP.md` §2.
- **Diagnose by measurement, not theory.** This bug survived several wrong guesses (hysteresis tuning, FP-precision). What cracked it: a per-frame `DiagLog` of one locked caster's world pos + light-space texel proved the *world transform* was bit-stable while the *texel* skewed only under camera motion — pointing straight at the camera-cancellation term. Add the diagnostic before forming the fix.
- **State save granularity matters.** Save+restore VP id including `0`, and both `VERTEX_PROGRAM_ARB` / `FRAGMENT_PROGRAM_ARB` enabled flags via `glIsEnabled`. K2 sometimes draws with no VP bound (fixed function); if we only restore `glBindProgram` and not the `glEnable` state, our shadow VP stays active for the next engine draw.

---

## 5. Rules of thumb when adding features

1. **Hook via IAT, never convert naked exports.** Only `wglGetProcAddress` is safe to wrap; everything else crashes.
2. **Resolve GL extensions through `orig_wglGetProcAddress`**, never via `GetProcAddress` of `opengl32.dll`.
3. **Re-push env params from a `glClear` hook**, not only on value change. The engine stomps state between FP binds.
4. **Filter swap-time work by viewport size** — `w < 1280 || h < 720` means it's a sub-render, skip it.
5. **Don't trust `state.matrix.*` or `state.light[N].*` in shaders.** Read `program.env[N]` slots.
6. **Stay under 72 ALU instructions per FP.** Aspyr won't report a compile error, just renders black.
7. **No `DST` opcode.** Vertex-program only.
8. **Test shader changes by deploying to the game's `shaders_override/`** and restarting. The Aspyr wrapper does not hot-reload programs.
9. **Use `DiagLog` for failsafe diagnostics** — survives even when `file_logger`'s `CreateFileA` path is broken.
10. **Verify shader override pickup** in `logs/pbr_file_log.txt` before debugging anything else. "It's not working" most often means the file didn't deploy.
11. **Two matrices for shadow work**, not one. `K_main` (worldPos→clip) for FP samplers, `K_caster` (model→clip via engine modelview) for the caster VP. See `SHADOW_MAP.md` §2.
12. **World is Z-up.** `up = (0,0,1)` in `lookAt`, not `(0,1,0)`. Camera Z is constant as the player walks.
13. **When sampling a depth tex, set `GL_DEPTH_TEXTURE_MODE = GL_LUMINANCE` + `GL_TEXTURE_COMPARE_MODE = GL_NONE`.** Default is `GL_INTENSITY` with hardware compare on; depth ends up in `.a` and silently breaks `.x` math.
14. **GLSL is available** (§1.10) — use a `glUseProgram` override for anything ARB FP 1.0 can't do (loops/branches: POM, SSR, light loops). **Gate the override on `glIsEnabled(GL_FRAGMENT_PROGRAM_ARB)`, never the bound program id** (§3.8) — the id is stale on fixed-function draws.
15. **A GLSL material must read the same `program.env` slots as the ARB it replaces** — `glGetProgramEnvParameterfvARB`, and remember VERTEX vs FRAGMENT env are separate arrays.
16. **Don't change a lighting *convention* on one shader path only.** The static-world pair is GLSL; characters + other shaders are ARB. Normalize the sun dir / change a term in one and not the rest → world-vs-character mismatch. All or none. (`SHADER_REFERENCE.md` "Lighting-model gotchas")
17. **When porting ARB `ARL + boneArray[A0 + row]` to GLSL: `A0` is the ARRAY OFFSET, not the bone number.** The stride (env[16].z=3) is already applied by the caller (`aBoneIdx × uBoneCfg.z`). The GLSL helper must treat its parameter as the offset, not multiply by the stride again — that double-scales and maps limbs to wrong bones → folding inward. (§1.10b)
18. **Never resolve a GLSL uniform location against any program except the currently active one (`GL_CURRENT_PROGRAM`).** Locations are per-program; a location from program A used while program B is active writes to whatever uniform B has at that index → cross-program corruption. Use `SetEnv4ForActive`. (§1.10c)
19. **Audit the full env-param push loop when adding new slots.** The per-bind re-push stomps every slot in its range; a new slot can silently overwrite a slot owned by a different subsystem. The shadow sentinels (`env[32..33]`) are especially fragile — zeroing them kills all shadows across both ARB and GLSL paths. (§1.10d)
20. **Never gate a universal feature on a "missing" sentinel that evaluates to zero for stock textures.** Stock surfaces are the 95% case. A roughness-based gate silently disables the feature for everything that doesn't have a PBR sibling. Gate on `usePbr` instead so stock textures get the full contribution and PBR surfaces use the authored gate. (§1.10e)
21. **Per-program stable-uniform guard (`UploadStableForProg`):** when multiple GLSL programs share a draw call, upload per-frame (stable) uniforms once per program per frame, not every draw. `s_stableProg` tracks the last uploaded program; `GlslMaterial_OnFrame` resets it at `glClear`. Without this, a VERTEX env read from an ARB-bound program that was later GLSL-overridden would push the stable uniforms to the wrong GLSL program.

- **Near/far plane values** — not exposed in any env param. Could probe via `glGetFloatv(GL_DEPTH_RANGE)` plus inspection of the projection matrix from `program.env[?]`. Required for any real depth-linearization work (less critical now that light space provides linear depth).
- **Per-skybox sun-direction preset** — original ask; deferred while shadow work was active. The capture button (Backspace) + INI persist already exist; what's needed is a `texture_name → sun_dir_xyz` lookup keyed off the cube envmap or the skybox 2D texture name. `PbrGetTextureName(GLuint)` exists in `pbr_state.cpp`.
- **L0 vs sun separation in indirect lighting** — both contribute to the same `r` accumulator with no way to gate per-pixel. Shadow map handles direct sun term only.
- **Whether to bring back the static-geometry contact shadow** independent of the sun-shadow attempt. The depth subsystem still works; the snapshot enable flag is one line.
- ~~**Skinned caster VP.**~~ **DONE.** `vp_shadow_caster_skinned.txt` ships; skinned characters/droids cast animated (not rest-pose) shadows. Classified via `ClassifyCaster` — a skinned VP ⇒ `CC_CHAR` ⇒ live caster, never cached.
- ~~**Shadow sampler in non-walls FPs.**~~ **DONE.** Sampler is in all major world FPs (TMU 6 / last-frame map) and all model+character FPs incl. `fp_model_armor_legacy`/`fp_model_headgear_legacy` (TMU 7 / current-frame map). See `SHADOW_MAP.md` §16.
- **Stable-origin snap** as a cheap alternative to cascades. Pick a per-scene anchor (e.g. zone center), snap relative to that, only refit when the camera leaves a tolerance radius. Trades coverage flexibility for zero swim.
- **Character-only fill/ambient** — characters read darker than the lightmapped world (no lightmap + steep sun misses vertical surfaces; `SHADER_REFERENCE.md` "Lighting-model gotchas" #2). A small additive fill in `fp_model_*`, exposed as a "Char fill" tune row, would brighten characters without blowing out the world. Not yet built.
- **True sun-dir decouple (angle vs brightness)** — would require normalizing the sun dir in *all* lit shaders (world ARB + GLSL + every `fp_model_*`) and re-tuning every saved per-location calibration. Deferred; raw length currently doubles as a brightness trim (gotcha #1).
- **804dro character shadow offset** (2026-08-11): only on M4-78 area 804dro (not 801/802/803/805), character shadows are displaced — "in the distance, not under the character." Changing the sun angle makes them appear, but shifted. On other dro areas with the same normal-mapped textures, shadows are correct → not a texture or shader bug. Suspect: location-specific world-origin offset, or light-box anchor (`s_anchorWorld`/`s_extentWorld`) mismatched on 804dro. Needs: compare `s_anchorWorld` + caster world-pos on 804dro vs a working area (801dro); confirm whether world shadows also work on 804dro.
