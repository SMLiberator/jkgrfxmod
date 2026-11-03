# 8-bit MAT fullbright texels (GPU-cached renderer)

Original indexed MATs retain palette indices, but JK's Direct3D upload
converts their pixels to RGB565/RGBA5551. Reversing that conversion to
identify fullbright palette entries is ambiguous and inaccurate.

For an original 8-bit MAT, the GPU renderer reads the read-only source
rdTexture indexed mip through a validated native tVBuffer prefix. It
inspects all 64 entries of each palette index's CMP light ramp: a texel
is fullbright exactly when its resulting RGB does not change at any
lighting level. Equivalent palette RGB values count, but transparent
pixels never glow.

A cached 0/1 texture mask follows normal MAT UVs, scrolling and CEL
changes. It prevents baked/dynamic diffuse shading, SSAO, sunlight,
projected lighting and added specular on those source texels, without
turning them into HDR point lights. Existing custom emissive maps
remain independent. Materials without fullbright source texels have
no additional GPU texture sample.

Masks are cached per Direct3D texture upload and active CMP pointer,
and discarded when the game reuses/reloads a native texture handle.
Shader unit 11 is reserved for these masks on ordinary model/world
geometry. Projected decals already use unit 11 for AO; masks are
disabled during their separate pass.

GeoMode 3 solid colors preserve their existing fullbright behavior.
Sky and Camera HUD are unchanged. For replacement albedos or unverifiable
8-bit source memory, the renderer conservatively preserves current
normal lighting rather than guessing from RGB colors.

The native tVBuffer prefix comes from studying OpenJKDF2. Runtime
pointers and rdTexture layouts use JKGM's independently validated
custom-executable mappings, not OpenJKDF2's binary addresses.

## In-game verification

Use a mixed 8-bit MAT with both fullbright and normal pixels. Test it
on a cached 3DO and a world face in darkness and under dynamic light,
sunlight, and SSAO. Check CEL animation, UV scrolling, alpha cutouts,
texture filtering, level changes, and custom emissive replacements.
Bright pixels should keep their unlit color, not become extra HDR
light sources. If the mask doesn't appear, validate JKGM's native
tVBuffer source pointer and pixel format against the user's executable.
