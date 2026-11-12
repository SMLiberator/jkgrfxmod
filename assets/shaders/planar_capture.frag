#version 330 core
in vec2 pixel_uv;
in vec3 baked_light;
in float clip_side;
uniform sampler2D albedo_map;
uniform vec2 uv_scale,uv_scroll;
uniform vec4 material_tint;
uniform int use_albedo;
uniform float alpha_cutoff;
uniform vec4 fog_params;
uniform vec3 fog_color;
out vec4 out_color;
void main() {
    // The original observer's side of the reflective plane is retained;
    // geometry behind the mirror or below the water is discarded.
    if(clip_side<0.0001)discard;
    vec4 albedo=use_albedo!=0
        ?texture(albedo_map,pixel_uv*uv_scale+uv_scroll)
        :vec4(1.0);
    if(albedo.a*material_tint.a<alpha_cutoff)discard;
    // Fog from the reflected scene to the water/mirror is independent of
    // fog between that receiver and the real camera (applied downstream).
    vec3 lit=albedo.rgb*material_tint.rgb*baked_light;
    float depth=32.0/max(gl_FragCoord.w,0.00000001);
    float fog=clamp((depth-fog_params.x)/
                    max(fog_params.y-fog_params.x,0.0001),0.0,1.0)*
              fog_params.z;
    out_color=vec4(mix(lit,fog_color,fog),1.0);
}
