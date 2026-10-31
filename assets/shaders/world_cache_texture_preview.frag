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
layout(location=0) out vec4 out_color;
layout(location=1) out vec4 out_emissive;
layout(location=2) out vec4 out_normal;
layout(location=3) out vec4 out_depth;
void main() {
    vec3 albedo=clamp(texture(albedo_map,material_uv*uv_scale+uv_scroll).rgb*
                      material_tint.rgb,0.0,1.0);
    vec3 lighting=cached_lit_rgb;
    if(cached_light_state.z>2.5 && num_lights>0) {
        vec3 n=normalize(cached_world_normal);
        float added=0.0;
        for(int i=0;i<num_lights;i++) {
            vec3 delta=light_position_radius[i].xyz-cached_world_position;
            float dist=length(delta);
            if(dist>=light_position_radius[i].w || dist<0.000001)continue;
            float facing=max(dot(n,delta/dist),0.0);
            added+=max(light_intensity[i]-dist*light_attenuation,0.0)*facing;
        }
        float base=max(clamp(cached_light_state.x,0.0,1.0),
                       cached_light_state.y);
        float lit=max(clamp(cached_light_state.x+added,0.0,1.0),
                      cached_light_state.y);
        // Match the sector tint applied to static lighting at vertices.
        // Without this, a point light would wash out colored sectors.
        vec3 t=cached_sector_tint;
        vec3 scale=vec3(1.0)+t-0.5*(t.yzx+t.zxy);
        lighting+=pow(clamp(vec3(lit)*scale,0.0,1.0),vec3(1.8))-
                  pow(clamp(vec3(base)*scale,0.0,1.0),vec3(1.8));
    }
    lighting=clamp(lighting,0.0,1.0);
    out_color=vec4(albedo*lighting,1.0);
    out_emissive=vec4(0.0,0.0,0.0,
        dot(lighting,vec3(0.2126,0.7152,0.0722)));
    out_normal=vec4(normalize(cached_view_normal),1.0);
    out_depth=vec4(32.0/max(gl_FragCoord.w,0.00000001),albedo);
}
