# Depth capture & GL state mirror

Subsystem added in 2026-05 to give fragment programs access to screen-space depth and (eventually) view-space transforms. Implemented in `source/depth_capture.{h,cpp}` and `source/gl_state_capture.{h,cpp}`.

## Why depth at all

Stock ARB FP1.0 has no access to the depth buffer of the current draw. Any effect that needs neighbours' depth (contact AO, SSR, depth fog, soft particles, distance-based fade) requires the buffer copied into a sampleable texture.

## What we capture

| Resource | Source | How |
|---|---|---|
| **Scene depth** | Default framebuffer's implicit depth attachment | `glCopyTexImage2D(GL_DEPTH_COMPONENT24, ...)` then `glCopyTexSubImage2D` per frame |
| **Modelview matrix** | `glLoadIdentity/glLoadMatrixf/glMultMatrixf/glPushMatrix/glPopMatrix` | IAT hooks, in-DLL mirror stack |
| **Projection matrix** | Same | Same |
| **Fixed-function lights** | `glLightfv`, `glEnable(GL_LIGHTi)` | IAT hooks |
| **Per-frame phase signal** | `glDepthMask(FALSE)`, `glClear(GL_DEPTH_BUFFER_BIT)`, `glBindProgram(fp_model_*)` | Used to time depth snapshots |

## Empirical findings about K2

### K2 does NOT use FBOs for scene depth

ReShade's "Generic Depth" addon reports a `0x8218000000821a | 1920x1080 | D24S8 | 1420 draws` source — that handle is ReShade-internal and corresponds to the **default framebuffer's implicit depth attachment**, not a `glGenFramebuffersEXT` object.

Confirmed by our FBO interceptor (silent logging variant): K2 creates **9 color-only FBOs at startup** (texture ids 29–40) and never binds any of them during the frame. Scene goes directly to the WGL default backbuffer.

Implication: depth-via-FBO blit unavailable. Use `glCopyTexImage2D` from the default framebuffer instead.

### K2 doesn't use GL fixed-function matrices

`GlStateCapture_Init` probes 14 fixed-function entry points. K2 imports **9 of them**. The 5 it does NOT import are:

```
glLoadMatrixf  glLoadMatrixd  glMultMatrixd  glLightf  glLightiv
```

K2/Aspyr loads transforms directly into ARB program env params (matrix rows in `program.env[24..]` etc.) — the fixed-function modelview/projection stack is unused.

Implications:

- `state.matrix.*` and `state.light[N].*` ARB built-in references **return identity / zero** in this engine. Don't rely on them — read `program.env[N]` instead.
- Our matrix mirror in `gl_state_capture` is currently dormant: `glLoadMatrixf` is hooked but never called. Stays as scaffold for engines that DO use fixed-function (e.g., other Aurora-engine titles).

### K2 calls `gdi32!SwapBuffers`, not `wglSwapBuffers`

Standard for GL apps built before extension-import patterns settled. Both are IAT-hooked (`gdi32.dll:SwapBuffers` is the hot path; `opengl32.dll:wglSwapBuffers` IAT entry is `NULL`).

### Render order

Inferred from in-frame trigger fires:

```
glClear(DEPTH|COLOR)
  └─ skybox draw                       (depth writes off)
  └─ opaque world (walls)              fp_worldtex_env_fog / fp_worldtex_lm_env
  └─ opaque chars/objects              fp_model_env_fog
  └─ glDepthMask(FALSE)                ← opaque→transparent transition
  └─ transparency (particles, alpha)
  └─ UI overlay
SwapBuffers
```

The opaque→transparent transition is a reliable in-frame "scene done" signal.

## Snapshot timing strategy

**Two snapshots per frame**, into a single shared depth texture on TMU 7:

