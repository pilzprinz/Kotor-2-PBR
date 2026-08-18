/*
Minimal TGA loader for PBR sibling textures.

Supports:
- Uncompressed 24/32-bit RGB(A)
- Top-left and bottom-left origin (per TGA spec)
- RLE-compressed 24/32-bit RGB(A) (modders may ship compressed)
- Greyscale 8-bit (for roughness/metallic/AO maps)

Does NOT support:
- 16-bit color (rare)
- Color-mapped (paletted) TGA
- TPC format (need separate loader)
*/

#ifndef TGA_LOADER_H
#define TGA_LOADER_H

#include "platform.h"

struct TgaImage
{
    int width;
    int height;
    int channels;       // 1 (grey), 3 (RGB), or 4 (RGBA)
    unsigned char *data;
    GLenum glFormat;    // GL_RED / GL_RGB / GL_RGBA
    GLenum glInternal;  // GL_R8 / GL_RGB8 / GL_RGBA8
};

// Load TGA from file. Returns true on success.
// Caller must call TgaFree on result.
bool TgaLoad(const char *path, TgaImage *out);

void TgaFree(TgaImage *img);

// Upload TGA to fresh GL texture, return id. 0 on failure.
GLuint TgaUploadAsTexture(const TgaImage *img);

#endif
