---
name: kotor2_glsl_shader_programming
description: Write, port, debug and optimize GLSL shaders for Knights of the Old Republic II (OpenGL renderer). Specializes in converting ARBvp/ARBfp shaders to GLSL while preserving KOTOR 2 rendering behavior.
---

# KOTOR 2 GLSL Shader Programming Skill

## Purpose

Use this skill when working on KOTOR 2 shader mods:
- ARBvp1.0 / ARBfp1.0 → GLSL conversion
- custom vertex and fragment shaders
- PBR additions
- normal mapping
- roughness/metalness/AO/emissive workflows
- lighting, fog, shadows and reflections
- debugging shader compilation or visual differences

This skill is for the original KOTOR 2 OpenGL rendering pipeline, not modern engines.

---

# Core Rules

## Preserve original renderer behavior

When porting shaders:
1. First understand the original ARB shader.
2. Map every:
   - ATTRIB
   - PARAM
   - TEMP
   - TEX instruction
   - math operation
   - output assignment

Do not rewrite the shader from scratch unless explicitly requested.

A visually similar result is not enough:
- texture masks
- alpha behavior
- fog
- lightmaps
- vertex lighting
- material flags
- engine parameters

must remain compatible.

---

# KOTOR 2 Rendering Pipeline

Typical pipeline:

Vertex shader:
- transforms geometry
- outputs world position
- outputs normals
- passes UV coordinates
- passes vertex colors
- provides data required by fragment shader

Fragment shader:
- samples textures
- applies lighting
- combines lightmaps
- calculates PBR effects
- applies fog
- outputs final color

Common data flow:

```

model space
↓
world space
↓
view space
↓
clip space

```

Never mix coordinate spaces.

Normals, positions and vectors used in dot products must exist in the same space.

---

# GLSL Version Compatibility

Prefer GLSL compatible with old OpenGL:

Use:

```

attribute
varying
uniform
gl_Position
gl_FragColor
texture2D()

```

Avoid modern GLSL unless the host renderer explicitly supports it:

```

in
out
layout(location)
texture()
uniform blocks
SSBO

```

KOTOR 2 shaders are not Vulkan/OpenGL 4.x shaders.

---

# ARBfp / ARBvp Translation Rules

## Attributes

ARB:

```

ATTRIB T = fragment.texcoord[0];
ATTRIB v = fragment.color.primary;

```

GLSL:

```

varying vec2 vTexCoord;
varying vec4 vColor;

```

---

## Parameters

ARB:

```

PARAM pbr = program.env[20];

```

GLSL:

```

uniform vec4 pbr;

```

Preserve component meaning:

Example:

```

pbr.x = metallic
pbr.y = roughness
pbr.z = F0
pbr.w = emissive scale

```

Do not rename or reorder without updating engine code.

---

## Texture sampling

ARB:

```

TEX d, T, texture[0], 2D;

```

GLSL:

```

vec4 d = texture2D(diffuseMap, uv);

```

Always verify texture slot mapping.

---

# Shader Mathematics

## Vector operations

GLSL vectors:

```

vec2
vec3
vec4

```

Component access:

```

color.rgb
normal.xy
value.x

```

Swizzling:

```

vec3 a = color.bgr;

````

---

# Vertex Shader Guidelines

Responsibilities:

- transform vertices
- calculate world position
- transform normals
- pass UVs

Example:

```glsl
attribute vec3 vertex;
attribute vec3 normal;
attribute vec2 texcoord;

uniform mat4 modelViewProjection;
uniform mat4 modelMatrix;

varying vec3 worldNormal;
varying vec3 worldPosition;
varying vec2 uv;

void main()
{
    vec4 worldPos = modelMatrix * vec4(vertex,1.0);

    worldPosition = worldPos.xyz;
    worldNormal = normalize(normal);

    uv = texcoord;

    gl_Position =
        modelViewProjection *
        vec4(vertex,1.0);
}
````

---

# Fragment Shader Guidelines

Main operations:

1. Sample textures
2. Decode normal maps
3. Calculate lighting
4. Apply material properties
5. Apply fog
6. Preserve alpha

---

# Normal Mapping

Typical tangent normal decode:

```
normal = texture2D(normalMap,uv).xyz;
normal = normal * 2.0 - 1.0;
```

Always normalize:

```
normal = normalize(normal);
```

Interpolated normals are not unit length.

---

# PBR Support

KOTOR 2 stock shader:

```
diffuse * lighting + fog
```

PBR additions:

## Metallic

Metal:

```
F0 = albedo
```

Dielectric:

```
F0 = 0.04
```

Example:

```glsl
vec3 F0 = mix(
    vec3(0.04),
    albedo,
    metallic
);
```

---

## Schlick Fresnel

Use:

```glsl
float fresnel =
pow(
1.0 - max(dot(N,V),0.0),
5.0
);
```

---

## Roughness

Roughness affects:

* specular width
* reflection blur
* highlight intensity

Do not invert roughness unless texture convention requires it.

---

## AO

AO is usually:

```
finalLighting *= AO;
```

Apply after lighting calculation.

---

## Emissive

Emission is additive:

```
color += emissive * intensity;
```

Do not multiply emissive by lighting.

---

# KOTOR 2 Specific Materials

## World diffuse shader

Typical behavior:

```
diffuse
+
vertex lighting
+
lightmap
+
fog
```

No reflections unless envmap is enabled.

Alpha must remain unchanged:

```
output.a = diffuse.a;
```

Important for:

* foliage
* grates
* transparent cards

---

## Environment reflective shaders

Default behavior:

* diffuse
* normal mapping
* cubemap reflections
* Fresnel
* metallic response

Materials without envmap must not receive reflections.

---

# Optimization Rules

GPU friendly:

Prefer:

```
mix()
step()
smoothstep()
clamp()
```

Avoid:

```
if()
while()
large loops
```

Avoid unnecessary texture samples.

Texture reads are expensive.

---

# Debugging Workflow

When shader is black:

Check:

1. Vertex shader outputs:

```
gl_Position
```

2. UV range:

```
0.0 - 1.0
```

3. Uniforms actually provided.

4. Texture slots.

5. Coordinate spaces.

---

When porting ARB → GLSL:

Create a checklist:

[ ] every ATTRIB converted
[ ] every PARAM converted
[ ] every TEMP variable mapped
[ ] every TEX preserved
[ ] alpha behavior preserved
[ ] fog preserved
[ ] lighting order preserved
[ ] output color matches original

---

# Common KOTOR 2 Shader Bugs

## Shader compiles but looks wrong

Usually caused by:

* wrong normal space
* missing normalize()
* swapped texture slots
* inverted roughness
* wrong alpha handling
* wrong env flag logic

---

## Do not invent engine features

Never assume:

* Unity syntax
* Unreal material system
* modern OpenGL features

Only use:

* GLSL supported by the KOTOR 2 renderer
* data actually supplied by the engine
* parameters visible in original shaders

---

# Working Style

When modifying existing shaders:

1. Read the complete shader.
2. Explain what each block does.
3. Make minimal changes.
4. Keep comments explaining original ARB behavior.
5. Compare output against stock shader.
6. Test compilation before suggesting further changes.

```

Я бы ещё добавил этот skill в `.cline/skills/kotor2_glsl_shader_programming/SKILL.md` и в `cline_rules` сделать отдельное правило:

```

Always load kotor2_glsl_shader_programming skill before editing any .glsl, .arbfp, .arbvp shader files.

```