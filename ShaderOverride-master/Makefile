# Makefile for ShaderOverride PBR diagnostic build (MinGW-w64)
#
# Usage:
#   mingw32-make           # build opengl32.dll
#   mingw32-make clean
#
# Requires: 32-bit MinGW-w64 (i686-w64-mingw32 toolchain).
# KOTOR2 is 32-bit, opengl32.dll proxy MUST be 32-bit.

# Try common MinGW 32-bit toolchain names.
CXX ?= i686-w64-mingw32-g++
CC  ?= i686-w64-mingw32-gcc

# If above missing, allow override: mingw32-make CXX=g++ -m32
# (with w64devkit's default g++, pass -m32 explicitly)

CXXFLAGS = -O2 -Wall -m32 -std=c++11 -DWIN32 -D_WINDOWS -D_USRDLL
LDFLAGS  = -shared -m32 -static -static-libgcc -static-libstdc++ \
           -Wl,--enable-stdcall-fixup -lopengl32 -lkernel32 -lgdi32

SOURCES = opengl32.cpp glFunctions.cpp md5.cpp iat_hook.cpp file_logger.cpp
OBJECTS = $(SOURCES:.cpp=.o)
DEFFILE = opengl32.def
TARGET  = opengl32.dll

all: $(TARGET)

$(TARGET): $(OBJECTS) $(DEFFILE)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(DEFFILE)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	-rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
