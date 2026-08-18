#version 120
// Faithful GLSL port of fp_worldtex_bump_env ARB + shadow_receive.inc.
// Pairs with vp_static_env_fog.vs.glsl. Pure-reflective bumpmapped walls — no diffuse.
// TMU: texNrm=8, texRgh=9, texMtl=10, texEnv=5(cube), texShadow=6

uniform sampler2D texNrm;
uniform sampler2D texRgh;
uniform sampler2D texMtl;
uniform samplerCube texEnv;
uniform sampler2D texShadow;

uniform vec4 pbr; uniform vec4 fl; uniform vec4 ns; uniform vec4 ux;
uniform vec4 tn; uniform vec4 tnB; uniform vec4 tnC; uniform vec4 tnD;
uniform vec4 tnE; uniform vec4 tnF; uniform vec4 tnG; uniform vec4 uPcf;
uniform vec4 uFL0pos; uniform vec4 uFL0dif; uniform vec4 uCamW;
uniform vec4 uK0, uK1, uK2;
uniform vec4 uFogColor;
uniform vec4 uCamLight;   // env[34] x=strength y=exponent z=range w=unused

const vec3 dF0 = vec3(0.04);
const vec2 envFlr = vec2(0.75, 0.25);

void main()
{
    vec2 uv = gl_TexCoord[0].xy;
    vec3 v  = gl_Color.rgb;
    vec3 Y  = gl_TexCoord[2].xyz;   // cube reflect coord (tc2)
    vec3 N4 = gl_TexCoord[4].xyz;
    vec3 P5 = gl_TexCoord[5].xyz;

    float rs = (pbr.y < 0.0) ? 1.0 : pbr.y;
    if (fl.y > 0.0) {
        rs = mix(rs, texture2D(texRgh, uv).r, fl.y);
    }
    float mt = (pbr.x < 0.0) ? 1.0 : pbr.x;
    if (fl.z > 0.0) mt = mix(mt, texture2D(texMtl, uv).r, fl.z);
    float usePbr = max((pbr.y >= 0.0) ? 1.0 : 0.0, fl.y);

    // Normal decode
    vec3 N = N4;
    if (fl.x > 0.0) {
        vec3 nrm = texture2D(texNrm, uv).xyz * 2.0 - 1.0;
        vec4 nrmD = texture2D(texNrm, uv * tn.z);
        nrm.xy += (nrmD.xy * 2.0 - 1.0) * tn.y;
        nrm.xy *= ns.x;
        float nrmInvLen = inversesqrt(dot(nrm, nrm));
        nrm *= nrmInvLen;
        N = nrm * fl.x + N4;
    }
    N = normalize(N);

    vec3 V = normalize(uCamW.xyz - P5);
    float NdotV = max(dot(N, V), 0.0);
    float fres = 1.0 - NdotV;
    fres = (fres * fres) * (fres * fres) * fres;

    // Fresnel — no diffuse sample; F0 defaults to metal·white
    vec3 F0 = mix(vec3(1.0), dF0, 1.0 - mt);
    vec3 F  = mix(F0, vec3(1.0), fres);

    // AO
    float ao = 1.0;
    if (fl.w > 0.0) {
        float nrmA = texture2D(texNrm, uv).a;
        ao = clamp((nrmA - 1.0) * (fl.w * ns.z) + 1.0, 0.0, 1.0);
    }

    // Light proxy
    float lit = v.x * envFlr.x + envFlr.y;
    lit = min(lit, 1.0);
    lit = mix(1.0, lit, usePbr);

    // Env weight: stock=1.0 (pure reflection), PBR=(1-rs)+fres·rim
    float envW = mix(1.0, (1.0 - rs + fres * ns.w), usePbr) * ux.x;
    envW *= lit * ao;
    envW *= (1.0 + fl.x * tnC.x);   // env boost on PBR

    // L0 spec
    vec3 r = vec3(0.0);
    if (fl.x > 0.0) {
        vec3  Ldir  = normalize(uFL0pos.xyz - P5);
        vec3  H     = normalize(Ldir + V);
        float NdotH = pow(max(dot(N, H), 0.0), tnC.y) * (1.0 - rs) * fl.x;
        r = (uFL0dif.rgb * F * tnB.z) * NdotH;
    }

    // shadow_receive.inc
    vec4 shc = vec4(N4 * tnG.w + P5, 1.0);
    float tx = dot(uK0, shc);
    float ty = dot(uK1, shc);
    float tz = dot(uK2, shc);
    vec2 shUV = vec2(tx, ty) * 0.5 + 0.5;
    vec2 sh2;
    sh2.x = gl_FragCoord.x * 0.06711056 + gl_FragCoord.y * 0.00583715;
    sh2.y = gl_FragCoord.x * 0.00583715 + gl_FragCoord.y * 0.06711056;
    sh2 = fract(sh2);
    sh2 = fract(sh2 * 52.9829189);
    sh2 = (sh2 * 2.0 - 1.0) * uPcf.xy;
    shUV += sh2;
    float shZ = clamp(tz * 0.5 + 0.5, 0.0, 0.99);
    vec2 ge = step(0.0, shUV);
    vec2 le = step(shUV, vec2(1.0));
    float inb = ge.x * ge.y * le.x * le.y;
    inb *= step(tz, 1.0);
    float zcmp = shZ - tnF.w;
    float s = 0.0;
    s += step(zcmp, texture2D(texShadow, shUV + uPcf.xy).x);
    s += step(zcmp, texture2D(texShadow, shUV - uPcf.xy).x);
    s += step(zcmp, texture2D(texShadow, shUV + vec2(-uPcf.x, uPcf.y)).x);
    s += step(zcmp, texture2D(texShadow, shUV + vec2(uPcf.x, -uPcf.y)).x);
    s *= 0.25;
    s = mix(1.0, s, inb);
    float shClip = s;
    float shadowed = s * (1.0 - tnF.z) + tnF.z;
    shadowed = mix(1.0, shadowed, tnF.x);

    // Baked sun spec
    if (fl.x > 0.0) {
        vec3  Hs   = normalize(tnD.xyz + V);
        float NdHs = pow(max(dot(N, Hs), 0.0), tnC.z) * fl.x * tn.x * tnB.w * shadowed;
        r += F * NdHs;
    }

    // Env composite
    vec3 m = textureCube(texEnv, Y).rgb;
    r += m * envW;

    // Sun-shadow global contrast
    float nl = min(max(dot(N4, tnD.xyz), 0.0) * 4.0, 1.0);
    float contrast = 1.0 - ((1.0 - shClip) * nl * tnG.x);
    contrast = max(contrast, tnF.z);
    r *= contrast;

    r *= ao;
    r *= (1.0 - fl.x * tnD.w);

    // --- Camera light: sky-behind-camera specular (default off: strength=0) ---
    if (uCamLight.x > 0.0) {
        float dist  = length(uCamW.xyz - P5);
        float falloff = max(1.0 - dist / uCamLight.z, 0.0);
        float camSpec = pow(NdotV, uCamLight.y) * uCamLight.x * falloff * mix(1.0, 1.0 - clamp(rs, 0.0, 1.0), usePbr);
        r += tnE.yzw * camSpec;
    }

    // --- Rim light: additive fresnel at grazing angles (ns.w = fresnelRim from TXI) ---
    if (ns.w > 0.0) {
        float rim = pow(1.0 - NdotV, 4.0) * ns.w;
        r += tnE.yzw * rim * 0.4;
    }

    // Soft shoulder tonemap
    {
        vec3 knee = vec3(0.9);
        vec3 excess = max(r - knee, vec3(0.0));
        r = min(r, knee) + excess / (vec3(1.0) + excess * vec3(10.0));
    }

    // Fog
    float fogAmt = gl_SecondaryColor.r;
    r = clamp(r, 0.0, 1.0);
    r = mix(r, uFogColor.rgb, fogAmt);
    r = clamp(r, 0.0, 1.0);

    gl_FragColor = vec4(r, 1.0);
}