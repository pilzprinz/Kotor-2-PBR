/*
PBR pipeline state — shared across hooks.

Tracks: current loading texture filename, GL texture id ↔ filename mapping,
and lazy-loaded sibling cache (normalmap / roughness / metallic with AO/emissive
packed into their alpha channels).
*/

#ifndef PBR_STATE_H
#define PBR_STATE_H

#include "platform.h"
#include <string>
#include <map>

struct PbrParams
{
    float metallic;        // 0 = dielectric, 1 = metal. Negative sentinel = "use shader default" (env-shaders → 1, non-env → 0)
    float roughness;       // negative sentinel = "use stock d.a (inverse reflectivity)"
    float fresnelF0;       // base reflectance at normal incidence (unused by rim-Fresnel)
    float emissive;        // emissive intensity multiplier
    float normalStrength;  // scale on normal-map cube perturb + Lambert
    float cavityStrength;  // 0 = flat, 1 = full crevice darkening
    float fresnelRim;      // additive rim boost factor
    float reflectivity;    // user-facing env reflection scale (0=matte, 1=full)
    // txi `billboardshadow 1`: darken this material by raw sun-occlusion, UNgated
    // by N·sun. For steam/fog/foliage billboards whose normal faces the camera
    // (not the sun), so the normal-gated global-contrast shadow misses them.
    float billboardShadow; // 0 = off (normal walls), 1 = receive occlusion shadow ungated
    // txi `normalmap <stem>`: load Override/<stem>.tga as THIS material's normal
    // instead of the auto <diffuse>n.tga. Lets recolored variants share one normal
    // (e.g. 5 droid skins → one C_drdprot01n.tga). Empty = use the auto sibling.
    std::string normalMap;
};

struct SiblingSet
{
    GLuint normalId;       // TMU 8 — normal.rgb + AO.a (if aoPacked)
    GLuint roughId;        // TMU 9 — rough.r + emissive.a (if emiPacked)
    GLuint metalId;        // TMU 10 — metallic.r
    bool   aoPacked;       // AO is in normalId alpha
    bool   emiPacked;      // emissive is in roughId alpha
    bool   normalSynth;    // normalId is white-RGB placeholder (don't sample as normal)
    bool   roughSynth;     // roughId is white-R placeholder (don't sample as roughness)
    PbrParams params;
    unsigned lastUsed;     // LRU timestamp (frame counter) for cache eviction
};

void PbrInit();
void PbrShutdown();

// True while loading sibling textures — hooks skip PBR logic to avoid recursion.
extern bool g_pbrLoadingInProgress;

// Called from CreateFileA/W hook when an Override texture is being opened.
void PbrCaptureLoadingFilename(const char *path);

// Called from glTexImage2D hook — associates the currently bound GL texture id
// with the most recently loaded filename, then clears the TLS slot.
void PbrAssociateBoundTexture(GLuint glId);

// Called from glDeleteTextures hook — removes id→name binding so that
// engine-reused ids don't apply stale sibling sets from prior textures.
void PbrForgetTexture(GLuint glId);

// Resolve siblings for a bound texture id. Returns NULL if no name known.
// On first call for an id, looks up sibling files in Override\ and uploads them.
SiblingSet *PbrGetSiblings(GLuint diffId);

// Look up the friendly filename associated with a GL texture id. Returns
// NULL if no name was captured for this id.
const char *PbrGetTextureName(GLuint glId);

// Strip leading ".\override\" prefix and extension, return lowercased base name.
std::string PbrStripPath(const char *path);

// Push PBR params for current diffuse texture into program.env[20..23].
// Called after glBindTexture for diffuse-slot textures.
void PbrPushEnvParams(GLuint diffId);

// Push DEFAULT (stock-compatible) PBR params. Called when binding a texture
// with no PBR siblings, to prevent state leak from previous PBR-applied draw.
void PbrPushDefaultEnvParams();

#endif
