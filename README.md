# Kotor 2 PBR

Physically Based Rendering pipeline for **Star Wars: Knights of the Old Republic II — The Sith Lords**.

Modernizes the legacy OpenGL 1.2 / ARB assembly rendering path of the KOTOR 2 engine: adds an `opengl32.dll` proxy, shader dump/override capability, file access logging, real-time shadow maps and an experimental GLSL pilot for PBR materials.

## Repository layout

```
Kotor 2 PBR/
├── Kotor PBR/              ← main development
│   ├── README.md               project readme (status, install, folder layout)
│   ├── CHANGELOG.md            session-by-session change log
│   ├── docs/                   INSTALL / SHADER_REFERENCE / PBR_TEXTURE_FORMAT /
│   │                           PBR_PIPELINE / TROUBLESHOOTING / SHADOW_MAP / ARCHITECTURE
│   ├── source/                 buildable C++ source (see source/BUILD.md)
│   ├── deploy/                 copy-to-game-folder output
│   └── tools/                  validation / dev tools
└── ShaderOverride-master/   ← vendored base for the proxy hooks
    (HappyFunTimes ShaderOverride 2016 + gShaderReplacer + RSA MD5)
```

## Status

🚧 **Work in progress** — diagnostic / dev preview.

Current state:
- ✅ Shader identification (~35 of 59 stock shaders identified by role)
- ✅ `opengl32.dll` proxy with shader override capability
- ✅ File access logger for texture loading diagnostic
- ✅ Real-time shadow map subsystem (constellation: static geometry cache, alpha-tested cutout casters, live dynamic casters)
- 🚧 PBR texture sibling loader
- 🚧 PBR ARB fragment programs / GLSL pilot

## Build

Requires a 32-bit MinGW-w64 toolchain (`i686-w64-mingw32-g++`). KOTOR 2 is a 32-bit game, the `opengl32.dll` proxy must be 32-bit.

```sh
cd "Kotor PBR/source"
mingw32-make
```

See [`Kotor PBR/source/BUILD.md`](Kotor PBR/source/BUILD.md) and the Makefile for toolchain details.

## Install (diagnostic preview)

1. Backup `<game>/opengl32.dll` (if present)
2. Copy `Kotor PBR/deploy/opengl32.dll` → `<game>/`
3. Copy `Kotor PBR/deploy/shader_ident.txt` → `<game>/`
4. Optionally apply [3C-FD-Patcher](https://github.com/J0-o/3C-FD-Patcher) first (fog/reflection fix)
5. Run the game

Full walkthrough: [`Kotor PBR/README.md`](Kotor PBR/README.md).

## License

MIT (inherited from ShaderOverride / gShaderReplacer / RSA MD5). See `Kotor PBR/LICENSE_*.txt` and `ShaderOverride-master/LICENSE_*.txt`.

## Credits

- **HappyFunTimes** — ShaderOverride 2016
- **psycholns** — gShaderReplacer 2015
- **J0-o** — 3C-FD-Patcher (fog/reflection fix)
- **Frank Thilo & RSA Data Security** — MD5 reference