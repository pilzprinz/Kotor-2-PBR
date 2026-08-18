/*
PBR state — texture id/name tracking + sibling loading + env-param push.
KOTOR uses single OpenGL thread, so plain globals are safe.
*/

#include "pbr_state.h"
#include "pbr_tune.h"
#include "pbr_config.h"
#include "tga_loader.h"
#include "glFunctions.h"
#include "file_logger.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <algorithm>

// === Constants ===
static const char *kOverrideDir = "Override\\";

// === Globals ===
static std::string g_currentLoadingFilename;
static std::map<GLuint, std::string> g_texIdToName;
static std::map<std::string, SiblingSet> g_siblingCache;

bool g_pbrLoadingInProgress = false;

// RAII guard for the re-entry flag — auto-clears on scope exit.
struct LoadingGuard
{
    LoadingGuard()  { g_pbrLoadingInProgress = true;  }
    ~LoadingGuard() { g_pbrLoadingInProgress = false; }
};

// === Small helpers ===
static std::string ToLowerCopy(const std::string &s)
{
    std::string r = s;
    for (size_t i = 0; i < r.length(); ++i)
        r[i] = (char)tolower((unsigned char)r[i]);
    return r;
}

static void TrimWs(std::string &s)
{
    const char *ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    size_t e = s.find_last_not_of(ws);
    if (b == std::string::npos) { s.clear(); return; }
    s = s.substr(b, e - b + 1);
}

// === Lifecycle ===
void PbrInit()
{
    g_currentLoadingFilename.clear();
    g_texIdToName.clear();
    g_siblingCache.clear();
}

void PbrShutdown()
{
    g_currentLoadingFilename.clear();
    g_texIdToName.clear();
    g_siblingCache.clear();
}

// === Path normalization ===
std::string PbrStripPath(const char *path)
{
    if (!path) return std::string();
    std::string s = ToLowerCopy(path);

    // Strip leading override-folder prefix.
    const char *prefixes[] = { ".\\override\\", "./override/", "override\\", "override/", NULL };
    for (int i = 0; prefixes[i]; ++i)
    {
        size_t plen = strlen(prefixes[i]);
        if (s.length() > plen && s.compare(0, plen, prefixes[i]) == 0)
            { s = s.substr(plen); break; }
    }

    // Strip extension.
    size_t dot = s.find_last_of('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    return s;
}

static bool IsTextureExt(const std::string &lowerExt)
{
    return lowerExt == ".tga" || lowerExt == ".tpc";
}

void PbrCaptureLoadingFilename(const char *path)
{
    g_currentLoadingFilename.clear();
    if (!path) return;
    std::string s = path;
    size_t dot = s.find_last_of('.');
    if (dot == std::string::npos) return;
    std::string ext = ToLowerCopy(s.substr(dot));
    if (!IsTextureExt(ext)) return;
    g_currentLoadingFilename = PbrStripPath(path);
}

void PbrAssociateBoundTexture(GLuint glId)
{
    if (glId == 0 || g_currentLoadingFilename.empty()) return;
    // Idempotent: multiple mip uploads keep the first association.
    if (g_texIdToName.find(glId) != g_texIdToName.end()) return;

    g_texIdToName[glId] = g_currentLoadingFilename;

    char buf[256];
    snprintf(buf, sizeof(buf), "PBR|associate|texid=%u|name=%s\n",
        glId, g_currentLoadingFilename.c_str());
    PbrLogLine(buf);
}

const char *PbrGetTextureName(GLuint glId)
{
    std::map<GLuint, std::string>::iterator it = g_texIdToName.find(glId);
    if (it == g_texIdToName.end()) return NULL;
    return it->second.c_str();
}

void PbrForgetTexture(GLuint glId)
{
    if (glId == 0) return;
    std::map<GLuint, std::string>::iterator it = g_texIdToName.find(glId);
    if (it == g_texIdToName.end()) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "PBR|forget|texid=%u|name=%s\n",
        glId, it->second.c_str());
    PbrLogLine(buf);
    g_texIdToName.erase(it);
}

