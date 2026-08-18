#version 120
// ============================================================================
// Faithful GLSL port of vp_skinned_env_lit (ARB) — skinned characters/objects
// with envmap. Pairs with fp_model_env_reflective.fs.glsl. Bound via glUseProgram
// override when the engine binds (vp_skinned_env_lit + fp_model_env_reflective).
// See shaders_override/vp_skinned_env_lit.txt for the original ARB source.
//
// Every uniform is read CPU-side from the SAME program.env slot the ARB VP read,
// via glGetProgramEnvParameterfvARB(GL_VERTEX_PROGRAM_ARB, ...) (glsl_program.cpp).
//
// Bone palette: 51 vec4 = 17 bones x 3 rows (env[18..68]). GLSL 1.20
// does NOT allow dynamic indexing of uniform arrays on most drivers, so the
// access is unrolled through getBoneRow() — every branch uses literal indices.
// ============================================================================

// Bone palette: 51 vec4 = 17 bones x 3 rows (env[18..68]).
uniform vec4 uBone[51];
// env[16]: y=envmap reflect scale, z=bone index scale, w=1.0
uniform vec4 uBoneCfg;
// Generic vertex attribs bound via glBindAttribLocation (slot 1 = weights, slot 4 = bone indices).
// KOTOR2 uses glVertexAttribPointerARB with these generic slots; GLSL 1.20 has no built-in
// for them, so we declare named attributes and bind them in GlslLinkProgram before linking.
attribute vec4 aWeight;   // generic attrib[1] = bone weights
attribute vec4 aBoneIdx;  // generic attrib[4] = bone indices × env[16].z
// viewInv (camera->world) rows, from VERTEX env[92,91,90,89].
uniform vec4 uVI0;   // env[92]
uniform vec4 uVI1;   // env[91]
uniform vec4 uVI2;   // env[90]
uniform vec4 uVI3;   // env[89]

// 3 engine point lights (VERTEX env). Per light: pos, ambient, diffuse, atten.
uniform vec4 uL0pos, uL0amb, uL0dif, uL0att;   // env 87,86,85,83
uniform vec4 uL1pos, uL1amb, uL1dif, uL1att;   // env 82,81,80,78
uniform vec4 uL2pos, uL2amb, uL2dif, uL2att;   // env 77,76,75,73
uniform vec4 uAmb;        // env[93] scene ambient add
uniform vec4 uDifScale;   // env[94] global diffuse scale (.w = vertex alpha)
uniform vec4 uAmbScale;   // env[95] global ambient scale

// Fog: x=start y=end z=1/span w=1/span (state.fog.params). ARB computes
// secondary.x = (|eye| - start) * (1/span), clamped by fixed-function.
uniform vec4 uFogParams;

