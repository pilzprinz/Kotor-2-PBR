# Kotor 2 PBR

Physically Based Rendering pipeline for **Star Wars: Knights of the Old Republic II — The Sith Lords**.

Based on [ShaderOverride](https://github.com/) (HappyFunTimes 2016) and inspired by [3C-FD-Patcher](https://github.com/J0-o/3C-FD-Patcher) (J0-o 2025).

## Status

🚧 **Work in progress** — diagnostic / dev preview.

Current state:
- ✅ Shader identification (~35 of 59 stock shaders identified by role)
- ✅ `opengl32.dll` proxy with shader override capability (inherited from ShaderOverride)
- ✅ File access logger for texture loading diagnostic
- 🚧 Texture binding hook (step A2)
- 🚧 PBR sibling texture loader (step A3)
- 🚧 PBR ARB fragment programs (step A5)

## Install (diagnostic preview)

1. Backup `<game>/opengl32.dll` (if present)
2. Copy `deploy/opengl32.dll` → `<game>/`
3. Copy `deploy/shader_ident.txt` → `<game>/`
4. Optionally apply `3C-FD-Patcher` first if not already (fog/reflection fix)
5. Run game

DLL acts as `opengl32.dll` proxy — game loads it instead of system one, intercepts shader calls and file accesses.

## Prerequisites

- **3C-FD-Patcher applied** (recommended) — fixes Aspyr update breakage of fog/envmap. Without it, shader hashes won't match.
- KOTOR 2 (any version — Steam/GOG/retail, M4-78 compatible)

## What this gives you (now)

- **Shader dump** — every ARB shader the game loads is dumped to `shaders_original/` in game folder
- **Shader override** — drop modified ARB shaders into `shaders_override/`, engine uses them
- **File access log** — `pbr_file_log.txt` lists every file the engine opens (Override TGAs, BIFs, ERFs)

## What this will give you (later)

- Custom PBR-aware fragment programs
- Automatic loading of PBR texture maps from Override (roughness/metallic/AO/normal) via naming convention
- Sidecar `.pbr` metadata files for fine control

## Folder layout

```
Kotor PBR/
├── README.md                 ← you are here
├── deploy/                   ← copy to game folder
│   ├── opengl32.dll
│   ├── shader_ident.txt
│   └── shaders_override/     ← put custom ARB shaders here
├── docs/                     ← documentation
│   ├── INSTALL.md
│   ├── SHADER_REFERENCE.md       — stock shader catalog + override status
│   ├── PBR_TEXTURE_FORMAT.md     — sibling suffixes, sidecar keys, defaults
│   ├── PBR_PIPELINE.md           — shader math, env param layout, packing
│   ├── TROUBLESHOOTING.md        — build, gotchas, diagnostics
│   ├── DEPTH_CAPTURE.md
│   └── ARCHITECTURE.md
├── source/                   ← buildable C++ source
│   └── BUILD.md
└── tools/                    ← validation/dev tools
```

## License

MIT (inherited from ShaderOverride / gShaderReplacer / RSA MD5). See `LICENSE_*.txt`.

## Credits

- **HappyFunTimes** — ShaderOverride 2016
- **psycholns** — gShaderReplacer 2015
- **J0-o** — 3C-FD-Patcher (fog/reflection fix)
- **Frank Thilo & RSA Data Security** — MD5 reference
