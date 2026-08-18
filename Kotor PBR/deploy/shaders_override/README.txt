=== shaders_override ===

Put modified ARB shaders here. Filename = friendly name from shader_ident.txt
or raw MD5 hash (with "fp" or "vp" prefix).

Example:
  fp_worldtex_lm_fog_alpha.txt  <- overrides main interior walls (non-env / alpha-aware)
  fp_model_env_fog.txt          <- overrides character/object shader

Each file must be valid ARB fragment program (!!ARBfp1.0) or vertex program (!!ARBvp1.0).

The DLL substitutes these at shader compile time. Remove a file = revert to stock.

See docs/SHADER_REFERENCE.md for the list of identified shaders and their roles.
