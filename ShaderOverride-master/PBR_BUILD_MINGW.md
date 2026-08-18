# Build with MinGW-w64 (no Visual Studio)

## Install w64devkit (portable, ~80 MB)

1. Download: https://github.com/skeeto/w64devkit/releases — latest `w64devkit-x.y.z.zip` (x64 host, 32-bit + 64-bit cross-targets)
2. Extract to `C:\w64devkit\` (or anywhere)
3. Add `C:\w64devkit\bin\` to PATH:
   - Windows Settings → System → About → Advanced system settings → Environment Variables → Path → Edit → New → `C:\w64devkit\bin\`
4. Verify in new terminal: `g++ --version` should show MinGW

w64devkit's `g++.exe` defaults to 64-bit. KOTOR2 is **32-bit** → need `-m32` flag. Already in Makefile.

Alternative — install 32-bit cross-compiler explicitly via MSYS2:
- https://www.msys2.org/
- `pacman -S mingw-w64-i686-gcc`

## Build

In VS Code terminal (or any cmd/PowerShell) at project root:

```
cd "d:/Documents/!Programs/Kotor 2 PBR/ShaderOverride-master"
mingw32-make
```

OR press `Ctrl+Shift+B` in VS Code (uses `.vscode/tasks.json`).

Expected output:
```
i686-w64-mingw32-g++ -O2 -Wall -m32 ... opengl32.cpp -o opengl32.o
... (all .cpp compiled)
i686-w64-mingw32-g++ ... -o opengl32.dll opengl32.o glFunctions.o md5.o iat_hook.o file_logger.o opengl32.def
```

Result: `opengl32.dll` in project root.

## If w64devkit uses generic g++ (not i686-w64-mingw32-g++)

w64devkit bundles `gcc.exe` / `g++.exe` (no prefix). Override Make variable:
```
mingw32-make CXX=g++ CC=gcc
```
The `-m32` flag in CXXFLAGS forces 32-bit output.

## If make says "no rule" / errors

Common fixes:
- `mingw32-make` vs `make` — w64devkit ships `mingw32-make.exe`. Use that.
- `cl.exe not found` — wrong toolchain in PATH. Make sure w64devkit\bin first.
- 64-bit link error — Makefile already passes -m32. Check toolchain supports it: `g++ -m32 -v`.

## Install built DLL

1. Backup `<game>/opengl32.dll`
2. Copy new `opengl32.dll` to `<game>/`
3. Run game

If game crashes immediately — bad DLL. Restore backup. Likely toolchain produced 64-bit by mistake. Verify:
```
file opengl32.dll
```
Should say `PE32 executable (DLL) (console) Intel 80386` (32-bit), NOT `PE32+` (64-bit).