// === Sidecar parsing ===
// Single source of default values — PbrPushDefaultEnvParams uses the same defaults.
static void ApplyPbrDefaults(PbrParams &p)
{
    p.metallic       = -1.0f;   // sentinel: shader picks default (env=1, non-env=0)
    p.roughness      = -1.0f;   // sentinel: shader falls back to d.a (stock K2)
    p.fresnelF0      = 0.04f;
    p.emissive       = 1.0f;
    p.normalStrength = 1.0f;    // bump xy multiplier (now wired in FPs): 1 = stock perturb, >1 stronger, 0 flat
    p.cavityStrength = 0.3f;
    p.fresnelRim     = 0.0f;    // 0 by default so unconfigured textures keep stock envmap intensity
    p.reflectivity   = 1.0f;
    p.billboardShadow = 0.0f;   // off: walls use the normal-gated shadow. Opt-in for steam/fog/foliage sheets.
    p.normalMap.clear();        // no custom normal map → auto <diffuse>n.tga
}

static void ApplyKv(const std::string &keyIn, const std::string &val, PbrParams &p)
{
    if (keyIn.empty() || val.empty()) return;
    std::string key = ToLowerCopy(keyIn);
    float fv = (float)atof(val.c_str());

    if      (key == "metallic")       p.metallic       = fv;
    else if (key == "roughness")      p.roughness      = fv;
    else if (key == "fresnelf0")      p.fresnelF0      = fv;
    else if (key == "emissive")       p.emissive       = fv;
    else if (key == "normalstrength") p.normalStrength = fv;
    else if (key == "cavitystrength") p.cavityStrength = fv;
    else if (key == "fresnelrim")     p.fresnelRim     = fv;
    else if (key == "reflectivity")   p.reflectivity   = fv;
    else if (key == "billboardshadow" || key == "volumeshadow") p.billboardShadow = fv;
    else if (key == "normalmap" || key == "normalmaptexture") {
        // Value is a texture stem (file Override/<stem>.tga). Strip a stray .tga.
        p.normalMap = val;
        if (p.normalMap.size() > 4) {
            std::string ext = ToLowerCopy(p.normalMap.substr(p.normalMap.size() - 4));
            if (ext == ".tga" || ext == ".tpc") p.normalMap.erase(p.normalMap.size() - 4);
        }
    }
}

// Parse one line into key/value. Accepts '=' or whitespace separator.
// Strips '#' and '//' line comments. Returns false if no pair found.
static bool ParseKvLine(const std::string &lineIn, std::string &outKey, std::string &outVal)
{
    std::string s = lineIn;
    size_t hash  = s.find('#');
    size_t slash = s.find("//");
    size_t cut = std::string::npos;
    if (hash  != std::string::npos) cut = hash;
    if (slash != std::string::npos && slash < cut) cut = slash;
    if (cut != std::string::npos) s = s.substr(0, cut);

    size_t sep = s.find('=');
    if (sep == std::string::npos) sep = s.find_first_of(" \t");
    if (sep == std::string::npos) return false;

    outKey = s.substr(0, sep);
    outVal = s.substr(sep + 1);
    TrimWs(outKey); TrimWs(outVal);
    return !outKey.empty() && !outVal.empty();
}

static bool ParseSidecarFile(const std::string &path, PbrParams &p)
{
    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) return false;
    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        std::string key, val;
        if (ParseKvLine(line, key, val))
            ApplyKv(key, val, p);
    }
    fclose(fp);
    return true;
}

// .txi parsed first (K2 native — engine ignores unknown keys); .pbr overrides.
static void ParsePbrSidecar(const std::string &baseName, PbrParams &p)
{
    bool gotTxi = ParseSidecarFile(kOverrideDir + baseName + ".txi", p);
    bool gotPbr = ParseSidecarFile(kOverrideDir + baseName + ".pbr", p);

    char buf[512];
    if (!gotTxi && !gotPbr)
    {
        snprintf(buf, sizeof(buf), "PBR|sidecar|%s|MISSING\n", baseName.c_str());
        PbrLogLine(buf);
        return;
    }
    snprintf(buf, sizeof(buf),
        "PBR|sidecar|%s|txi=%d pbr=%d|mtl=%.2f rgh=%.2f F0=%.2f emi=%.2f ns=%.2f cav=%.2f rim=%.2f refl=%.2f bbShadow=%.0f\n",
        baseName.c_str(), gotTxi ? 1 : 0, gotPbr ? 1 : 0,
        p.metallic, p.roughness, p.fresnelF0, p.emissive,
        p.normalStrength, p.cavityStrength, p.fresnelRim, p.reflectivity, p.billboardShadow);
    PbrLogLine(buf);
}