| Trigger | What's in the depth buffer | Used by |
|---|---|---|
| **Mid-frame** — first `glBindProgram(fp_model_*)` of frame | Walls only (chars haven't drawn yet) | Current-frame fp_model_* sampling → contact AO between chars and walls **with zero lag** |
| **End-of-frame** — `glDepthMask(FALSE)` + `SwapBuffers` | Worldtex + models + transparency | Next-frame `fp_worldtex_*` sampling → walls AO around char silhouettes (1-frame stale, acceptable for moving chars on static walls) |

The mid-frame trigger requires per-frame state:
- `glClear(GL_DEPTH_BUFFER_BIT)` resets the `s_charSnapshotDone` flag.
- `my_glProgramString` tags every override whose friendly name starts with `fp_model` (queries `GL_FRAGMENT_PROGRAM_BINDING_ARB = 0x8873` via `orig_glGetIntegerv` to learn the program id).
- `my_glBindProgram` sees the tagged id, calls `GlState_TriggerCharSnapshotIfNeeded()` which snapshots once and sets the flag.

## Depth texture format

`GL_DEPTH_COMPONENT24` 2D, on TMU 7 (constant `DEPTH_CAPTURE_TMU`):

```c
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,    GL_NEAREST);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,    GL_NEAREST);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,        GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,        GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE,    GL_LUMINANCE);  // depth in .r
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,  GL_NONE);       // not shadow2D
```

`DEPTH_TEXTURE_MODE = LUMINANCE` is critical: without it some drivers default to `INTENSITY` or shadow-compare, returning unexpected channels in ARB FP `TEX`.

### Why TMU 7

K2/Aspyr wrapper rejects `glActiveTexture(GL_TEXTURE11+)` silently. PBR siblings already occupy TMU 8/9/10 (normal/rough/metal). TMU 7 is the highest free slot in the supported range.

### Grow-only allocation

The viewport flickers between sizes (`1920x1080` ↔ `1920x1079`) across passes — likely UI overlay vs scene. Re-allocating via `glCopyTexImage2D` every flicker is expensive and triggers driver memory churn.

Fix: keep the largest size ever seen; subsequent snapshots use `glCopyTexSubImage2D` into the existing storage. Single allocation per session in practice.

## Sampling depth from a fragment program

```
ATTRIB pos = fragment.position;          # window-space xyzw
PARAM  vp  = program.env[10];            # (w, h, 1/w, 1/h)  pushed by my_glBindProgram

TEMP uv, dpth;
MUL uv.x, pos.x, vp.z;                   # → screen UV in [0..1]
MUL uv.y, pos.y, vp.w;
TEX dpth, uv, texture[7], 2D;            # dpth.r = raw GL depth [0..1]
```

`dpth.r` is non-linear (hyperbolic). For world-space distances:
```
linear_z = near*far / (far - depth*(far-near))
```

K2's exact near/far are not yet exposed in env params. Hardcoded `near=1, far=1000` works for ballpark.

## Failure modes encountered

| Symptom | Cause | Fix |
|---|---|---|
| Game flashes once, doesn't start | Naked-trampoline conversion of `__E__72__/74__/361__` to C wrappers | Use IAT hooks instead — never convert naked trampolines unless absolutely necessary |
| Stack overflow at load | `DepthCapture_InterceptProc` resolved FBO originals via `GetProcAddress(GetModuleHandleA("opengl32.dll"), "wglGetProcAddress")` → returned **our own** `__E__355__` wrapper → infinite recursion | Use `orig_wglGetProcAddress` (system pointer captured by `InitGL`) |
| No depth lines in log | `wglSwapBuffers` wrapper never fires — game uses `gdi32!SwapBuffers` | IAT-hook `gdi32.dll!SwapBuffers` (primary) and `opengl32.dll!wglSwapBuffers` (fallback) |
| Constant re-alloc thrash in log | Viewport flicker between 1080 and 1079 | Grow-only depth tex; `glCopyTexSubImage2D` when current size fits |
| Outlines visible but lag behind moving characters | Single end-of-frame snapshot; chars sample previous-frame depth | Add the mid-frame `fp_model` bind trigger so chars sample current-frame walls |
| Sample returns cyan-tinted garbage | `GL_DEPTH_TEXTURE_MODE` not set; driver returned alpha-only or compare-mode result | Set `GL_DEPTH_TEXTURE_MODE = GL_LUMINANCE`, `GL_TEXTURE_COMPARE_MODE = GL_NONE` at first alloc |
| Outlines on silhouettes, not on contacts | Depth-only neighbour-diff AO can't distinguish "depth jump at object edge" from "depth jump at corner crease" | Multi-direction occluder count gate helps partially. Real fix: push projection matrix, reconstruct view-space pos, compare in world units. |
| `glCopyTexSubImage2D` returns `GL_INVALID_VALUE` (0x0501) on every frame | **Aspyr-K2 driver quirk**: Sub-rect copy from default-FB depth into a `GL_DEPTH_COMPONENT` tex fails after first frame even with identical args | Full `glCopyTexImage2D` every frame works; Sub does not. See "Driver quirks" below. |
| Depth-tex sample returns 0 in shader despite `err=0x0000` | Sub never updated content after first frame — silent failure mode | Verify via `glGetTexLevelParameteriv(GL_TEXTURE_INTERNAL_FORMAT)` (returns 0x1902 = `GL_DEPTH_COMPONENT`). If non-zero internal fmt is set, content is empty. |
| Hundreds of "(re)alloc 189x354" log lines per second | K2 calls `SwapBuffers` for **offscreen renders** (inventory item icons, GUI panels) — our swap hook fires on each | Filter snapshots by viewport size — skip if `w<1280 || h<720`. |
| Snapshot drops FPS from 60 → 4 at 1080p | Driver software-fallbacks for "depth → texture" copy when source/dest format combination is suboptimal | Throttle: snapshot every Nth swap (5 = ~12 Hz updates, plenty for slow effects). |
| "Doubled silhouette" / ghosting on moving characters | Throttled snapshot lag — depth tex shows position from N frames ago, current draw is at new position | Cannot use throttled depth for any effect that touches dynamic geometry. Acceptable only for static-geometry effects (e.g., baked-into-light AO of walls). |
| Sun "spotlight patch" on ground when slider Z changed | View-dependent specular highlight (Blinn-Phong `(N·H)^exp`), **not** sun lighting itself. H = normalize(L+V) so patch tracks where view+sun produce vertical H | If unwanted: lower `Sun spec strength` (env[25].w) to 0; `Sun diffuse intens` (env[28].x) gives the parallel-ray sun. |

## Driver quirks (Aspyr K2 OpenGL wrapper)

Discovered during shadow attempt 2026-05. Driver appears to be Aspyr's GL-to-D3D translation layer (not native GL).

### `glCopyTexSubImage2D` rejects depth-component textures

Sequence that **fails**:
```c
// Frame 1
glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, w, h, 0,
             GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0,0, 0,0, w,h);  // err = 0 (OK!)
// Frame 2
glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0,0, 0,0, w,h);  // err = GL_INVALID_VALUE
```

First Sub succeeds. Every subsequent Sub fails with `GL_INVALID_VALUE` even with identical viewport. No combination of internal format (`GL_DEPTH_COMPONENT`, `_24`, `_24_STENCIL8`), GL type (`GL_FLOAT`, `GL_UNSIGNED_INT`), or allocation path (`TexImage2D` NULL data, `CopyTexImage2D`) makes Sub work past the first frame.

Replacement that **works**:
```c
glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, x,y, w,h, 0);  // every frame
```

Cost: 6 MB allocation/copy per snapshot at 1080p. With throttle (1/5 frames) FPS stays at 50+. Without throttle: 4 FPS.

### K2 issues many small `SwapBuffers` per real frame

Observed viewport sizes in our swap hook in a single second:
```
1920x1080 / 1920x1079  (main scene + HUD pass)
189x354    (inventory mini-render)
201x354
225x390
207x372
...
```

K2 renders sub-views (item portraits, equipment previews, dialogue cameos) by setting a small viewport, drawing, calling `SwapBuffers` (or its internal equivalent that flows through our IAT-hooked entry). Our depth snapshot must filter these out by checking viewport size against window-client size — otherwise allocation thrashes and FPS collapses.

### View matrix is in `program.env[91..93].w`

Aspyr packs the camera-to-world matrix's translation column (camera position in world space) into the W components of three consecutive env slots. Discovered while building the F2/Backspace sun-capture button.

```
camera.x = program.env[92].w
camera.y = program.env[91].w
camera.z = program.env[90].w
```

Used in `fp_*` as:
```
MOV camW.x, program.env[92].w;
MOV camW.y, program.env[91].w;
MOV camW.z, program.env[90].w;
```

The fixed-function `glLoadMatrixf` mirror in `gl_state_capture.cpp` is **never called** by K2. Real view matrix only available via `glGetFloatv(GL_MODELVIEW_MATRIX)` from inside our hooks, captured at first non-identity `glPushMatrix` per frame.

### `wglUseFontBitmapsA` is silently no-op

Aspyr wrapper accepts the call but produces no glyphs. Forced us to build the tune-overlay font via GDI → DIB → `glTexImage2D` texture atlas instead. See `pbr_tune.cpp` font code.

## What this unlocks (and what it doesn't)

With current capture:

- **Depth-based fog** (linear distance instead of vertex-interpolated `f.x`) — viable
- **Soft particles** (depth fade against scene) — viable
- **Static-geometry contact shadow** with mid-frame snapshot — viable (was working before SS sun-shadow attempt)

Tried and **cut**:

- **Screen-space sun shadow ray-march** — see SHADER_REFERENCE for the autopsy. Short version: K2's z-precision + 1-tap depth-diff fundamentally aliases scene depth growth as occlusion. 4-tap linear-z attempt blew up because `1/(1-z)` is exponential at the relevant z range. Throttling depth made ghost silhouettes follow moving NPCs.

Pending (would require additional engine work):

- **Proper SSAO** with view-space hemisphere → needs projection matrix push
- **Screen-space reflections** (ray-march in screen) → needs view matrix in shader
- **Depth linearization** with engine's actual near/far → near/far not in env params; could be probed via `glGet(GL_DEPTH_RANGE)` + projection inspection
