#version 120
// Faithful GLSL port of vp_skinned_lit_fog (ARB) — skinned characters without envmap.
// Pairs with fp_model_diff_simple / fp_model_diff_nolm / fp_model_armor_legacy / fp_model_headgear_legacy.
// Outputs world normal→tc4, world pos→tc5 for shadow receive. No cube reflect.

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

uniform vec4 uBone[51];
uniform vec4 uBoneCfg;    // env[16]
attribute vec4 aWeight;   // generic attrib[1] = bone weights
attribute vec4 aBoneIdx;  // generic attrib[4] = bone indices × env[16].z

// off = bone index × env[16].z (the array offset, == ARB A0 after ARL).
// Returns uBone[off + row], mirroring ARB boneArray[A0 + row]. The caller
// passes the SCALED index (aBoneIdx * uBoneCfg.z); do NOT multiply by 3 again
// here — that reads uBone[off*3 + row] = wrong bone → limbs fold inward.
vec4 getBoneRow(int off, int row)
{
    if (off == 0) { if (row == 0) return uBone[0];  else if (row == 1) return uBone[1];  else return uBone[2]; }
    else if (off == 3) { if (row == 0) return uBone[3];  else if (row == 1) return uBone[4];  else return uBone[5]; }
    else if (off == 6) { if (row == 0) return uBone[6];  else if (row == 1) return uBone[7];  else return uBone[8]; }
    else if (off == 9) { if (row == 0) return uBone[9];  else if (row == 1) return uBone[10]; else return uBone[11]; }
    else if (off == 12) { if (row == 0) return uBone[12]; else if (row == 1) return uBone[13]; else return uBone[14]; }
    else if (off == 15) { if (row == 0) return uBone[15]; else if (row == 1) return uBone[16]; else return uBone[17]; }
    else if (off == 18) { if (row == 0) return uBone[18]; else if (row == 1) return uBone[19]; else return uBone[20]; }
    else if (off == 21) { if (row == 0) return uBone[21]; else if (row == 1) return uBone[22]; else return uBone[23]; }
    else if (off == 24) { if (row == 0) return uBone[24]; else if (row == 1) return uBone[25]; else return uBone[26]; }
    else if (off == 27) { if (row == 0) return uBone[27]; else if (row == 1) return uBone[28]; else return uBone[29]; }
    else if (off == 30) { if (row == 0) return uBone[30]; else if (row == 1) return uBone[31]; else return uBone[32]; }
    else if (off == 33) { if (row == 0) return uBone[33]; else if (row == 1) return uBone[34]; else return uBone[35]; }
    else if (off == 36) { if (row == 0) return uBone[36]; else if (row == 1) return uBone[37]; else return uBone[38]; }
    else if (off == 39) { if (row == 0) return uBone[39]; else if (row == 1) return uBone[40]; else return uBone[41]; }
    else if (off == 42) { if (row == 0) return uBone[42]; else if (row == 1) return uBone[43]; else return uBone[44]; }
    else if (off == 45) { if (row == 0) return uBone[45]; else if (row == 1) return uBone[46]; else return uBone[47]; }
    else if (off == 48) { if (row == 0) return uBone[48]; else if (row == 1) return uBone[49]; else return uBone[50]; }
    else return vec4(0.0);
}

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
    vec4 boneIdx = aBoneIdx * uBoneCfg.z;
    vec4 vWeight = aWeight;

    int i0 = int(boneIdx.x), i1 = int(boneIdx.y), i2 = int(boneIdx.z), i3 = int(boneIdx.w);

    vec4 p0 = vec4(dot(gl_Vertex, getBoneRow(i0,0)), dot(gl_Vertex, getBoneRow(i0,1)), dot(gl_Vertex, getBoneRow(i0,2)), 1.0);
    vec4 p1 = vec4(dot(gl_Vertex, getBoneRow(i1,0)), dot(gl_Vertex, getBoneRow(i1,1)), dot(gl_Vertex, getBoneRow(i1,2)), 1.0);
    vec4 p2 = vec4(dot(gl_Vertex, getBoneRow(i2,0)), dot(gl_Vertex, getBoneRow(i2,1)), dot(gl_Vertex, getBoneRow(i2,2)), 1.0);
    vec4 p3 = vec4(dot(gl_Vertex, getBoneRow(i3,0)), dot(gl_Vertex, getBoneRow(i3,1)), dot(gl_Vertex, getBoneRow(i3,2)), 1.0);
    vec4 skinnedPos = p0*vWeight.x + p1*vWeight.y + p2*vWeight.z + p3*vWeight.w;
    skinnedPos.w = uBoneCfg.w;

    vec3 n0 = vec3(dot(gl_Normal, getBoneRow(i0,0).xyz), dot(gl_Normal, getBoneRow(i0,1).xyz), dot(gl_Normal, getBoneRow(i0,2).xyz));
    vec3 n1 = vec3(dot(gl_Normal, getBoneRow(i1,0).xyz), dot(gl_Normal, getBoneRow(i1,1).xyz), dot(gl_Normal, getBoneRow(i1,2).xyz));
    vec3 n2 = vec3(dot(gl_Normal, getBoneRow(i2,0).xyz), dot(gl_Normal, getBoneRow(i2,1).xyz), dot(gl_Normal, getBoneRow(i2,2).xyz));
    vec3 n3 = vec3(dot(gl_Normal, getBoneRow(i3,0).xyz), dot(gl_Normal, getBoneRow(i3,1).xyz), dot(gl_Normal, getBoneRow(i3,2).xyz));
    vec3 skinnedN = n0*vWeight.x + n1*vWeight.y + n2*vWeight.z + n3*vWeight.w;

    gl_Position = gl_ModelViewProjectionMatrix * skinnedPos;

    vec3 eyeN = mat3(gl_ModelViewMatrix) * skinnedN;
    eyeN = normalize(eyeN);
    vec4 eye = gl_ModelViewMatrix * skinnedPos;

    // Fog
    float fogF = clamp((length(eye.xyz) - gl_Fog.start) * (1.0 / (gl_Fog.end - gl_Fog.start + 0.0001)), 0.0, 1.0);
    gl_FrontSecondaryColor = vec4(fogF, fogF, fogF, 1.0);

    gl_TexCoord[0] = gl_MultiTexCoord0;

    // World-space position + normal
    vec3 wPos;
    wPos.x = dot(eye, uVI0); wPos.y = dot(eye, uVI1); wPos.z = dot(eye, uVI2);
    vec3 wN;
    wN.x = dot(eyeN, uVI0.xyz); wN.y = dot(eyeN, uVI1.xyz); wN.z = dot(eyeN, uVI2.xyz);

    gl_TexCoord[4] = vec4(wN, 0.0);
    gl_TexCoord[5] = vec4(wPos, 0.0);

    // Stock per-vertex 3-light lighting
    vec3 col = vec3(uVI3.x);
    addLight(wPos, wN, uL0pos, uL0amb, uL0dif, uL0att, col);
    addLight(wPos, wN, uL1pos, uL1amb, uL1dif, uL1att, col);
    addLight(wPos, wN, uL2pos, uL2amb, uL2dif, uL2att, col);
    col += uAmb.rgb;

    gl_FrontColor = vec4(col, uDifScale.w);
}