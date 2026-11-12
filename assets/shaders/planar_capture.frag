#version 330 core
in vec2 pixel_uv;
in vec3 baked_light;
in float clip_side;
uniform sampler2D albedo_map;
uniform vec2 uv_scale,uv_scroll;
uniform vec4 material_tint;
uniform int use_albedo;
uniform float alpha_cutoff;
out vec4 out_color;
void main() {
    // The original observer's side of the reflective plane is retained;
    // geometry behind the mirror or below the water is discarded.
    if(clip_side<0.0001)discard;
    vec4 albedo=use_albedo!=0
        ?texture(albedo_map,pixel_uv*uv_scale+uv_scroll)
        :vec4(1.0);
    if(albedo.a*material_tint.a<alpha_cutoff)discard;
    // No expensive material lighting, no reflection recursion.
    out_color=vec4(albedo.rgb*material_tint.rgb*baked_light,1.0);
}
