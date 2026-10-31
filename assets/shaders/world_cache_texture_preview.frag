#version 330 core
// Diagnostic overlay only. Native world remains visible and writes depth;
// this pass checks the cache's MAT texture, source UVs and camera projection.
in vec2 material_uv;
uniform sampler2D albedo_map;
uniform vec2 uv_scale,uv_scroll;
uniform vec4 material_tint;
layout(location=0) out vec4 out_color;
void main() {
    vec3 pigment=texture(albedo_map,material_uv*uv_scale+uv_scroll).rgb *
                 material_tint.rgb;
    // Distinct translucent fullbright preview: source texture is recognizable
    // while native baked lighting remains visible underneath.
    const float coverage=0.62;
    out_color=vec4(clamp(pigment,0.0,1.0)*coverage,coverage);
}
