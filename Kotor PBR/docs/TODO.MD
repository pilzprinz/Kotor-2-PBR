# TODO: Полный современный PBR-пайплайн для KOTOR 2

## Уже реализовано (база)

- [x] Metallic/Roughness PBR workflow (Schlick Fresnel, energy conservation, AO, emissive)
- [x] Normal mapping (world-space blend, sibling `_n.tga`)
- [x] Sibling-текстуры (`_n`, `_R`, `_M`, `_O`, `_E`) + `.txi`/`.pbr` sidecar
- [x] Directional sun shadow map (2048², 2-tap PCF, global snap grid)
- [x] Self-shadow для моделей/персонажей (TMU5)
- [x] Envmap-рефлексии (cubemap) с Fresnel/roughness/light proxy
- [x] L0 Blinn-Phong specular (солнце)
- [x] Lightmap blending (K2 stock)
- [x] Fog (stock GL, pos/end)
- [x] Shadow receive на большинстве world/model FP
- [x] Cavity occlusion (AO из normal.a)
- [x] GLSL-порт инфраструктура (GlslProgram, uniform cache, Apply/End)
- [x] Tune/debug UI (F11, DEL-меню, per-shader sliders)
- [x] Hash-идентификация ARB шейдеров + override-подстановка
- [x] Sibling texture NPOT warning + fallback (POT required)

---

## Ближний приоритет — завершить текущее

### GLSL-порт персонажей (первая GLSL-пара)
- [x] Развернуть dynamic bone indexing в `vp_skinned_env_lit.vs.glsl` (17 костей через макрос)
- [x] Интегрировать `LoadEnvRefl()` в `glsl_program.cpp` (загрузка VS+FS, линковка, бинд сэмплеров)
- [x] Матчинг `fp_model_env_reflective` + `vp_skinned_env_lit` — вторая GLSL-программа
- [x] UploadStable/UploadPerDraw для второй программы (bone uniforms, viewInv, shadow, PBR params)
- [x] Замерить перф: `GlslMaterial_Apply` CPU-стоимость измеряется через QPC (`s_applyUs`/`s_applyDraw`), лог `[glsl] perf: %d GLSL draws/frame, avg %.1fus/draw` раз в 60 кадров в DiagLog

### GLSL-порт мировых шейдеров
- [x] `fp_worldtex_env_fog` → GLSL (есть как fp_worldtex_env_reflective.fs.glsl, MATCH_WORLD_ENV_REFL)
- [x] `fp_worldtex_lm_env` → GLSL (есть fp_worldtex_lm_env.fs.glsl, MATCH_WORLD_LM_ENV)
- [x] `fp_worldtex_lm_fog_alpha` → GLSL (есть как fp_worldtex_diffuse_main.fs.glsl, MATCH_DIFFUSE_MAIN)
- [x] `fp_model_env_fog` → GLSL (есть как fp_model_env_reflective.fs.glsl, MATCH_ENV_REFL)
- [x] `fp_model_bump_env_spec` → GLSL (есть fp_model_bump_env_spec.fs.glsl, MATCH_BUMP_ENV_SPEC)
- [x] `fp_worldtex_bump_env` → GLSL (bump walls, C++ интеграция сделана)
- [x] **Фикс потерянной глобальной альфы**: ARB пишет `MOV d.a, c[0].a` (env[0] = глобальная альфа), GLSL писал `d.a` (альфа диффузной текстуры = envmap-маска 0..1) → броня/доспехи полупрозрачные. Добавлен uniform `uEnv0` в 6 FS (armor, headgear, env_reflective, bump_env_spec, worldtex_env_reflective, worldtex_lm_env) + push FRAGMENT env[0] в UploadPerDraw*

