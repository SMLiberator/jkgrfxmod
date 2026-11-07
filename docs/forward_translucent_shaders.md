# Forward translucent lighting GLSL organization

The native transparency shader (`game_trns_pass.frag`) and GPU-cached
3DO/sprite shader (`gpu_3do_preview.frag`) load shared fragments via the
recursive local `#include` expansion in `renderer/shaders.cpp`.

- `translucent_spotlight_common.glsl`: byte-identical projector uniforms,
  receiver bias, world reconstruction, and shadow-map lookup.
- `translucent_sun_sampling.glsl`: byte-identical light-space coordinate and
  shadow depth-comparison helpers.
- Individual fragment shaders still implement their *own* directional sun
  visibility/atlas policy, sun diffuse composition, and material blending.
  Those functions are **not** safely interchangeable yet.

Keep the include files as ordinary GLSL 3.3 snippets (without a `#version`
line); the loader prepends them at their include site. Any change to the
shared helpers affects native and cached alpha at once, so validate both
receiver paths before merging into `development`.