// === Sibling TGA loading ===
// Windows FS is case-insensitive — fopen resolves <baseName><suffix>.tga vs any
// case variant on disk. No suffix-case-flip fallback: would risk false matches
// (e.g. unrelated textures whose names end in the flipped-suffix letter).
static GLuint LoadSibling(const std::string &baseName, const char *suffix)
{
    std::string path = kOverrideDir + baseName + suffix + ".tga";
    TgaImage img;
    if (!TgaLoad(path.c_str(), &img)) return 0;
    GLuint id = TgaUploadAsTexture(&img);
    TgaFree(&img);
    return id;
}

// Allocate white-RGB image at given dimensions. Used as a placeholder host
// for the alpha-packing pipeline when the main sibling is missing.
static bool SynthesizeWhiteRgb(int w, int h, TgaImage *out)
{
    size_t bytes = (size_t)w * h * 3;
    unsigned char *buf = (unsigned char *)malloc(bytes);
    if (!buf) return false;
    memset(buf, 255, bytes);
    out->width = w;
    out->height = h;
    out->channels = 3;
    out->data = buf;
    out->glFormat = GL_RGB;
    out->glInternal = GL_RGB8;
    return true;
}

// Build packed RGBA buffer: main RGB + R-channel of alpha source as alpha.
// Alpha source is resampled nearest-neighbor when dimensions differ.
// Returns malloc'd buffer of size w*h*4, or NULL on allocation failure.
static unsigned char *PackRgbWithAlpha(const TgaImage &main, const TgaImage &alpha)
{
    int w = main.width, h = main.height;
    unsigned char *packed = (unsigned char *)malloc((size_t)w * h * 4);
    if (!packed) return NULL;

    const int mainCh  = main.channels;
    const int alphaCh = alpha.channels;
    const int aw = alpha.width, ah = alpha.height;
    for (int y = 0; y < h; ++y)
    {
        int ay = (y * ah) / h; if (ay >= ah) ay = ah - 1;
        for (int x = 0; x < w; ++x)
        {
            int ax = (x * aw) / w; if (ax >= aw) ax = aw - 1;
            const unsigned char *src = main.data + (y * w + x) * mainCh;
            unsigned char r = src[0];
            unsigned char g = (mainCh >= 2) ? src[1] : r;
            unsigned char b = (mainCh >= 3) ? src[2] : r;
            unsigned char a = alpha.data[(ay * aw + ax) * alphaCh];
            int di = (y * w + x) * 4;
            packed[di + 0] = r;
            packed[di + 1] = g;
            packed[di + 2] = b;
            packed[di + 3] = a;
        }
    }
    return packed;
}