### GLSL-порт вершинных шейдеров
- [x] `vp_static_env_fog` → GLSL (есть vp_static_env_fog.vs.glsl, MATCH_WORLD_ENV_REFL/WORLD_LM_ENV/DOOR/BUMP_ENV)
- [x] `vp_worldtex_env_fog_t2` → GLSL (создан vp_worldtex_env_fog_t2.vs.glsl, C++ матчинг добавлен — исправлен главный баг: engine биндит этот VP для fp_worldtex_env_fog, раньше GLSL world-материал не активировался)
- [x] `vp_static_lit_fog` → GLSL (есть как fp_worldtex_diffuse_main.vs.glsl, MATCH_DIFFUSE_MAIN)
- [x] `vp_skinned_env_lit` → GLSL (уже создан, bone unrolling на 17 костей, MATCH_ENV_REFL)
- [x] `vp_static_bump_env` → GLSL (C++ матчинг добавлен; использует vp_static_env_fog.vs.glsl — FS читает cube из tc2, консистентно)
- [x] `vp_skinned_bump_env` → GLSL (C++ матчинг для MATCH_BUMP_ENV_SPEC добавлен — исправлен баг: engine биндит этот VP для fp_model_bump_env_spec, раньше char bump GLSL не активировался)

### GLSL-порт пост-эффектов
- [ ] **Пост-порт ОТКАЧЕН (2026-08)** — лог `fp=38(fp_post_composite_top) ... count=49` показывает: движок биндит этот FP и для 3D-объектов и UI (не только полноэкранный квад). FP-only матчинг захватывал их → белая альфа в UI, «солнечный зайчик» на стенах (сэмпл старого фреймбуфера через UV 3D). Повторная попытка требует надёжного отличия настоящего пост-квада (напр. viewport == экран И !GL_BLEND И texcoord[0] покрывает [0,1]) — сделать тестовый лог id→(fp,vp,blend,viewport) в кастер-драв и проверить.
- [ ] `fp_post_grading` → GLSL (цветокоррекция — требует RECT + 3×1D сэмплеры)
- [ ] `fp_post_lut3d_sub` → GLSL (3D LUT)
- [ ] `fp_post_lut3x1d` → GLSL (3×1D LUT)
- [ ] `fp_post_overlay_add` → GLSL (UI/HUD overlay)
- [ ] `fp_hologram` → GLSL
- [ ] `fp_speedblur` → GLSL
- [ ] `fp_video_player` → GLSL

### GLSL-порт прочих шейдеров
- [ ] `fp_terrain_splat4` / `_b` → GLSL (terrain)
- [ ] `fp_fx_space_distort` → GLSL (FX)
- [ ] `fp_fx_surveillance` → GLSL (FX)
- [ ] `fp_fx_telepathy_screen` → GLSL (FX)
- [ ] `fp_ui_*` шейдеры → GLSL (UI)
- [ ] `fp_main_menu` → GLSL
- [x] `fp_door` → GLSL (есть fp_door.fs.glsl, MATCH_DOOR + vp_static_env_fog)
- [ ] `fp_ambient_overlay` / `_b` → GLSL

### Кодовая база
- [x] Убрать дублирование загрузчиков GLSL-программ (generic LinkProgram + таблицы сэмплеров kWld0/kWldBump/kSkEnv/kSkLt/kHeadgear/kDoor; ~200 строк копипасты удалено)
- [ ] Убрать дублирование ARB-кода в FP (вынести common.inc) — **ОЦЕНЕНО, НЕ ДЕЛАТЬ**: `shadow_receive.inc`/`shadow_receive_self.inc` вынесены и доказали себя (блок изолированный, фиксированный контракт регистров). Но PBR-material / normal-decode / env-composite блоки не изолированы — в каждом FP они переплетены с кодом (флаги fl.x/y/z управляют normal-decode, который потом переиспользуется L0-spec через N/V/fres), регистры/атакеры у FP разные. Вынос дал бы 0 выигрыша рантайма (ARB компилируется 1 раз, размер исходника не важен) и высокий риск поломать работающий рендер. Оставлено как есть; правки — точечно в каждый FP
- [ ] Кэш GLSL-программ (LRU) — не критично: программы линкуются один раз, работают весь сеанс
- [ ] GLSL error log → отдельный файл (`logs/glsl_errors.txt`) — GlslLinkProgram уже логирует в DiagLog, отдельный файл опционален
---

