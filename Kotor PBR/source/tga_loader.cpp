/*
TGA loader — minimal subset for KOTOR PBR pipeline.
Supports: uncompressed 24/32-bit BGR(A) and 8-bit grey, RLE variants of same.
*/

#include "tga_loader.h"
#include "file_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
struct TgaHeader
{
    unsigned char  idLength;
    unsigned char  colorMapType;
    unsigned char  imageType;     // 2=RGB, 3=grey, 10=RLE RGB, 11=RLE grey
    unsigned short colorMapStart;
    unsigned short colorMapLen;
    unsigned char  colorMapDepth;
    unsigned short xOrigin;
    unsigned short yOrigin;
    unsigned short width;
    unsigned short height;
    unsigned char  bitsPerPixel;
    unsigned char  descriptor;
};
#pragma pack(pop)

// === Constants ===
static const unsigned char TGA_TYPE_RGB         = 2;
static const unsigned char TGA_TYPE_GREY        = 3;
static const unsigned char TGA_TYPE_RGB_RLE     = 10;
static const unsigned char TGA_TYPE_GREY_RLE    = 11;
static const unsigned char TGA_DESCRIPTOR_TOP   = 0x20; // origin at top-left when set

// === RLE decoder ===
static bool DecodeRLE(FILE *fp, unsigned char *out, int pixels, int bpp)
{
    int written = 0;
    while (written < pixels)
    {
        unsigned char header;
        if (fread(&header, 1, 1, fp) != 1) return false;
        int count = (header & 0x7F) + 1;
        bool runPacket = (header & 0x80) != 0;

        if (runPacket)
        {
            unsigned char pixel[4];
            if (fread(pixel, 1, bpp, fp) != (size_t)bpp) return false;
            for (int i = 0; i < count && written < pixels; ++i)
            {
                memcpy(out + written * bpp, pixel, bpp);
                ++written;
            }
        }
        else
        {
            int bytes = count * bpp;
            if (fread(out + written * bpp, 1, bytes, fp) != (size_t)bytes) return false;
            written += count;
        }
    }
    return true;
}

// === Pixel post-processing ===
static void SwapBGRtoRGB(unsigned char *data, int pixels, int bpp)
{
    for (int i = 0; i < pixels; ++i)
    {
        unsigned char *p = data + i * bpp;
        unsigned char t = p[0]; p[0] = p[2]; p[2] = t;
    }
}

static void FlipVertical(unsigned char *data, int width, int height, int bpp)
{
    int rowBytes = width * bpp;
    unsigned char *tmp = (unsigned char *)malloc(rowBytes);
    if (!tmp) return;
    for (int y = 0; y < height / 2; ++y)
    {
        unsigned char *rowA = data + y * rowBytes;
        unsigned char *rowB = data + (height - 1 - y) * rowBytes;
        memcpy(tmp, rowA, rowBytes);
        memcpy(rowA, rowB, rowBytes);
        memcpy(rowB, tmp, rowBytes);
    }
    free(tmp);
}

// === Public API ===
bool TgaLoad(const char *path, TgaImage *out)
{
    if (!path || !out) return false;
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    TgaHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) { fclose(fp); return false; }

    // Skip image-ID field and (unsupported) color map.
    if (hdr.idLength > 0) fseek(fp, hdr.idLength, SEEK_CUR);
    if (hdr.colorMapType != 0)
        fseek(fp, hdr.colorMapLen * (hdr.colorMapDepth / 8), SEEK_CUR);

    int bpp = hdr.bitsPerPixel / 8;
    if (bpp != 1 && bpp != 3 && bpp != 4) { fclose(fp); return false; }

    // Bounds + overflow guard (max texture ~8192×8192×4 = 256 MB).
    if (hdr.width <= 0 || hdr.height <= 0 || hdr.width > 8192 || hdr.height > 8192)
    { fclose(fp); return false; }

    int pixels = hdr.width * hdr.height;
    int dataSize = pixels * bpp;
    unsigned char *data = (unsigned char *)malloc(dataSize);
    if (!data) { fclose(fp); return false; }

    bool ok = false;
    switch (hdr.imageType)
    {
        case TGA_TYPE_RGB:
        case TGA_TYPE_GREY:
            ok = (fread(data, 1, dataSize, fp) == (size_t)dataSize);
            break;
        case TGA_TYPE_RGB_RLE:
        case TGA_TYPE_GREY_RLE:
            ok = DecodeRLE(fp, data, pixels, bpp);
            break;
    }
    fclose(fp);

    if (!ok) { free(data); return false; }

    // TGA stores color as BGR(A); OpenGL expects RGB(A).
    if (bpp == 3 || bpp == 4)
        SwapBGRtoRGB(data, pixels, bpp);

    // Normalize to KOTOR engine convention (bottom-up in GL memory, where
    // row 0 = top of image — matches engine's TPC diffuse uploads).
    // Flip when source is top-left (descriptor bit 5 set) so result ends up
    // in same orientation as engine-loaded diffuse, keeping sibling UVs aligned.
    if (hdr.descriptor & TGA_DESCRIPTOR_TOP)
        FlipVertical(data, hdr.width, hdr.height, bpp);

    out->width    = hdr.width;
    out->height   = hdr.height;
    out->channels = bpp;
    out->data     = data;
    switch (bpp)
    {
        case 1: out->glFormat = GL_LUMINANCE; out->glInternal = GL_LUMINANCE8; break;
        case 3: out->glFormat = GL_RGB;       out->glInternal = GL_RGB8;       break;
        case 4: out->glFormat = GL_RGBA;      out->glInternal = GL_RGBA8;      break;
    }
    return true;
}

void TgaFree(TgaImage *img)
{
    if (!img || !img->data) return;
    free(img->data);
    img->data = NULL;
}

GLuint TgaUploadAsTexture(const TgaImage *img)
{
    if (!img || !img->data) return 0;

    GLuint id = 0;
    glGenTextures(1, &id);
    if (!id) return 0;

    glBindTexture(GL_TEXTURE_2D, id);

    // Power-of-2 check. Non-power-of-2 siblings can't form a complete mip chain on
    // the Aspyr/K2 driver, so a minified normal map samples its base texel grid →
    // a fine moiré "mesh" on normal-mapped surfaces, revealed by directional sun.
    // POT: trilinear mips (anti-aliased). NPOT: plain LINEAR (defined, but still
    // aliases under minification — re-export the texture at a power-of-2 size).
    int w = img->width, h = img->height;
    bool isPOT = w > 0 && h > 0 && (w & (w - 1)) == 0 && (h & (h - 1)) == 0;
    if (!isPOT) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "[pbr] WARNING: sibling texture %dx%d is non-power-of-2 -> no mipmaps -> aliasing 'mesh' on normal-mapped surfaces. Re-export at POT.\n",
                 w, h);
        PbrLogLine(msg);
    }

    // Auto-generate mipmaps on upload (deprecated in GL 3.1 but supported on K2-era drivers).
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP,    isPOT ? GL_TRUE : GL_FALSE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, isPOT ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);

    glTexImage2D(GL_TEXTURE_2D, 0, img->glInternal,
                 img->width, img->height, 0,
                 img->glFormat, GL_UNSIGNED_BYTE, img->data);
    return id;
}