// --------------------------------------------------------------------------
// Unrolled bone-row lookup. off = bone index × env[16].z = array offset
// (== ARB A0 after ARL). Returns uBone[off + row], mirroring ARB boneArray
// [A0 + row]. GLSL 1.20 forbids dynamic indexing of uniform arrays; every
// index here is a compile-time constant, so the compiler emits a fixed-index
// load. The caller passes the SCALED index (aBoneIdx * uBoneCfg.z); do NOT
// multiply by 3 again here — that reads uBone[off*3 + row] = wrong bone,
// which attaches limbs to the wrong matrices → "legs/arms fold inward".
// --------------------------------------------------------------------------
vec4 getBoneRow(int off, int row)
{
    // Bone 0 (off 0)
    if (off == 0) {
        if (row == 0) return uBone[0];  else if (row == 1) return uBone[1];  else return uBone[2];
    }
    // Bone 1 (off 3)
    else if (off == 3) {
        if (row == 0) return uBone[3];  else if (row == 1) return uBone[4];  else return uBone[5];
    }
    // Bone 2 (off 6)
    else if (off == 6) {
        if (row == 0) return uBone[6];  else if (row == 1) return uBone[7];  else return uBone[8];
    }
    // Bone 3 (off 9)
    else if (off == 9) {
        if (row == 0) return uBone[9];  else if (row == 1) return uBone[10]; else return uBone[11];
    }
    // Bone 4 (off 12)
    else if (off == 12) {
        if (row == 0) return uBone[12]; else if (row == 1) return uBone[13]; else return uBone[14];
    }
    // Bone 5 (off 15)
    else if (off == 15) {
        if (row == 0) return uBone[15]; else if (row == 1) return uBone[16]; else return uBone[17];
    }
    // Bone 6 (off 18)
    else if (off == 18) {
        if (row == 0) return uBone[18]; else if (row == 1) return uBone[19]; else return uBone[20];
    }
    // Bone 7 (off 21)
    else if (off == 21) {
        if (row == 0) return uBone[21]; else if (row == 1) return uBone[22]; else return uBone[23];
    }
    // Bone 8 (off 24)
    else if (off == 24) {
        if (row == 0) return uBone[24]; else if (row == 1) return uBone[25]; else return uBone[26];
    }
    // Bone 9 (off 27)
    else if (off == 27) {
        if (row == 0) return uBone[27]; else if (row == 1) return uBone[28]; else return uBone[29];
    }
    // Bone 10 (off 30)
    else if (off == 30) {
        if (row == 0) return uBone[30]; else if (row == 1) return uBone[31]; else return uBone[32];
    }
    // Bone 11 (off 33)
    else if (off == 33) {
        if (row == 0) return uBone[33]; else if (row == 1) return uBone[34]; else return uBone[35];
    }
    // Bone 12 (off 36)
    else if (off == 36) {
        if (row == 0) return uBone[36]; else if (row == 1) return uBone[37]; else return uBone[38];
    }
    // Bone 13 (off 39)
    else if (off == 39) {
        if (row == 0) return uBone[39]; else if (row == 1) return uBone[40]; else return uBone[41];
    }
    // Bone 14 (off 42)
    else if (off == 42) {
        if (row == 0) return uBone[42]; else if (row == 1) return uBone[43]; else return uBone[44];
    }
    // Bone 15 (off 45)
    else if (off == 45) {
        if (row == 0) return uBone[45]; else if (row == 1) return uBone[46]; else return uBone[47];
    }
    // Bone 16 (off 48)
    else if (off == 48) {
        if (row == 0) return uBone[48]; else if (row == 1) return uBone[49]; else return uBone[50];
    }
    // Fallback (should never reach with valid skinned data)
    else return vec4(0.0, 0.0, 0.0, 0.0);
}

