#version 330 core
// GPU-first opaque SithWorld G-buffer. Native depth-tested fallback follows.
// Static JK lighting is evaluated at vertices and interpolated in RGB.
// In pixel mode, a shared GLSL dynamic-light contribution is composed per
// fragment without double-counting native per-vertex dynamic lights.
// MotS RGB light arrays are not yet decoded; JK global extra is sampled.
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
uniform int solid_color; // GeoMode 3: native MAT/CMP RGB, no texture sampling
uniform int alpha_cutout; // explicit cutout MATs on solid adjoins
uniform float alpha_cutoff;
uniform vec4 jk_color_filter,jk_color_tint,jk_color_add_fade;

#include "jk_point_lighting.glsl"

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
    // Opaque grates need real holes in both depth and G-buffer color.
    // Sample the MAT once and discard *before* writing any attachment.
    vec4 texel=vec4(1.0);
    if(solid_color==0)
        texel=texture(albedo_map,material_uv*uv_scale+uv_scroll);
    if(alpha_cutout!=0 && texel.a*material_tint.a<alpha_cutoff)
        discard;
    vec3 albedo=solid_color!=0 ? clamp(material_tint.rgb,0.0,1.0) :
        clamp(texel.rgb*material_tint.rgb,0.0,1.0);
    // Keep authored Gouraud/sector lighting, already interpolated in RGB.
    // The same GLSL helpers calculate pixel light for both world and 3DO.
    vec3 lighting=cached_lit_rgb;
    if(cached_light_state.z>2.5 && num_lights>0) {
        float added=0.0;
        vec3 normal=normalize(cached_world_normal);
        for(int i=0;i<num_lights;i++) {
            vec3 delta=light_position_radius[i].xyz-cached_world_position;
            added+=jk_point_sample(delta,normal,light_position_radius[i].w,
                                   light_intensity[i],light_attenuation,
                                   false);
            if(added>=1.0)break;
        }
        if(added>0.0) {
            vec3 light_color=max(
                world_jk_effect(vec3(1.0),cached_sector_tint)-
                world_jk_effect(vec3(0.0),cached_sector_tint),vec3(0.0));
            lighting=jk_point_compose(lighting,added,light_color);
        }
    }
    lighting=clamp(lighting,0.0,1.0);
    out_color=vec4(albedo*lighting,1.0);
    out_emissive=vec4(0.0,0.0,0.0,
        dot(lighting,vec3(0.2126,0.7152,0.0722)));
    out_normal=vec4(normalize(cached_view_normal),1.0);
    out_depth=vec4(32.0/max(gl_FragCoord.w,0.00000001),albedo);
}
