#version 120
// ============================================================================
// Faithful GLSL port of vp_static_env_fog (ARB) — static geometry with envmap.
// Pairs with fp_worldtex_env_reflective / fp_worldtex_bump_env / fp_worldtex_lm_env.
// See shaders_override/vp_static_env_fog.txt for the original ARB source.
// ============================================================================

uniform vec4 uVI0;   // env[92]
uniform vec4 uVI1;   // env[91]
uniform vec4 uVI2;   // env[90]
uniform vec4 uVI3;   // env[89]

uniform vec4 uL0pos, uL0amb, uL0dif, uL0att;   // env 87,86,85,83
uniform vec4 uL1pos, uL1amb, uL1dif, uL1att;   // env 82,81,80,78
uniform vec4 uL2pos, uL2amb, uL2dif, uL2att;   // env 77,76,75,73
uniform vec4 uAmb;        // env[93]
uniform vec4 uDifScale;   // env[94]
uniform vec4 uAmbScale;   // env[95]
uniform vec4 uFogParams;  // x=?, y=start, z=?, w=1/span

void addLight(vec3 wPos, vec3 wN, vec4 lpos, vec4 lamb, vec4 ldif, vec4 latt, inout vec3 col)
{
    vec3  L      = lpos.xyz - wPos;
    float d2     = dot(L, L);
    float invLen = inversesqrt(d2);
    vec3  Ln     = L * invLen;
    float dist   = d2 * invLen;
    float atten  = latt.x + latt.y * dist + latt.z * d2;
    float invAtt = 1.0 / atten;
    float ndl    = max(dot(wN, Ln), 0.0);
    col += (lamb.rgb * invAtt) * uAmbScale.rgb;
    col += (ldif.rgb * (ndl * invAtt)) * uDifScale.rgb;
}

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    vec4 eye  = gl_ModelViewMatrix * gl_Vertex;
    vec3 eyeN = mat3(gl_ModelViewMatrix) * gl_Normal;

    // --- Fog: secondary.x = (|eye| - start) * (1/span), gated on fog.color>0 ---
    float fogF = clamp((length(eye.xyz) - uFogParams.y) * uFogParams.w, 0.0, 1.0);
    gl_FrontSecondaryColor = vec4(fogF, fogF, fogF, 1.0);

    // --- Cube reflect coord (world space) ---
    float NdE = dot(eyeN, eye.xyz);
    vec3 R = eye.xyz - NdE * eyeN * 2.0;   // reflect scale = 2.0 (standard)
    R = normalize(R);
    vec3 wR;
    wR.x = dot(R, uVI0.xyz);
    wR.y = dot(R, uVI1.xyz);
    wR.z = dot(R, uVI2.xyz);
    gl_TexCoord[2] = vec4(wR, 1.0);   // cube reflect → tc2 (matches ARB env_reflective/lm_env)

    // --- Stock texcoords ---
    gl_TexCoord[0] = gl_MultiTexCoord0;   // diffuse UV
    gl_TexCoord[1] = gl_MultiTexCoord2;   // lightmap UV → tc1 (matches ARB env_reflective/lm_env)

    // --- World-space position + normal ---
    vec3 wPos;
    wPos.x = dot(eye, uVI0);
    wPos.y = dot(eye, uVI1);
    wPos.z = dot(eye, uVI2);
    vec3 wN;
    wN.x = dot(eyeN, uVI0.xyz);
    wN.y = dot(eyeN, uVI1.xyz);
    wN.z = dot(eyeN, uVI2.xyz);

    gl_TexCoord[4] = vec4(wN, 0.0);
    gl_TexCoord[5] = vec4(wPos, 0.0);

    // --- Stock per-vertex 3-light lighting ---
    vec3 col = vec3(uVI3.x);
    addLight(wPos, wN, uL0pos, uL0amb, uL0dif, uL0att, col);
    addLight(wPos, wN, uL1pos, uL1amb, uL1dif, uL1att, col);
    addLight(wPos, wN, uL2pos, uL2amb, uL2dif, uL2att, col);
    col += uAmb.rgb;

    gl_FrontColor = vec4(col, uDifScale.w);
}