// Load main TGA, attempt to pack R-channel of secondary TGA into main's alpha.
// 5 PBR channels (n/R/M/O/E) compress to 3 TMUs (n+O, R+E, M) — Aspyr driver
// wrapper rejects TMU 11+, forcing this packing.
//
// Edge cases:
// - main missing + alpha present → synthesize white main at alpha dims.
//   Lets user provide AO without normal, or emissive without rough host.
// - alpha source missing → upload main unmodified.
// - dimension mismatch → alpha resampled nearest-neighbor to main dims.
static GLuint LoadSiblingPacked(const std::string &baseName,
                                const char *mainSuffix,
                                const char *alphaSuffix,
                                bool *outAlphaPacked,
                                bool *outMainSynth)
{
    if (outAlphaPacked) *outAlphaPacked = false;
    if (outMainSynth)   *outMainSynth   = false;

    std::string mainPath  = kOverrideDir + baseName + mainSuffix  + ".tga";
    std::string alphaPath = kOverrideDir + baseName + alphaSuffix + ".tga";

    TgaImage main, alpha;
    bool haveMain  = TgaLoad(mainPath.c_str(),  &main);
    bool haveAlpha = TgaLoad(alphaPath.c_str(), &alpha);
    if (!haveMain && !haveAlpha) return 0;

    bool synthMain = false;
    if (!haveMain)
    {
        if (!SynthesizeWhiteRgb(alpha.width, alpha.height, &main))
            { TgaFree(&alpha); return 0; }
        synthMain = true;
        haveMain  = true;
    }
    auto cleanupMain = [&] { if (synthMain) free(main.data); else TgaFree(&main); };

    // No alpha source — upload main alone.
    if (!haveAlpha)
    {
        GLuint id = TgaUploadAsTexture(&main);
        cleanupMain();
        return id;
    }

    // Pack alpha source into main.alpha.
    unsigned char *packed = PackRgbWithAlpha(main, alpha);
    if (!packed)
    {
        GLuint id = TgaUploadAsTexture(&main);
        TgaFree(&alpha);
        cleanupMain();
        return id;
    }

    TgaImage rgba;
    rgba.width      = main.width;
    rgba.height     = main.height;
    rgba.channels   = 4;
    rgba.data       = packed;
    rgba.glFormat   = GL_RGBA;
    rgba.glInternal = GL_RGBA8;
    GLuint id = TgaUploadAsTexture(&rgba);

    free(packed);
    cleanupMain();
    TgaFree(&alpha);

    if (id)
    {
        if (outAlphaPacked) *outAlphaPacked = true;
        if (outMainSynth)   *outMainSynth   = synthMain;
    }
    return id;
}

static void ResolveSiblings(const std::string &baseName, SiblingSet &set)
{
    ApplyPbrDefaults(set.params);
    ParsePbrSidecar(baseName, set.params);

    // Texture uploads re-enter glTexImage2D hook — RAII guard blocks re-query.
    {
        LoadingGuard guard;
        if (!set.params.normalMap.empty()) {
            // txi `normalmap <stem>`: load Override/<stem>.tga directly as the normal
            // (RGB), no AO packing — lets recolored variants share one normal map.
            set.normalId    = LoadSibling(set.params.normalMap, "");
            set.normalSynth = false;
            set.aoPacked    = false;
        } else {
            set.normalId = LoadSiblingPacked(baseName, "n", "O", &set.aoPacked, &set.normalSynth);
        }
        set.roughId  = LoadSiblingPacked(baseName, "R", "E", &set.emiPacked, &set.roughSynth);
        set.metalId  = LoadSibling(baseName, "M");
    }

    char buf[512];
    snprintf(buf, sizeof(buf),
        "PBR|resolve|%s|nrm=%u(synth=%d ao=%d) rgh=%u(synth=%d emi=%d) mtl=%u\n",
        baseName.c_str(),
        set.normalId, set.normalSynth ? 1 : 0, set.aoPacked ? 1 : 0,
        set.roughId,  set.roughSynth  ? 1 : 0, set.emiPacked ? 1 : 0,
        set.metalId);
    PbrLogLine(buf);
}