## Средний приоритет — продвинутое освещение и тени

### Shadow maps
- [ ] Cascaded Shadow Maps (CSM) — 2–3 каскада: ближний/средний/дальний
- [ ] Soft shadows: PCSS / VSM / ESM вместо жёсткого 2-tap PCF
- [ ] Shadow bias per-material (из `.txi`: `shadowBias`, `shadowStrength`)
- [ ] Contact shadows (screen-space raymarch) — восстановить depth capture, короткий raymarch в FP
- [ ] Shadow для point/spot источников L0–L7 (cubemap shadow maps или dual-paraboloid)
- [ ] Penumbra scaling по расстоянию от кастера
- [ ] Fade между CSM каскадами (dither blend, без видимого seam)
- [ ] Shadow map caching для static geometry (не перерисовывать каждый кадр)

### SSAO / HBAO
- [ ] Восстановить depth capture (отключён в `depth_capture.cpp`)
- [ ] Horizon-Based Ambient Occlusion (HBAO), full или half-res
- [ ] Bent normals из AO (для диффузного IBL)
- [ ] Temporal accumulation (reprojection между кадрами, шумодав)
- [ ] Bilateral blur (сохраняет края)
- [ ] AO intensity per-material (`aoStrength` в `.txi`)
- [ ] AO только для статического мира (skip characters)

### IBL (Image-Based Lighting)
- [ ] Diffuse irradiance map (convolved cubemap, pre-filtered Lambert)
- [ ] Specular pre-filtered envmap (mipmap roughness chain) — нужны mipmap на cubemap
- [ ] BRDF LUT (2D текстура: NdotV × roughness → 2 канала)
- [ ] Split-sum approximation (Karis 2013)
- [ ] IBL specular с stability (доминантный направленный свет для smooth surfaces, реализация в LUT)
- [ ] Убрать старый light proxy (luma clamp 0.25), заменить на proper IBL
- [ ] Local reflection probes (не только глобальный cubemap)
- [ ] Probe blending (2-3 ближайших probes по расстоянию)
- [ ] Parallax correction для box-projected cubemaps (AABB probes)

### Envmap-рефлексии
- [ ] Roughness-based mip выбор для cubemap (glossy reflections)
- [ ] Mipmap generation для env cubemap (ручной downsample с pre-filter)
- [ ] Screen-space reflections (SSR) как fallback при отсутствии cubemap coverage
---

## Средний приоритет — материалы и surface detail

### BRDF upgrade (microfacet)
- [ ] Перейти с L0 Blinn-Phong ^16 + Schlick на полный GGX/Trowbridge-Reitz NDF + Smith height-correlated visibility + Schlick Fresnel
- [ ] Multi-scattering correction (Kulla-Conty / Hammon approx) — металл без неё слишком тёмный
- [ ] Specular occlusion (Filament): затенение spec лучей по AO/height, не только diffuse
- [ ] Height-based micro-shadow для spec (ближняя самотень из height map)
- [ ] Fresnel F0 из `.txi` для диэлектриков (не только 0.04 fallback и металл-albedo)
- [ ] Sheen для тканей (cloth BRDF — по Chan/Burley), сейчас только грубый sheen slider
- [ ] Cotton/fabric specular (анизотропия + sheen вместе)
- [ ] Dragon curve / качественные normal-based IBL заменить штатный split-sum (см. IBL секцию)

### Parallax Occlusion Mapping (POM)
- [ ] Height map (`_h.tga`) sibling texture
- [ ] POM raymarch в FP (steep parallax / relief mapping)
- [ ] Self-shadowing для POM (дополнительный raymarch к источнику света)
- [ ] POM strength per-material (`pomStrength`, `pomSteps` в `.txi`)
- [ ] Ограничить POM по расстоянию (дальше N метров — flat normal mapping)
- [ ] POM для террейна (terrain splat shaders)

### Detail maps
- [ ] Macro-variation texture (`_detail.tga`) — тайловый, поверх diffuse
- [ ] Detail normal map (`_detail_n.tga`) — высокочастотные нормали
- [ ] UV scale/rotation (`detailUVScale`, `detailUVRotate` в `.txi`)
- [ ] Detail fade по расстоянию (mip-based)

