#version 330 core
// GPU-first opaque SithWorld G-buffer. Native depth-tested fallback follows.
// Static JK lighting is interpolated at vertices; the dynamic difference
// is evaluated per fragment without double-counting native vertex lights.
// MotS RGB light arrays and extra global light are not implemented yet.
in vec2 material_uv;
in vec3 cached_view_normal;
in vec3 cached_lit_rgb;
in vec3 cached_light_state; // authored+extra, ambient floor, light mode
in vec3 cached_sector_tint; // the face's owning sector, not camera
in vec3 cached_world_position;
in vec3 cached_world_normal;
uniform int num_lights;
uniform float light_attenuation;
uniform vec4 light_position_radius[16];
uniform float light_intensity[16];
uniform sampler2D albedo_map;
uniform vec2 uv_scale,uv_scroll;
uniform vec4 material_tint;
uniform vec4 jk_color_filter,jk_color_tint,jk_color_add_fade;
vec3 world_jk_effect(vec3 raw,vec3 local_sector_tint)
{
    vec3 v=pow(clamp(raw,0.0,1.0),vec3(1.0/1.8));
    if(any(greaterThan(jk_color_filter.xyz,vec3(0.5))))
        v*=step(vec3(0.5),jk_color_filter.xyz);
    vec3 t=clamp(local_sector_tint+jk_color_tint.xyz,
                 vec3(-4.0),vec3(4.0));
    vec3 scale=vec3(1.0)+t-0.5*(t.yzx+t.zxy);
    v=clamp(v*scale,0.0,1.0);
    v=clamp(v+jk_color_add_fade.xyz,0.0,1.0);
    v*=jk_color_add_fade.w;
    return pow(clamp(v,0.0,1.0),vec3(1.8));
}
layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
void main() {
    vec3 albedo=clamp(texture(albedo_map,material_uv*uv_scale+uv_scroll).rgb*
                      material_tint.rgb,0.0,1.0);
    // Match gpu_3do_preview.frag::pixel_jk_diffuse_lighting().
    // Per-vertex authored light and sector effects are interpolated
    // unchanged. ONLY the dynamic-light delta is evaluated per pixel.
    vec3 lighting=cached_lit_rgb;
    if(cached_light_state.z>2.5 && num_lights>0) {
        float base=cached_light_state.x;
        float lit=base;
        vec3 normal=normalize(cached_world_normal);
        for(int i=0;i<num_lights;i++) {
            vec3 delta=light_position_radius[i].xyz-cached_world_position;
            float dist=length(delta);
            float along=dot(normal,delta/max(dist,0.00001));
            // Radius is only a cutoff: real falloff may reach zero first.
            // A dynamic light must never SUBTRACT baked vertex lighting.
            if(dist<light_position_radius[i].w && along>0.0)
                lit+=max(light_intensity[i]-
                         dist*light_attenuation,0.0)*along;
            if(lit>=1.0)break;
        }
        float original_bright=max(clamp(base,0.0,1.0),
                                  cached_light_state.y);
        float lit_bright=max(clamp(lit,0.0,1.0),
                             cached_light_state.y);
        // Absolutely no color change at pixels outside light influence.
        // In particular do not reconstruct baked Gouraud colors from
        // interpolated intensity: the gamma conversion is nonlinear.
        if(lit_bright!=original_bright)
            lighting+=world_jk_effect(vec3(pow(lit_bright,1.8)),
                                      cached_sector_tint)-
                      world_jk_effect(vec3(pow(original_bright,1.8)),
                                      cached_sector_tint);
    }
    lighting=clamp(lighting,0.0,1.0);
    out_color=vec4(albedo*lighting,1.0);
    out_emissive=vec4(0.0,0.0,0.0,
        dot(lighting,vec3(0.2126,0.7152,0.0722)));
    out_normal=vec4(normalize(cached_view_normal),1.0);
    out_depth=vec4(32.0/max(gl_FragCoord.w,0.00000001),albedo);
}
