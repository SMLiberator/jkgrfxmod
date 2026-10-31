#version 330 core
// GPU-first opaque SithWorld G-buffer. Native depth-tested fallback follows.
// JK grayscale per-vertex lighting is updated separately from static world geometry.
// MotS RGB light arrays and extra global light are not implemented yet.
in vec2 material_uv;
in vec3 cached_view_normal;
in vec3 cached_lit_rgb;
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
    out_color=vec4(albedo*clamp(cached_lit_rgb,0.0,1.0),1.0);
    out_emissive=vec4(0.0,0.0,0.0,
        dot(clamp(cached_lit_rgb,0.0,1.0),vec3(0.2126,0.7152,0.0722)));
    out_normal=vec4(normalize(cached_view_normal),1.0);
    out_depth=vec4(32.0/max(gl_FragCoord.w,0.00000001),albedo);
}
