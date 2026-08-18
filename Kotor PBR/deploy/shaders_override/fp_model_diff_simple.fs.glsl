#version 120
// Faithful GLSL port of fp_model_diff_simple ARB + shadow_receive_self.inc.
// Pairs with vp_skinned_lit_fog.vs.glsl. Simple diffuse × vertex-light + fog + sun shadow.
// TMU: tex0=0, texShadow=5(SELF)

uniform sampler2D tex0;
uniform sampler2D texShadow;

uniform vec4 tnD; uniform vec4 tnE; uniform vec4 tnF; uniform vec4 tnG; uniform vec4 uPcf;
uniform vec4 uKS0, uKS1, uKS2;
uniform vec4 uFogColor;
uniform vec4 uCamW;      // camera world pos
uniform vec4 uCamLight;   // env[34] x=strength y=exponent z=range w=unused

void main()
{
    vec2 uv = gl_TexCoord[0].xy;
    vec3 v  = gl_Color.rgb;
    vec3 N4 = gl_TexCoord[4].xyz;
    vec3 P5 = gl_TexCoord[5].xyz;

    vec4 d = texture2D(tex0, uv);
    vec3 alb = d.rgb;
    d.rgb *= v;

    vec3 N = normalize(N4);
    vec3 V = normalize(uCamW.xyz - P5);
    float NdotV = max(dot(N, V), 0.0);

    // shadow_receive_self.inc
    vec4 shc = vec4(N4 * tnG.w + P5, 1.0);
    float tx = dot(uKS0, shc);
    float ty = dot(uKS1, shc);
    float tz = dot(uKS2, shc);
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

    // Sun diffuse
    float sd = max(dot(N, tnD.xyz), 0.0) * tnE.x * shadowed;
    d.rgb += (alb * tnE.yzw) * sd;

    // Sun-shadow global contrast
    float nl = min(max(dot(N4, tnD.xyz), 0.0) * 4.0, 1.0);
    float contrast = 1.0 - ((1.0 - shClip) * nl * tnG.x);
    contrast = max(contrast, tnF.z);
    d.rgb *= contrast;

    // --- Camera light + rim ---
    if (uCamLight.x > 0.0) {
        float dist  = length(uCamW.xyz - P5);
        float falloff = max(1.0 - dist / uCamLight.z, 0.0);
        float camSpec = pow(NdotV, uCamLight.y) * uCamLight.x * falloff * 0.5;
        d.rgb += tnE.yzw * camSpec;
    }
    d.rgb = min(d.rgb, 1.0);  // prevent bloom

    // Fog
    float fogAmt = gl_SecondaryColor.r;
    d.rgb = mix(d.rgb, uFogColor.rgb, fogAmt);

    gl_FragColor = d;
}