### Clear coat
- [ ] Clear coat normal (второй слой normal map)
- [ ] Clear coat roughness (отдельный канал)
- [ ] Clear coat F0 (фиксированный 0.04 или из текстуры)
- [ ] Clear coat intensity per-material
- [ ] Применение: лакированное дерево, мокрая поверхность, car paint

### Anisotropic shading
- [ ] Anisotropy direction map (`_aniso.tga`: direction × strength)
- [ ] Anisotropic specular (Ward / Heitz-Ashikhmin)
- [ ] Применение: волосы (персонажи), brushed metal (дроиды, корабли)

### Subsurface scattering (SSS)
- [ ] Screen-space subsurface (fast SSS, Jimenez 2015)
- [ ] Thickness map (`_thickness.tga`)
- [ ] Translucency color (`transmittance` в `.txi`)
- [ ] Применение: кожа персонажей, уши, листья
- [ ] Separable SSS blur (два прохода: horizontal + vertical)

### Wetness / rain
- [ ] Wetness map (`_wet.tga`) или динамический wetness (rain триггер)
- [ ] Porosity per-material (скорость высыхания)
- [ ] Wetness: затемняет albedo, снижает roughness, добавляет clear coat
- [ ] Rain ripples normal map (tileable, animated)
- [ ] Puddles (planar reflections на плоских поверхностях)
- [ ] Drip maps (вертикальные потёки)

### Specular AA
- [ ] Toksvig / LEAN mapping для подавления specular aliasing
- [ ] Variance нормалей из normal map mip chain
- [ ] Применять к envmap reflections

### Alpha handling
- [ ] Dithered alpha (LOD-переходы, punch-through)
- [ ] Alpha-to-coverage (если поддерживается драйвером)
- [ ] Translucency (glass, holograms) — refractive + reflective
---

## Дальний приоритет — пост-обработка

### HDR pipeline
- [ ] Рендер в HDR rendertarget (RGBA16F или RGB10_A2, если драйвер поддерживает)
- [ ] Eye adaptation (auto-exposure) — histogram-based или running average
- [ ] ACES filmic tonemap (Narkowicz fit или полный ACES RRT)
- [ ] Exposure bias per-area (camera relative)
- [ ] HDR bloom input (до tonemap)
- [ ] Emission/light scales >1.0 (внимание: после tonemap диэлектрики раздуются — нужен правильно обрезанный яркий канал)

### Bloom
- [ ] Bright-pass threshold (изолировать яркие пиксели)
- [ ] Downsample chain (half → quarter → ... → 1/32)
- [ ] Gaussian blur по downsample chain
- [ ] Upsample + composite (additive blend)
- [ ] Anamorphic bloom (stretched horizontal/vertical, cinematic look)
- [ ] Dirt mask (lens dirt texture, модулирует bloom)
- [ ] Starburst filter (дифракционные лучи для ярких источников)

### Color grading
- [ ] 3D LUT (32×32×32 или 16×16×16, нейтральная + кастомные)
- [ ] Lift/Gamma/Gain (ASC CDL)
- [ ] Contrast, saturation, vibrance
- [ ] Channel mixer (R→G→B cross-talk)
- [ ] Shadows/Midtones/Highlights раздельный контроль
- [ ] LUT blending indoor/outdoor (интерполяция по времени)

### Motion blur
- [ ] Velocity buffer (motion vectors) — модифицировать VP для записи previous-frame позиции
- [ ] Camera motion blur (вращение/перемещение камеры)
- [ ] Per-object motion blur (анимированные персонажи/дроиды)
- [ ] Tile-based max velocity (Tile Max для оптимизации sample count)
- [ ] Sample count per-material (`motionBlurSamples` в `.txi`)

### Depth of Field
- [ ] Circle of Confusion (CoC) из глубины и фокусного расстояния
- [ ] Near/far раздельное размытие
- [ ] Bokeh shape (custom sprite: hex, oct)
- [ ] Autofocus (точка фокуса по центру экрана или nearest depth)
- [ ] DoF по дистанции (cinematic, как в диалогах)

