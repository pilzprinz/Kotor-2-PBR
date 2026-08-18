# ============================================================================
# shadow_receive.inc — directional sun shadow sample (single map).
# Injected wherever a receiver FP has the line "# @include shadow_receive".
# Expansion is done by my_glProgramString (glFunctions.cpp) at program upload.
#
# SINGLE SOURCE for every receiver FP. Edit the shadow sampling HERE, not in each
# shader — the old per-shader copy-paste is what let the shadow paths diverge.
#
# Samples ONE depth map: texture[6], K = env[100..103] (lightK). The caster pass
# renders BOTH cached level geometry AND live characters/models into this one map
# each frame (last frame's complete map is sampled — 1-frame ping-pong lag, hidden
# by the soft 4-tap edge). 4-tap PCF + IGN dither softens the edge and the lag.
#
# CONTRACT — the including FP must already declare (names are fixed):
#   ATTRIB N4 = fragment.texcoord[4];              # world normal
#   ATTRIB P5 = fragment.texcoord[5];              # world position
#   PARAM  tnF = program.env[29];                  # x=str  z=floor  w=bias
#   PARAM  tnG = program.env[30];                  # w=nrmBias
#   PARAM  lightK[4] = { program.env[100..103] };  # K = lightProj * lightView
#   PARAM  pcfStep   = program.env[32];            # 1 texel of the shadow map
#   TEMP   shClip, tmp, shZ, shUV, shUV2, shDepth, shadowed;
#
# OUTPUTS the FP then applies:
#   shClip.x   = raw lit factor (1=lit, 0=occluded), BEFORE floor/strength
#                — feed the global sun-shadow contrast pass.
#   shadowed.x = lit factor AFTER floor + strength — multiply onto sun diffuse.
# ============================================================================
MAD shClip.xyz, N4, tnG.w, P5;       # normal-offset receiver bias (cuts acne)
MOV shClip.w, 1.0;

# --- project into light clip space (K = env[100..103] via lightK) ---
DP4 tmp.x, lightK[0], shClip;
DP4 tmp.y, lightK[1], shClip;
DP4 tmp.z, lightK[2], shClip;
# Ortho projection (M4Ortho) => clip.w == 1 exactly, so the perspective divide is a
# no-op and is dropped. tmp.xyz are already the light-clip/NDC coords.
MAD shUV.xy, tmp, 0.5, 0.5;
# Per-pixel IGN dither: scatter PCF banding into fine noise. Two hashes (x,y use
# swapped const pairs); the frac->scramble->frac chain runs once on .xy.
MUL shUV2.x, fragment.position.x, 0.06711056;
MAD shUV2.x, fragment.position.y, 0.00583715, shUV2.x;
MUL shUV2.y, fragment.position.x, 0.00583715;
MAD shUV2.y, fragment.position.y, 0.06711056, shUV2.y;
FRC shUV2.xy, shUV2;
MUL shUV2.xy, shUV2, 52.9829189;
FRC shUV2.xy, shUV2;
MAD shUV2.xy, shUV2, 2.0, -1.0;
MUL shUV2.xy, shUV2, pcfStep;
ADD shUV.xy, shUV, shUV2;
MAD shZ.x, tmp.z, 0.5, 0.5;
MIN shZ.x, shZ.x, 0.99;
MAX shZ.x, shZ.x, 0.0;
# UV-bounds (outside [0,1]^2 -> lit). Vectorised: tmp.x = 1 iff shUV in [0,1]^2.
SGE tmp.xy, shUV, 0.0;
SGE shUV2.xy, 1.0, shUV;
MUL tmp.xy, tmp, shUV2;
MUL tmp.x, tmp.x, tmp.y;
# Z gate: a receiver beyond the light far plane (skybox/backdrop, tz>1) projects to
# depth 0.99 and would read as occluded by every nearer scene texel -> wrongly shadowed
# (the sky darkening/vanishing per camera angle). tmp.z still holds light-NDC z here
# (overwritten on the next line); fold "tz<=1" into the in-bounds factor so the sky stays
# lit. (tz<-1, in front of near, already reads lit via the shZ clamp to 0.)
SGE tmp.w, 1.0, tmp.z;
MUL tmp.x, tmp.x, tmp.w;
SUB tmp.z, shZ.x, tnF.w;
# 4-tap PCF (diagonal)
ADD shUV2.xy, shUV, pcfStep;
TEX shDepth, shUV2, texture[6], 2D;
SGE shadowed.x, shDepth.x, tmp.z;
SUB shUV2.xy, shUV, pcfStep;
TEX shDepth, shUV2, texture[6], 2D;
SGE tmp.w, shDepth.x, tmp.z;
ADD shadowed.x, shadowed.x, tmp.w;
MUL shUV2.x, pcfStep.x, -1.0;
MOV shUV2.y, pcfStep.y;
ADD shUV2.xy, shUV, shUV2;
TEX shDepth, shUV2, texture[6], 2D;
SGE tmp.w, shDepth.x, tmp.z;
ADD shadowed.x, shadowed.x, tmp.w;
MOV shUV2.x, pcfStep.x;
MUL shUV2.y, pcfStep.y, -1.0;
ADD shUV2.xy, shUV, shUV2;
TEX shDepth, shUV2, texture[6], 2D;
SGE tmp.w, shDepth.x, tmp.z;
ADD shadowed.x, shadowed.x, tmp.w;
MUL shadowed.x, shadowed.x, 0.25;
LRP shadowed.x, tmp.x, shadowed.x, 1.0;   # out of bounds -> lit

MOV shClip.x, shadowed.x;            # raw lit factor (for global contrast)
SUB tmp.x, 1.0, tnF.z;
MAD shadowed.x, shadowed.x, tmp.x, tnF.z;   # apply floor
LRP shadowed.x, tnF.x, shadowed.x, 1.0;     # apply strength
