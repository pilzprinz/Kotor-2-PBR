#version 120
// ============================================================================
// Faithful GLSL port of fp_worldtex_lm_fog_alpha (ARB) + shadow_receive.inc.
// Pairs with fp_worldtex_diffuse_main.vs.glsl. Mirrors the ARB FP (the primary
// diffuse world surface / catch-all): stock diffuse * (vtx_light + lightmap) +
// fog; plus Schlick Fresnel, L0 Blinn-Phong spec, energy conservation, AO,
// directional sun + shadow PCF, sun-shadow global contrast.
//
// All uniforms are read CPU-side from the SAME FRAGMENT program.env slots the
// ARB FP read (glsl_program.cpp), so the inputs are identical to the ARB path.
//
// OPTIMIZATION vs the ARB (which had no branches): the per-draw-uniform flags
// (fl.x/y/z/w, ns.y) gate work that the ARB always paid for but multiplied out
// to zero. Here they guard the matching texture samples + spec POWs. The flags
// are uniform across the draw, so the branches are fully coherent and the output
// is IDENTICAL — when a flag is 0 the guarded result was already 0/no-op. For the
// catch-all's many plain/sky/foliage surfaces (flags 0) this skips up to 4 texture
// fetches + 2 POWs + 3 normalizes per fragment.
// ============================================================================

uniform sampler2D tex0;       // diffuse  (TMU0)
uniform sampler2D tex1;       // lightmap (TMU1)
uniform sampler2D texNrm;     // normal   (TMU8 sibling)
uniform sampler2D texRgh;     // rough    (TMU9 sibling)
uniform sampler2D texMtl;     // metal    (TMU10 sibling)
uniform sampler2D texShadow;  // shadow depth (TMU6)

uniform vec4 pbr;   // env[20] x=metal y=rough z=F0 w=emiScale
uniform vec4 fl;    // env[21] x=useNrm y=useRgh z=useMtl w=useAO
uniform vec4 ns;    // env[22] x=nrmStr y=useEmi z=cavityStr w=fresRim
uniform vec4 ux;    // env[23] x=reflectivity y=billboardShadow
uniform vec4 tn;    // env[24] x=sheenStr y=detBlend z=detUV w=alphaShiftK
uniform vec4 tnB;   // env[25] x=perturb y=lodScale z=L0Gain w=sunGain
uniform vec4 tnC;   // env[26] x=envBoost y=L0Exp z=SunExp (w=GLSL toggle, unused here)
uniform vec4 tnD;   // env[27] xyz=sunDir w=diffDim
uniform vec4 tnE;   // env[28] x=sunDiffIntens yzw=sunColor
uniform vec4 tnF;   // env[29] x=str y=viz z=floor w=bias
uniform vec4 tnG;   // env[30] x=shadowDarken w=nrmBias
uniform vec4 uPcf;  // env[32] (1/res,1/res,0,0)
uniform vec4 uFL0pos; // FRAGMENT env[87] light0 world pos (separate GL state from the VS's VERTEX env[87])
uniform vec4 uFL0dif; // FRAGMENT env[86] light0 diffuse color
uniform vec4 uCamW;   // (env92.w, env91.w, env90.w) camera world pos
uniform vec4 uK0, uK1, uK2;  // env[100..102] shadow K rows (K[3] unused: ortho w==1)
uniform vec4 uFogColor;      // real GL_FOG_COLOR (C++ glGetFloatv), not gl_Fog
                             // (fog FACTOR now comes from the VS in gl_TexCoord[5].w, per-vertex like ARB)
uniform vec4 uCamLight;      // env[34] x=strength y=exponent z=range w=unused

