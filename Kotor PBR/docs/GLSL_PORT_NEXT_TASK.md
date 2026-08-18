# Промт для следующего таска (новый чат, без контекста)

Скопируй текст ниже в новый чат.

---

## Задача: завершить GLSL-порты персонажей в KOTOR 2 PBR

### Контекст проекта

Проект `d:/Documents/!Programs/Kotor 2 PBR/Kotor PBR` — мод PBR для Star Wars: Knights of the Old Republic II. Создаёт прокси `opengl32.dll` (32-bit, MinGW-w64 i686), которая перехватывает вызовы OpenGL движка, подменяет оригинальные ARB-шейдеры на улучшенные (ARB и GLSL). Движок использует OpenGL 1.x fixed-function + ARB vertex/fragment programs. Aspyr GL wrapper (старый, GL 4.6 на NVIDIA RTX 3070, но wrapper ограничивает: `glActiveTexture(TEXTURE11+)` отклоняется, `wglUseFontBitmaps` no-op).

Ключевые правила (из `.clinerules/AGENTS.md`):
- Приоритет: data flow > math. Трассируй CPU → VS → varyings → FS.
- Предполагай ошибки портирования первыми (missing varying, wrong TEXCOORD, wrong matrix space, lost fog, lost vertex color).
- Визуальный баг важнее текстового сходства кода.
- Для портов сравнивай: inputs, outputs, varyings, coordinate spaces, uniforms, register mappings, texture units, fog, alpha.
- GLSL семантика ≠ ARB семантика. Проверяй `fragment.color.primary/secondary`, `vertex.color`, TEXCOORD каналы, `program.env` регистры.

### Что уже сделано (предыдущие сессии)

1. **Краш игры исправлен.** VEH (Vectored Exception Handler) перехватывал SEH Aspyr wrapper — игра падала после заставок. Убран из `DllMain`. `PbrShutdown()` убран из `DLL_PROCESS_DETACH` (CRT heap мёртв).
2. **Тёмные персонажи исправлены.** `fp_model_armor_legacy.txt` + `fp_model_headgear_legacy.txt` имели darken с гейтом NdotL — тёмная сторона темнела в 2 раза. Убран NdotL из гейта, теперь только реальный каст shadow map.
3. **Рефакторинг:** `pbr_config.h/cpp` (INI конфиг), `glFunctions.cpp` (`operator[]`→`find()`), `depth_capture.cpp` (`GetFbo()`→`find()`), `opengl32.cpp` (безопасный `InitGL()`), `pbr_state.cpp/h` (LRU eviction), `pbr_tune.cpp/h` (FPS + draw calls, F11 toggle), `Makefile` (добавлен `pbr_config.cpp`).
4. **Существующий GLSL порт:** `deploy/shaders_override/fp_worldtex_diffuse_main.{vs,fs}.glsl` — порт пары (vp_static_lit_fog + fp_worldtex_diffuse_main), работает через `glsl_program.cpp` (GlslMaterial_Apply/End, UploadStable/UploadPerDraw, кэш uniform locations). Включён тумблером "GLSL material" (env[26].w >= 0.5).

### Текущий статус GLSL-портов персонажей (не завершено)

**Созданы файлы (готовы, но НЕ интегрированы):**
- `deploy/shaders_override/vp_skinned_env_lit.vs.glsl` — порт ARB `vp_skinned_env_lit.txt` (skinned персонажи/объекты с envmap). 4-bone палитра (env[18..68]), viewInv (env[92..89]), 3-light LIT, world normal/pos → tc4/tc5, cube reflect → tc1, fog → secondary.
- `deploy/shaders_override/fp_model_env_reflective.fs.glsl` — порт ARB `fp_model_env_reflective.txt` (бывший fp_model_env_fog) + `shadow_receive_self.inc`. PBR (normal/rough/metal + AO + emissive + fresnel + L0 spec), shadow_receive_self (env[104..106] K, TMU5 SELF map, 4-tap PCF + IGN dither), sun diffuse + global contrast, env composite, fog.

**Блокер (решить ПЕРВЫМ):**
`vp_skinned_env_lit.vs.glsl` использует dynamic indexing uniform массива:
```glsl
int i0 = int(boneIdx.x);
vec4 p0 = vec4(dot(gl_Vertex, uBone[i0*3+0]), ...);
```
GLSL 1.20 НЕ поддерживает dynamic indexing uniform arrays на многих драйверах. ARB использует ARL (аппаратный адресный регистр). Варианты:
- **Unroll 17 костей** через макрос/функцию: `mat4 getBone(int i)` с 17 `if` ветками (uniform index — константа в каждой ветке, компилируется). Это предпочтительно — сохраняет GLSL 1.20 совместимость со старым wrapper.
- `#version 130` — dynamic indexing разрешён, но теряется attribute/varying совместимость (нужны `in/out`, не `attribute/varying`). Рискованно на старом wrapper.