### Temporal Anti-Aliasing (TAA)
- [ ] Motion vectors (см. Motion blur)
- [ ] Jitter матрицы проекции (Halton sequence)
- [ ] History buffer (reprojection, neighbourhood clamp)
- [ ] Variance-based colour clamping
- [ ] Sharpening pass после TAA (unsharp mask или CAS)
- [ ] TAA ослабляет specular aliasing (работает вместе с Toksvig)

### SMAA / FXAA
- [ ] SMAA 1x (edge detection + blending weights + neighbourhood blending)
- [ ] Или FXAA 3.11 как быстрый fallback
- [ ] Для игроков без TAA (слабые GPU)

### Film grain
- [ ] Фильм-грейн (temporal dithering, маскирует banding)
- [ ] Per-channel или luma-only
- [ ] Intensity per-scene (indoor/outdoor)
- [ ] Анимированный шум (меняется каждый кадр)

### Vignette
- [ ] Radial vignette (затемнение по краям кадра)
- [ ] Intensity, radius, softness
- [ ] Chromatic vignette (R/G/B раздельное смещение по краям)
- [ ] Включается в cinematic-сценах

### Chromatic aberration
- [ ] Lateral CA (разная дисторсия R/G/B от центра)
- [ ] Longitudinal CA (purple/green fringing в defocus)
- [ ] Intensity scaled by distance from center + depth

### Lens flares / glare
- [ ] Ghosting flares (дискретные отражения линз)
- [ ] Anamorphic streak
- [ ] Light source detection (яркие пиксели → screen-space позиция)
- [ ] Occlusion query (flare прячется за геометрией)

### Sharpen
- [ ] CAS (Contrast Adaptive Sharpening, AMD)
- [ ] Unsharp mask (быстрый fallback)
- [ ] Применять ПОСЛЕ TAA (компенсировать размытие)
---

## Дальний приоритет — атмосфера и окружение

### Volumetric fog
- [ ] Raymarch в текстуру (half-res, несколько шагов)
- [ ] Height fog (плотность меняется с высотой, экспоненциальный спад)
- [ ] Noise-модулированная плотность (3D noise, heterogeneous fog)
- [ ] God rays (crepuscular rays из позиции солнца)
- [ ] Shadowed volumetric (лучи солнца проходят через shadow map)
- [ ] Indoor volumetric (dust motes в лучах света)

### Sky / Atmosphere
- [ ] Precomputed atmospheric scattering (Bruneton — сложно для старого GL)
- [ ] Rayleigh/Mie scattering (аналитически, если перф позволяет)
- [ ] Dynamic time-of-day (солнце + небо меняются по времени)
- [ ] Night sky (звёзды, луна, ambient понижается, blue shift)
- [ ] Skybox HDR (если есть panorama)
- [ ] Skybox cubemap mip chain для IBL

### Weather system
- [ ] Rain: GPU particles, splashes, ripples, wetness
- [ ] Lightning: flash + directional light + звук
- [ ] Snow: GPU particles, accumulation map
- [ ] Sandstorm / ash: частицы + fog colour shift + ambient scatter
- [ ] Cloud shadows: движущиеся тени от облаков по террейну
- [ ] Wind: vegetation bend + particle direction

---

## Дальний приоритет — эффекты и анимация

### Vegetation
- [ ] Wind animation (vertex displacement через sinusoidal noise)
- [ ] Subsurface на листьях (translucency)
- [ ] Leaf cards normal map (двустороннее освещение)
- [ ] Grass LOD (fade + density reduction с расстоянием)

### Water / liquid surfaces
- [ ] Planar reflections (отдельный проход рендера для отражений)
- [ ] Refraction (смещение UV на основе normal map)
- [ ] Flow map (скорость и направление течения)
- [ ] Foam/edge detection (береговая пена по глубине)
- [ ] Caustics (animated tileable texture, проецируется в мир)
- [ ] Wave vertex displacement (Gerstner waves)
- [ ] Underwater post-effect: blur + colour shift при погружении камеры

