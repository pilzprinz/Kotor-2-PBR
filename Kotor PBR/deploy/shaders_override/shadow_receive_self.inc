# ============================================================================
# shadow_receive_self.inc — directional sun shadow sample, SELF map (model/char).
# Injected wherever a model/char receiver FP has "# @include shadow_receive_self".
#
# Identical to shadow_receive.inc except it samples the SELF map instead of the
# complete map:
#   texture[5]      = THIS frame's in-progress shadow tex (static cache + casters
#                     drawn so far + the receiver's OWN just-written depth).
#   program.env[104..106] = current-frame K (lightProj * lightView).
# Because a model/char writes its own caster depth (BeforeDraw) immediately before
# its color draw, its own depth is present in this map when it shades itself →
# self-shadow is SAME-FRAME → no creep on movement. World receivers use the other
# (complete, last-frame) map via shadow_receive.inc instead.
#
# CONTRACT — same as shadow_receive.inc. The FP still declares lightK (env[100..103]);
# it is unused here (this block reads env[104..106] directly), which is harmless.
#   ATTRIB N4 = fragment.texcoord[4];   ATTRIB P5 = fragment.texcoord[5];
#   PARAM  tnF = program.env[29];        PARAM tnG = program.env[30];
#   PARAM  pcfStep = program.env[32];
#   TEMP   shClip, tmp, shZ, shUV, shUV2, shDepth, shadowed;
#
# OUTPUTS (same as shadow_receive.inc):
#   shClip.x   = raw lit factor (1=lit, 0=occluded), BEFORE floor/strength.
#   shadowed.x = lit factor AFTER floor + strength.
# ============================================================================
MAD shClip.xyz, N4, tnG.w, P5;       # normal-offset receiver bias (cuts acne)
MOV shClip.w, 1.0;

# --- project into light clip space (current-frame K = env[104..106]) ---
DP4 tmp.x, program.env[104], shClip;
DP4 tmp.y, program.env[105], shClip;
DP4 tmp.z, program.env[106], shClip;
# Ortho projection => clip.w == 1 exactly; perspective divide dropped.
MAD shUV.xy, tmp, 0.5, 0.5;
# Per-pixel IGN dither: scatter PCF banding into fine noise.
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
SUB tmp.z, shZ.x, tnF.w;
# 4-tap PCF (diagonal)
ADD shUV2.xy, shUV, pcfStep;
TEX shDepth, shUV2, texture[5], 2D;
SGE shadowed.x, shDepth.x, tmp.z;
SUB shUV2.xy, shUV, pcfStep;
TEX shDepth, shUV2, texture[5], 2D;
SGE tmp.w, shDepth.x, tmp.z;
ADD shadowed.x, shadowed.x, tmp.w;
MUL shUV2.x, pcfStep.x, -1.0;
MOV shUV2.y, pcfStep.y;
ADD shUV2.xy, shUV, shUV2;
TEX shDepth, shUV2, texture[5], 2D;
SGE tmp.w, shDepth.x, tmp.z;
ADD shadowed.x, shadowed.x, tmp.w;
MOV shUV2.x, pcfStep.x;
MUL shUV2.y, pcfStep.y, -1.0;
ADD shUV2.xy, shUV, shUV2;
TEX shDepth, shUV2, texture[5], 2D;
SGE tmp.w, shDepth.x, tmp.z;
ADD shadowed.x, shadowed.x, tmp.w;
MUL shadowed.x, shadowed.x, 0.25;
LRP shadowed.x, tmp.x, shadowed.x, 1.0;   # out of bounds -> lit

MOV shClip.x, shadowed.x;            # raw lit factor (for global contrast)
SUB tmp.x, 1.0, tnF.z;
MAD shadowed.x, shadowed.x, tmp.x, tnF.z;   # apply floor
LRP shadowed.x, tnF.x, shadowed.x, 1.0;     # apply strength