**Не завершено в `source/glsl_program.cpp`:**
- Добавлены только статические `s_progEnvRefl`/`s_triedEnvRefl` (строка ~265).
- Нужно: загрузчик второй программы (аналог `LoadDiffuseMain`), матчинг в `GlslMaterial_Apply` на пару (vp_skinned_env_lit + fp_model_env_reflective), UploadStable/UploadPerDraw для новых uniforms (uBone[51], uBoneCfg, uKS0..2 для self shadow, texEnv sampler, uFogParams для skinned VS).

### Что нужно сделать

1. **Решить dynamic indexing** в `vp_skinned_env_lit.vs.glsl` (unroll 17 костей через макрос).
2. **Завершить интеграцию** в `glsl_program.cpp`:
   - Загрузчик `LoadEnvRefl()` — читает `shaders_override/fp_model_env_reflective.{vs,fs}.glsl`, линкует, биндит сэмплеры (tex0=0, texEnv=1, texNrm=8, texRgh=9, texMtl=10, texShadow=5 — ВАЖНО: SELF map TMU5, не TMU6), кэширует uniform locations.
   - Матчинг в `GlslMaterial_Apply`: если FP = "fp_model_env_reflective" и VP = "vp_skinned_env_lit" → использовать вторую программу. Существующий матчинг на diffuse_main оставить.
   - UploadStable для второй программы: viewInv (env[92..89] VERTEX), tune sliders (env[24..30] FRAGMENT), pcf (env[32]), self shadow K (env[104..106] FRAGMENT), camW (env[92..90].w FRAGMENT).
   - UploadPerDraw для второй программы: 3 vertex lights (env[87..73] VERTEX), ambient/scales (env[93..95]), PBR params (env[20..23] FRAGMENT), L0 (env[87..86] FRAGMENT), fog color (glGetFloatv GL_FOG_COLOR), fog params (glGetFloatv GL_FOG_START/END → fb2/fb3).
   - **Bone uniforms:** uBone[51] = env[18..68] VERTEX, uBoneCfg = env[16] VERTEX. Читать через pglGetEnvFv в цикле.
3. **Собрать** через w64devkit:
   ```
   set PATH=C:\Program Files (x86)\w64devkit\bin;%PATH%
   cd "d:\Documents\!Programs\Kotor 2 PBR\Kotor PBR\source"
   mingw32-make clean && mingw32-make
   ```
   Результат: `../deploy/opengl32.dll`.
4. **Проверить** в игре: включить "GLSL material" тумблер (DEL → GLSL material → 1), сравнить персонажей/дроидов с ARB (тумблер 0). Проверить: скининг (кости), освещение, тени, envmap, туман, альфа.

### Ключевые файлы

- `source/glsl_program.cpp` — GLSL инфраструктура (probe, link, Apply/End, UploadStable/PerDraw). Главный файл для интеграции.
- `source/glsl_program.h` — API.
- `source/pbr_tune.cpp` — draw loop вызывает GlslMaterial_Apply/End (my_glDrawElements/Arrays).
- `deploy/shaders_override/vp_skinned_env_lit.txt` — оригинальный ARB VP для порта.
- `deploy/shaders_override/fp_model_env_reflective.txt` — оригинальный ARB FP для порта.
- `deploy/shaders_override/shadow_receive_self.inc` — блок тени (TMU5, env[104..106]).
- `deploy/shaders_override/vp_skinned_env_lit.vs.glsl` — созданный порт VS (нужен фикс dynamic indexing).
- `deploy/shaders_override/fp_model_env_reflective.fs.glsl` — созданный порт FS.
- `deploy/shaders_override/fp_worldtex_diffuse_main.{vs,fs}.glsl` — рабочий пример порта (образец стиля).
- `source/glFunctions.cpp` — GetProgramName (id→имя для матчинга).
- `source/shadow_map.cpp` — BindShadowMaps (TMU5/TMU6), env[100..106] push.

### Важные детали

- **TMU5 = SELF shadow map** (модели/персонажи), TMU6 = COMPLETE map (мир). `shadow_receive_self.inc` сэмплит texture[5] + env[104..106]. НЕ перепутай с TMU6.
- **Weights/bone indices:** ARB читает weights из `vertex.attrib[1]`, bone indices из `vertex.attrib[4]`. В GLSL 1.20 compat: attrib[1] → `gl_SecondaryColor`, attrib[4] → `gl_MultiTexCoord4`. Проверь это в существующем коде движка.
- **Fog:** ARB VP пишет `result.color.secondary.x = (|eye| - start) * (1/span)`. GLSL VS должен писать `gl_FrontSecondaryColor.x` так же. FS читает `gl_SecondaryColor.r`.
- **Alpha:** сохраняй `d.a` (punchthrough).
- **GLSL 1.20** — используй `attribute/varying/texture2D/gl_FragColor`, не `in/out/texture()`.
- **Сборка:** MinGW-w64 i686 (w64devkit), `-m32 -std=c++11`.

### Критерии готовности

- VS компилируется (нет dynamic indexing ошибок).
- Обе программы линкуются (лог `[glsl] material fp_model_env_reflective linked id=N`).
- В игре: персонажи/дроиды с GLSL тумблером выглядят идентично ARB (или лучше), скининг работает, тени/туман/альфа корректны.
- Нет краша, нет чёрных/фиолетовых поверхностей.