### Particles
- [ ] Soft particles (depth-based fade, убирает жёсткий clipping)
- [ ] GPU particle simulation (transform feedback / compute — маловероятно в GL 1.x)
- [ ] Particle lighting (принимают свет от окружения)
- [ ] Particle shadows (cast на мир — дорого)
- [ ] Distortion particles (heat haze, смещение UV фона)
- [ ] Ribbon/trail particles (мечи, двигатели)

### Decals
- [ ] Deferred decals (если будет deferred pipeline)
- [ ] DBuffer decals (normals, roughness, metallic override)
- [ ] Bullet holes / blood splatter (dynamic decals)
- [ ] Scorch marks с normal map

### Dynamic lights
- [ ] Point light shadow maps (cubemap или dual-paraboloid)
- [ ] Spot light shadow maps
- [ ] Light cookies (текстура-маска для прожекторов)
- [ ] Area lights (эмуляция через multiple point samples)
- [ ] Light probes для динамических источников (Irradiance Volume)
---

## Инфраструктура и инструментарий

### Deferred rendering
- [ ] GBuffer layout: albedo.rgb + metal, normal.xy + rough, normal.z + AO, emissive, depth
- [ ] MRT (Multiple Render Targets) — нужна поддержка FBO + MRT в Aspyr wrapper
- [ ] Light culling (tile-based или cluster-based)
- [ ] Decals через DBuffer
- [ ] Задержка перехода на deferred зависит от ограничений драйвера

### Performance
- [ ] GPU profiler (timing queries: GL_EXT_timer_query)
- [ ] Per-pass GPU time logging (shadow caster, main render, post-effects)
- [ ] Draw call batching (instancing — unlikely в старом движке)
- [ ] Occlusion culling (software или hardware queries)
- [ ] LOD system для PBR материалов (дальние объекты — упрощённые материалы)
- [ ] Texture streaming (async upload если возможно)
- [ ] VRAM budget tracking

### Tools
- [ ] PBR material editor (внешний инструмент: preview model, настройка параметров)
- [ ] `.pbr` sidecar generator (сканирует текстуры, заполняет defaults, ручной оверрайд)
- [ ] Shader validator (проверяет все override-шейдеры на синтаксис и лимиты инструкций)
- [ ] In-game material inspector (прицел на объект → показывает PBR-параметры)
- [ ] Texture cache analyser (загруженные текстуры, разрешение, формат)
- [ ] Auto `.txi` из `.pbr` (синхронизация параметров)

### Quality settings
- [ ] Low / Medium / High / Ultra пресеты
- [ ] Per-effect toggle (SSAO, shadow resolution, reflection quality)
- [ ] Resolution scale (0.5x – 2.0x рендертаргет)
- [ ] Ini-файл конфигурации (`pbr_config.ini` уже есть, расширить секциями)
- [ ] Сохранение настроек между сессиями
- [ ] Hot-reload шейдеров (перекомпиляция без перезагрузки игры)

### Platform / compatibility
- [ ] Linux support (Wine/Proton — проверить GL extensions и поведение wrapper)
- [ ] macOS support (Crossover, если Aspyr wrapper работает)
- [ ] Intel GPU compatibility (ограничения драйвера, fallback для слабых GPU)
- [ ] AMD GPU testing (precision, extensions, mipmap handling — отличия от NVIDIA)
- [ ] Отчёты о совместимости от пользователей

---

## Документация и community

- [ ] Руководство по созданию PBR-текстур (для моддеров)
- [ ] Спецификация `.pbr` sidecar формата (JSON-схема)
- [ ] Примеры PBR-текстур для типовых материалов (metal, wood, stone, fabric, skin)
- [ ] Changelog
- [ ] Credits (ShaderOverride, ReShade, MD5, etc.)
- [ ] FAQ / troubleshooting (чёрный экран, фиолетовые текстуры, краши)
- [ ] Инструкция по сборке DLL из исходников (MinGW, w64devkit)


