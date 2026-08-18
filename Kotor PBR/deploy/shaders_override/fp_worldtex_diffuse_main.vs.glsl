#version 120
// ============================================================================
// Faithful GLSL port of vp_static_lit_fog (ARB) — pairs with
// fp_worldtex_diffuse_main.fs.glsl. Bound via glUseProgram override when the
// engine binds (vp_static_lit_fog + fp_worldtex_diffuse_main). See
// shaders_override/vp_static_lit_fog.txt for the original ARB source this mirrors.
//
// Every uniform is read CPU-side from the SAME program.env slot the ARB VP read,
// via glGetProgramEnvParameterfvARB(GL_VERTEX_PROGRAM_ARB, ...) (glsl_program.cpp),
// so there is zero divergence from the ARB inputs.
// ============================================================================

// viewInv (camera->world) rows, from VERTEX env[92,91,90,89]. The ARB builds
// wPos.x = dot(eye,env92), .y=dot(eye,env91), .z=dot(eye,env90), .w=dot(eye,env89).
uniform vec4 uVI0;   // env[92]
uniform vec4 uVI1;   // env[91]
uniform vec4 uVI2;   // env[90]
uniform vec4 uVI3;   // env[89]  (affine bottom row (0,0,0,1); ARB also used .xxxw as the (0,0,0,1) lighting init)

// 3 engine point lights (VERTEX env). Per light: pos, ambient color, diffuse color,
// attenuation poly (const, linear, quad). Slot layout matches the ARB blocks.
uniform vec4 uL0pos, uL0amb, uL0dif, uL0att;   // env 87,86,85,83
uniform vec4 uL1pos, uL1amb, uL1dif, uL1att;   // env 82,81,80,78
uniform vec4 uL2pos, uL2amb, uL2dif, uL2att;   // env 77,76,75,73
uniform vec4 uAmb;        // env[93] scene ambient add
uniform vec4 uDifScale;   // env[94] global diffuse scale (.w = vertex alpha)
uniform vec4 uAmbScale;   // env[95] global ambient scale
uniform vec4 uFogParams;  // x=fogBehavior[2] y=fogBehavior[3] z=(fog.color>0) gate (per-draw, from C++).
                          // The linear fog factor is computed HERE per-vertex — identical to the ARB
                          // VP's result.color.secondary — so the FS just mixes with it (matches ARB's
                          // per-vertex interpolation, not the prior per-pixel reconstruction).

// One light's contribution. ARB uses LIT: only LIT.x (=1, ambient) and LIT.y
// (=max(N.L,0), diffuse) are consumed; specular (LIT.z) is discarded. Attenuation
// = 1/(c + l*dist + q*dist^2). World normal is intentionally NOT renormalised
// (matches the ARB, which dots the raw world normal).
void addLight(vec3 wPos, vec3 wN, vec4 lpos, vec4 lamb, vec4 ldif, vec4 latt, inout vec3 col)
{
    vec3  L      = lpos.xyz - wPos;
    float d2     = dot(L, L);
    float invLen = inversesqrt(d2);
    vec3  Ln     = L * invLen;
    float dist   = d2 * invLen;                          // |L|
    float atten  = latt.x + latt.y * dist + latt.z * d2; // c + l*d + q*d^2
    float invAtt = 1.0 / atten;
    float ndl    = max(dot(wN, Ln), 0.0);
    col += (lamb.rgb * invAtt) * uAmbScale.rgb;          // ambient  (LIT.x = 1)
    col += (ldif.rgb * (ndl * invAtt)) * uDifScale.rgb;  // diffuse  (LIT.y = max(N.L,0))
}

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    vec4 eye  = gl_ModelViewMatrix * gl_Vertex;          // eye-space position
    vec3 eyeN = mat3(gl_ModelViewMatrix) * gl_Normal;    // eye-space normal (ARB: DP3 on modelview rows)

    gl_TexCoord[0] = gl_MultiTexCoord0;   // diffuse UV
    gl_TexCoord[1] = gl_MultiTexCoord1;   // lightmap UV
    // tc2/tc3 (and wPos.w) the ARB VP passed are dead here: this GLSL program is only
    // the diffuse_main pair and its FS reads none of them. Skipped to save varyings/ALU.

    // eye -> world (only .xyz needed; FS reads P5.xyz)
    vec3 wPos;
    wPos.x = dot(eye, uVI0);
    wPos.y = dot(eye, uVI1);
    wPos.z = dot(eye, uVI2);
    vec3 wN;
    wN.x = dot(eyeN, uVI0.xyz);
    wN.y = dot(eyeN, uVI1.xyz);
    wN.z = dot(eyeN, uVI2.xyz);

    gl_TexCoord[4] = vec4(wN, 0.0);              // world normal (un-normalised; FS normalises after bump add)
    gl_TexCoord[5] = vec4(wPos, 0.0);            // .xyz = world pos (FS reads P5.xyz)
    // Per-vertex linear fog factor, output through the SECONDARY COLOR — the exact register
    // the ARB VP writes (`MUL result.color.secondary.x, ...`) and the ARB FP reads
    // (`fragment.color.secondary`). Mirroring the register (instead of computing fog from a
    // texcoord) makes the GLSL fog get the IDENTICAL wrapper treatment as the ARB path — so
    // whatever the wrapper does to the secondary color (it mangles gl_Fog/gl_FogFragCoord),
    // both paths behave the same. f = clamp((|eye|-start)/(end-start), 0,1) * (fog.color>0).
    float fogF = clamp(1.0 - (length(eye.xyz) * uFogParams.x + uFogParams.y), 0.0, 1.0) * uFogParams.z;
    gl_FrontSecondaryColor = vec4(fogF, fogF, fogF, 1.0);

    // Stock per-vertex 3-light lighting. Init = ARB env[89].xxxw rgb = env[89].x
    // (the affine viewInv bottom row is (0,0,0,1), so this is 0 — but mirror it exactly).
    vec3 col = vec3(uVI3.x);
    addLight(wPos, wN, uL0pos, uL0amb, uL0dif, uL0att, col);
    addLight(wPos, wN, uL1pos, uL1amb, uL1dif, uL1att, col);
    addLight(wPos, wN, uL2pos, uL2amb, uL2dif, uL2att, col);
    col += uAmb.rgb;

    gl_FrontColor = vec4(col, uDifScale.w);   // .w = env[94].w (vertex alpha)
}