void main()
{
    vec2 uv = gl_TexCoord[0].xy;
    vec3 v  = gl_Color.rgb;          // VS per-vertex lit color
    vec3 N4 = gl_TexCoord[4].xyz;    // world normal (geometric)
    vec3 P5 = gl_TexCoord[5].xyz;    // world position

    // --- stock samples ---
    vec4 d = texture2D(tex0, uv);
    vec4 l = texture2D(tex1, gl_TexCoord[1].xy);
    vec3 albedo = d.rgb;             // raw diffuse, for F0 (before the lit combine)

    // --- material scalars (sample rough/metal only when a flag uses them) ---
    float rs   = (pbr.y < 0.0) ? 1.0 : pbr.y;   // CMP: pbr.y<0 ? 1 : pbr.y
    float emiA = 0.0;                            // roughS.a, only used via ns.y
    if (fl.y > 0.0 || ns.y > 0.0) {
        vec4 roughS = texture2D(texRgh, uv);
        rs   = mix(rs, roughS.r, fl.y);
        emiA = roughS.a;
    }
    float mt = (pbr.x < 0.0) ? 0.0 : pbr.x;
    if (fl.z > 0.0) mt = mix(mt, texture2D(texMtl, uv).r, fl.z);
    float usePbr = max((pbr.y >= 0.0) ? 1.0 : 0.0, fl.y);

    // --- normal decode + detail resample + perturb; sampled only when normal-mapped
    //     or AO is on. nrmInvLen feeds the AO quirk below. When skipped, N = N4 and
    //     nrmInvLen = 1 -> AO = 1, which is exactly what the math yields anyway. ---
    float nrmInvLen = 1.0;
    vec3  N = N4;
    if (fl.x > 0.0 || fl.w > 0.0) {
        vec3 nrm = texture2D(texNrm, uv).xyz * 2.0 - 1.0;
        vec4 nrmD = texture2D(texNrm, uv * tn.z);            // detail tap (8x UV by default)
        nrm.xy += (nrmD.xy * 2.0 - 1.0) * tn.y;              // blend detail xy
        nrm.xy *= ns.x;                                      // normalStrength (xy only)
        // ARB reuses nrm.w as the normalize 1/length; the later "AO from normal.a"
        // reads that clobbered value (NOT the texture alpha). Reproduced for parity.
        nrmInvLen = inversesqrt(dot(nrm, nrm));
        nrm *= nrmInvLen;
        N = nrm * fl.x + N4;
    }
    N = normalize(N);

    // --- view dir (world) ---
    vec3 V = normalize(uCamW.xyz - P5);

    // --- Schlick Fresnel scalar (always needed for energy conservation; pow5) ---
    float NdotV = max(dot(N, V), 0.0);
    float fres = 1.0 - NdotV;
    fres = (fres * fres) * (fres * fres) * fres;

    // --- stock base: d *= (v + lightmap) ---
    d.rgb *= (v + l.rgb);

    // --- energy conservation: *= (1-F)*(1-mt), gated by usePbr ---
    d.rgb *= mix(1.0, (1.0 - fres) * (1.0 - mt), usePbr);

    // --- emissive (additive), gated by useEmi (ns.y) ---
    d.rgb += d.rgb * (emiA * pbr.w * ns.y);

    // --- AO from clobbered normal.w (see note above), gated by useAO*cavityStr ---
    float ao = clamp((nrmInvLen - 1.0) * (fl.w * ns.z) + 1.0, 0.0, 1.0);

    // ========================================================================
    // shadow_receive.inc — directional sun shadow, single map (texture[6]).
    // Computed before the specular terms (which need `shadowed`). Outputs:
    // shClip = raw lit factor (1=lit,0=occluded); shadowed = after floor+strength.
    // ========================================================================
    vec4 shc = vec4(N4 * tnG.w + P5, 1.0);   // normal-offset receiver bias
    float tx = dot(uK0, shc);
    float ty = dot(uK1, shc);
    float tz = dot(uK2, shc);                // ortho: w==1, divide dropped
    vec2 shUV = vec2(tx, ty) * 0.5 + 0.5;
    // per-pixel IGN dither (two hashes, swapped const pairs)
    vec2 sh2;
    sh2.x = gl_FragCoord.x * 0.06711056 + gl_FragCoord.y * 0.00583715;
    sh2.y = gl_FragCoord.x * 0.00583715 + gl_FragCoord.y * 0.06711056;
    sh2 = fract(sh2);
    sh2 = fract(sh2 * 52.9829189);
    sh2 = (sh2 * 2.0 - 1.0) * uPcf.xy;
    shUV += sh2;
    float shZ = clamp(tz * 0.5 + 0.5, 0.0, 0.99);
    // in-bounds test (outside [0,1]^2 -> lit)
    vec2 ge = step(0.0, shUV);
    vec2 le = step(shUV, vec2(1.0));
    float inb = ge.x * ge.y * le.x * le.y;
    // Z gate: a receiver BEYOND the light far plane (skybox/backdrop, tz>1) projects to
    // depth 0.99 and would read as occluded by every nearer scene texel -> wrongly
    // "shadowed" (the skybox darkening/vanishing per camera angle). Force it lit.
    // (tz<-1, in front of near, already reads lit via the shZ clamp to 0.)
    inb *= step(tz, 1.0);
    float zcmp = shZ - tnF.w;                // bias
    // 4-tap diagonal PCF: lit when stored depth >= receiver depth
    float s = 0.0;
    s += step(zcmp, texture2D(texShadow, shUV + uPcf.xy).x);
    s += step(zcmp, texture2D(texShadow, shUV - uPcf.xy).x);
    s += step(zcmp, texture2D(texShadow, shUV + vec2(-uPcf.x, uPcf.y)).x);
    s += step(zcmp, texture2D(texShadow, shUV + vec2(uPcf.x, -uPcf.y)).x);
    s *= 0.25;
    s = mix(1.0, s, inb);                    // out of bounds -> lit
    float shClip = s;                        // raw lit factor (global contrast)
    float shadowed = s * (1.0 - tnF.z) + tnF.z;   // floor
    shadowed = mix(1.0, shadowed, tnF.x);         // strength

    // --- specular (L0 Blinn-Phong + baked sun), both F-tinted and gated by fl.x.
    //     F (and F0 from raw albedo) is only used here, so skip it all when fl.x==0
    //     (the ARB multiplied both by fl.x -> 0 anyway). Additive, so it commutes
    //     with the shadow block being moved above it. ---
    if (fl.x > 0.0) {
        vec3 F0 = mix(vec3(0.04), albedo, mt);
        vec3 F  = mix(F0, vec3(1.0), fres);
        // L0 directional Blinn-Phong ^L0Exp
        vec3  Ldir  = normalize(uFL0pos.xyz - P5);
        vec3  H     = normalize(Ldir + V);
        float NdotH = pow(max(dot(N, H), 0.0), tnC.y) * (1.0 - rs) * fl.x;
        d.rgb += (uFL0dif.rgb * F * tnB.z) * NdotH;
        // baked sun spec — sheen, shadowed
        vec3  Hs   = normalize(tnD.xyz + V);
        float NdHs = pow(max(dot(N, Hs), 0.0), tnC.z) * fl.x * tn.x * tnB.w * shadowed;
        d.rgb += F * NdHs;
    }

    // --- sun diffuse: N.L * intens * sunColor * albedo, shadowed ---
    float sd = max(dot(N, tnD.xyz), 0.0) * tnE.x * shadowed;
    d.rgb += (d.rgb * tnE.yzw) * sd;

    // --- sun-shadow global contrast: darken sun-facing occluded surfaces ---
    float nl = min(max(dot(N4, tnD.xyz), 0.0) * 4.0, 1.0);
    float contrast = 1.0 - ((1.0 - shClip) * nl * tnG.x);
    contrast = max(contrast, tnF.z);
    d.rgb *= contrast;

    // --- billboard/volume shadow (ux.y flag; 0 on normal materials) ---
    float bmul = 1.0 - ((1.0 - shClip) * tnG.x * ux.y);
    bmul = max(bmul, tnF.z);
    d.rgb *= bmul;

    // --- AO global + PBR diffuse dim ---
    d.rgb *= ao;
    d.rgb *= (1.0 - fl.x * tnD.w);

    // --- Camera light: sky-behind-camera specular (default off: strength=0) ---
    if (uCamLight.x > 0.0) {
        float dist  = length(uCamW.xyz - P5);
        float falloff = max(1.0 - dist / uCamLight.z, 0.0);
        float camSpec = pow(NdotV, uCamLight.y) * uCamLight.x * falloff * mix(1.0, 1.0 - clamp(rs, 0.0, 1.0), usePbr);
        d.rgb += tnE.yzw * camSpec;
    }

    // --- Rim light: additive fresnel at grazing angles (ns.w = fresnelRim from TXI) ---
    if (ns.w > 0.0) {
        float rim = pow(1.0 - NdotV, 4.0) * ns.w;
        d.rgb += tnE.yzw * rim * 0.4;
    }

    // --- Soft shoulder tonemap: preserves contrast below 0.9, compresses above ---
    // Knee at 0.9 — normal maps stay fully visible. Compression 2x — gentle rolloff.
    {
        vec3 knee = vec3(0.9);
        vec3 excess = max(d.rgb - knee, vec3(0.0));
        d.rgb = min(d.rgb, knee) + excess / (vec3(1.0) + excess * vec3(10.0));
    }
    d.rgb = min(d.rgb, 1.0);  // prevent bloom from overbright spec/sun

    // --- fog + final: gl_TexCoord[5].w is the per-vertex linear fog factor from the VS
    //     (= ARB VP result.color.secondary, gated on fog.color>0). Mix to the real GL fog
    //     color; alpha preserved (punchthrough holes stay holes). ---
    // Fog factor from the SECONDARY COLOR (= ARB fragment.color.secondary), so it gets the
    // identical wrapper treatment as the ARB path (the per-vertex factor the VS wrote).
    float fogAmt = gl_SecondaryColor.r;
    d.rgb = mix(d.rgb, uFogColor.rgb, fogAmt);

    gl_FragColor = vec4(d.rgb, d.a);
}