// One light's contribution. ARB uses LIT: only LIT.x (=1, ambient) and LIT.y
// (=max(N.L,0), diffuse) consumed; specular discarded. Attenuation
// = 1/(c + l*dist + q*dist^2). World normal NOT renormalised (matches ARB).
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
    // --- Bone-weighted position + normal (4 bones) ---
    // ARB: vertex.attrib[4] = bone indices, vertex.attrib[1] = weights.
    // Conventional GL aliasing: attrib[4] = secondary color, attrib[1] = blend weight.
    // Named attribs bound aBoneIdx→4, aWeight→1 via glBindAttribLocation before link.
    vec4 boneIdx = aBoneIdx * uBoneCfg.z;   // * env[16].z (bone index scale)
    vec4 vWeight = aWeight;                 // generic attrib[1] = weights

    int i0 = int(boneIdx.x);
    int i1 = int(boneIdx.y);
    int i2 = int(boneIdx.z);
    int i3 = int(boneIdx.w);

    // Position: 4 bone transforms (3 rows each), blended by weights.
    vec4 p0 = vec4(dot(gl_Vertex, getBoneRow(i0, 0)),
                   dot(gl_Vertex, getBoneRow(i0, 1)),
                   dot(gl_Vertex, getBoneRow(i0, 2)), 1.0);
    vec4 p1 = vec4(dot(gl_Vertex, getBoneRow(i1, 0)),
                   dot(gl_Vertex, getBoneRow(i1, 1)),
                   dot(gl_Vertex, getBoneRow(i1, 2)), 1.0);
    vec4 p2 = vec4(dot(gl_Vertex, getBoneRow(i2, 0)),
                   dot(gl_Vertex, getBoneRow(i2, 1)),
                   dot(gl_Vertex, getBoneRow(i2, 2)), 1.0);
    vec4 p3 = vec4(dot(gl_Vertex, getBoneRow(i3, 0)),
                   dot(gl_Vertex, getBoneRow(i3, 1)),
                   dot(gl_Vertex, getBoneRow(i3, 2)), 1.0);
    vec4 skinnedPos = p0 * vWeight.x + p1 * vWeight.y + p2 * vWeight.z + p3 * vWeight.w;
    skinnedPos.w = uBoneCfg.w;   // env[16].w = 1.0

    // Normal: 4 bone transforms (3x3 rotation only), blended.
    vec3 n0 = vec3(dot(gl_Normal, getBoneRow(i0, 0).xyz),
                   dot(gl_Normal, getBoneRow(i0, 1).xyz),
                   dot(gl_Normal, getBoneRow(i0, 2).xyz));
    vec3 n1 = vec3(dot(gl_Normal, getBoneRow(i1, 0).xyz),
                   dot(gl_Normal, getBoneRow(i1, 1).xyz),
                   dot(gl_Normal, getBoneRow(i1, 2).xyz));
    vec3 n2 = vec3(dot(gl_Normal, getBoneRow(i2, 0).xyz),
                   dot(gl_Normal, getBoneRow(i2, 1).xyz),
                   dot(gl_Normal, getBoneRow(i2, 2).xyz));
    vec3 n3 = vec3(dot(gl_Normal, getBoneRow(i3, 0).xyz),
                   dot(gl_Normal, getBoneRow(i3, 1).xyz),
                   dot(gl_Normal, getBoneRow(i3, 2).xyz));
    vec3 skinnedN = n0 * vWeight.x + n1 * vWeight.y + n2 * vWeight.z + n3 * vWeight.w;

    // --- Clip-space ---
    gl_Position = gl_ModelViewProjectionMatrix * skinnedPos;

    // --- Eye-space normal (normalized) ---
    vec3 eyeN = mat3(gl_ModelViewMatrix) * skinnedN;
    eyeN = normalize(eyeN);

    // --- Eye-space position ---
    vec4 eye = gl_ModelViewMatrix * skinnedPos;

    // --- Fog: secondary.x = (|eye| - start) * (1/span), clamped [0,1] ---
    // ARB: ADD r1.x, -fogParams.y, |eye|; MUL result.color.secondary.x, r1.x, fogParams.w
    // Fixed-function pipeline clamps secondary color; GLSL has no such stage → clamp here.
    float fogF = clamp((length(eye.xyz) - uFogParams.y) * uFogParams.w, 0.0, 1.0);
    gl_FrontSecondaryColor = vec4(fogF, fogF, fogF, 1.0);

    // --- Diffuse UV ---
    gl_TexCoord[0] = gl_MultiTexCoord0;

    // --- Cube reflect coord (world space) ---
    // ARB (lines 94-105): R = normalize(eye - dot(N,eye) * N * env[16].y)
    // This is NOT reflect() — it uses a variable scale factor (env[16].y), not fixed 2.0.
    float NdE = dot(eyeN, eye.xyz);
    vec3 R = eye.xyz - NdE * eyeN * uBoneCfg.y;   // env[16].y = reflect scale
    R = normalize(R);
    // Transform reflected direction to world space
    vec3 wR;
    wR.x = dot(R, uVI0.xyz);
    wR.y = dot(R, uVI1.xyz);
    wR.z = dot(R, uVI2.xyz);
    gl_TexCoord[1] = vec4(wR, uBoneCfg.w);

    // --- World-space position + normal ---
    vec3 wPos;
    wPos.x = dot(eye, uVI0);
    wPos.y = dot(eye, uVI1);
    wPos.z = dot(eye, uVI2);
    vec3 wN;
    wN.x = dot(eyeN, uVI0.xyz);
    wN.y = dot(eyeN, uVI1.xyz);
    wN.z = dot(eyeN, uVI2.xyz);

    gl_TexCoord[4] = vec4(wN, 0.0);   // world normal
    gl_TexCoord[5] = vec4(wPos, 0.0); // world pos

    // --- Stock per-vertex 3-light lighting (world space, like ARB) ---
    vec3 col = vec3(uVI3.x);   // ARB env[89].xxxw → .x
    addLight(wPos, wN, uL0pos, uL0amb, uL0dif, uL0att, col);
    addLight(wPos, wN, uL1pos, uL1amb, uL1dif, uL1att, col);
    addLight(wPos, wN, uL2pos, uL2amb, uL2dif, uL2att, col);
    col += uAmb.rgb;

    gl_FrontColor = vec4(col, uDifScale.w);   // .w = env[94].w (vertex alpha)
}