SiblingSet *PbrGetSiblings(GLuint diffId)
{
    if (diffId == 0) return NULL;
    std::map<GLuint, std::string>::iterator it = g_texIdToName.find(diffId);
    if (it == g_texIdToName.end()) return NULL;

    const std::string &name = it->second;
    std::map<std::string, SiblingSet>::iterator cit = g_siblingCache.find(name);
    if (cit != g_siblingCache.end()) {
        // Update LRU timestamp on cache hit.
        static unsigned s_frameCounter = 0;
        s_frameCounter++;
        cit->second.lastUsed = s_frameCounter;
        return &cit->second;
    }

    // Enforce cache size limit with LRU eviction. When the cache exceeds the
    // configured limit, evict the least-recently-used entries. This prevents
    // unbounded memory growth during long play sessions where hundreds of
    // unique textures are loaded across area transitions. GL textures owned
    // by evicted sets are freed.
    int limit = g_config.siblingCacheLimit;
    if (limit > 0 && (int)g_siblingCache.size() >= limit)
    {
        int toEvict = (int)g_siblingCache.size() / 2;
        // Build a vector of (lastUsed, iterator) pairs, sort by lastUsed ascending,
        // evict the oldest toEvict entries.
        // Collect (lastUsed, name) pairs — use the string key instead of the
        // iterator so std::sort can compare pairs without needing iterator<.
        std::vector<std::pair<unsigned, std::string> > lru;
        lru.reserve(g_siblingCache.size());
        for (std::map<std::string, SiblingSet>::iterator it = g_siblingCache.begin();
             it != g_siblingCache.end(); ++it)
            lru.push_back(std::make_pair(it->second.lastUsed, it->first));
        // Sort by lastUsed ascending (oldest first). Custom comparator avoids
        // comparing the second element (string) when firsts are equal.
        std::sort(lru.begin(), lru.end(),
            [](const std::pair<unsigned, std::string> &a,
               const std::pair<unsigned, std::string> &b) {
                return a.first < b.first;
            });
        for (int i = 0; i < toEvict && i < (int)lru.size(); ++i)
        {
            std::map<std::string, SiblingSet>::iterator eit = g_siblingCache.find(lru[i].second);
            if (eit == g_siblingCache.end()) continue;
            SiblingSet &ss = eit->second;
            if (ss.normalId) glDeleteTextures(1, &ss.normalId);
            if (ss.roughId)  glDeleteTextures(1, &ss.roughId);
            if (ss.metalId)  glDeleteTextures(1, &ss.metalId);
            g_siblingCache.erase(eit);
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "PBR|cache_evict_lru|evicted=%d|remaining=%zu\n",
                 toEvict, g_siblingCache.size());
        PbrLogLine(buf);
    }

    // Value-initialize: zeroes the POD fields and default-constructs the
    // std::string member (memset on a struct containing std::string is UB).
    SiblingSet set = SiblingSet();
    ResolveSiblings(name, set);
    g_siblingCache[name] = set;
    return &g_siblingCache[name];
}

// === Env-param push ===
// program.env[20] = (metallic, roughness, fresnelF0, emissiveScale)
// program.env[21] = (useNormal, useRough, useMetal, useAO)   — booleans (0/1)
// program.env[22] = (normalStrength, useEmissive, cavityStrength, fresnelRim)
// program.env[23] = (reflectivity, billboardShadow, _, _)
static void PushAllEnvParams(const PbrParams &p,
                             float useNrm, float useRgh, float useMtl,
                             float useAO,  float useEmi)
{
    if (!orig_glProgramEnvParameter4d) return;
    orig_glProgramEnvParameter4d(GL_FRAGMENT_PROGRAM_ARB, 20,
        p.metallic, p.roughness, p.fresnelF0, p.emissive);
    orig_glProgramEnvParameter4d(GL_FRAGMENT_PROGRAM_ARB, 21,
        useNrm, useRgh, useMtl, useAO);
    orig_glProgramEnvParameter4d(GL_FRAGMENT_PROGRAM_ARB, 22,
        p.normalStrength, useEmi, p.cavityStrength, p.fresnelRim);
    orig_glProgramEnvParameter4d(GL_FRAGMENT_PROGRAM_ARB, 23,
        p.reflectivity, p.billboardShadow, 0.0, 0.0);
    PbrTune_PushEnvParams();
}

void PbrPushDefaultEnvParams()
{
    PbrParams p;
    ApplyPbrDefaults(p);
    PushAllEnvParams(p, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
}

void PbrPushEnvParams(GLuint diffId)
{
    SiblingSet *s = PbrGetSiblings(diffId);
    if (!s) { PbrPushDefaultEnvParams(); return; }

    // Flags derived from sibling presence — sibling auto-enables its effect.
    // Synthetic placeholders (e.g. white-R created to host emissive) don't
    // count as real maps — keeps shader from sampling them as normal/roughness.
    PushAllEnvParams(s->params,
        (s->normalId && !s->normalSynth) ? 1.0f : 0.0f,
        (s->roughId  && !s->roughSynth)  ? 1.0f : 0.0f,
        s->metalId                       ? 1.0f : 0.0f,
        s->aoPacked                      ? 1.0f : 0.0f,
        s->emiPacked                     ? 1.0f : 0.0f);
}
