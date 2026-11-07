# Forward translucent lighting GLSL organization

The native transparency shader (`game_trns_pass.frag`) and GPU-cached
3DO/sprite shader (`gpu_3do_preview.frag`) load shared fragments via the
recursive local `#include` expansion in `renderer/shaders.cpp`.

- `translucent_spotlight_common.glsl`: byte-identical projector uniforms,
  receiver bias, world reconstruction, shadow-map lookup, and forward
  translucent spotlight receiver lighting.
- `translucent_sun_sampling.glsl`: byte-identical light-space coordinate and
  shadow depth-comparison helpers used by the native opaque, native alpha,
  and cached 3DO paths.
- `game_surface_common.glsl`: identical native opaque/alpha surface-normal,
  EnvMap reflection, tangent-frame and Bayer dither functions. Its uniforms
  are declared by the two owning shaders before the include.
- Individual fragment shaders still implement their *own* directional sun
  visibility/atlas policy, sun diffuse composition, and material blending.
  Those functions are **not** safely interchangeable yet.

Keep the include files as ordinary GLSL 3.3 snippets (without a `#version`
line); the loader prepends them at their include site. Any change to the
shared helpers affects native and cached alpha at once, so validate both
receiver paths before merging into `development`.
