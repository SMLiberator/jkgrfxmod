#version 330 core
// GPU-first opaque SithWorld G-buffer. Native depth-tested fallback follows.
// Full native vertex lighting parity is not implemented in this preview.
in vec2 material_uv;
in vec3 cached_view_normal;
uniform sampler2D albedo_map;
uniform vec2 uv_scale,uv_scroll;
uniform vec4 material_tint;
layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
void main() {
    vec3 albedo=clamp(texture(albedo_map,material_uv*uv_scale+uv_scroll).rgb*
                      material_tint.rgb,0.0,1.0);
    out_color=vec4(albedo,1.0);
    out_emissive=vec4(0.0);
    out_normal=vec4(normalize(cached_view_normal),1.0);
    out_depth=vec4(32.0/max(gl_FragCoord.w,0.00000001),albedo);
}
