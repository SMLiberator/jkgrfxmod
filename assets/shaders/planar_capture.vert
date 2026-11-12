#version 330 core
// Lightweight planar capture. World lighting uses the SAME per-vertex
// JK gamma/CMP/sector color math as world_cache.vert, before interpolation.
layout(location=0) in vec3 local_position;
layout(location=1) in vec2 local_uv;
layout(location=3) in vec3 native_light;
layout(location=4) in vec3 native_light_state; // world light state / model normal
layout(location=5) in vec3 native_sector_tint; // world tint / model face anchor
layout(location=6) in vec2 native_uv_offset; // world UV offset / model face extra
layout(location=7) in vec3 native_cmp_or_mode; // world CMP / model light mode
layout(location=8) in vec3 native_vertex_rgb; // 3DO only
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection;
uniform int is_model;
uniform vec4 clip_plane_view;
uniform vec4 jk_color_filter,jk_color_tint,jk_color_add_fade;
uniform float jk_light_limit;
uniform float model_ambient,model_effective_light_mode;
uniform vec3 model_cmp_tint,model_sector_tint;
out vec2 pixel_uv;
out vec3 baked_light;
out float clip_side;
out float capture_forward_depth;
vec3 jk_capture_color(vec3 raw,vec3 cmp,vec3 sector_tint) {
    vec3 initial=raw*cmp;
    vec3 capped=jk_light_limit<=1.0
        ?clamp(initial,vec3(0.0),vec3(1.0)):max(initial,vec3(0.0));
    vec3 v=pow(capped,vec3(1.0/1.8));
    if(any(greaterThan(jk_color_filter.xyz,vec3(0.5))))
        v*=step(vec3(0.5),jk_color_filter.xyz);
    vec3 t=clamp(jk_color_tint.xyz+sector_tint,vec3(-4.0),vec3(4.0));
    vec3 scale=vec3(1.0)+t-0.5*(t.yzx+t.zxy);
    v=jk_light_limit<=1.0
        ?clamp(v*scale,vec3(0.0),vec3(1.0)):max(v*scale,vec3(0.0));
    v=jk_light_limit<=1.0
        ?clamp(v+jk_color_add_fade.xyz,vec3(0.0),vec3(1.0))
        :max(v+jk_color_add_fade.xyz,vec3(0.0));
    v*=jk_color_add_fade.w;
    v=jk_light_limit<=1.0
        ?clamp(v,vec3(0.0),vec3(1.0)):max(v,vec3(0.0));
    return pow(v,vec3(1.8));
}
void main() {
    vec3 p=local_position.x*view_right.xyz+
           local_position.y*view_forward.xyz+
           local_position.z*view_up.xyz+view_origin.xyz;
    float half_h=max(projection.x,0.0001);
    float half_v=half_h/max(projection.y,0.0001);
    // W=1 selects orthographic projection. W=0 retains planar perspective.
    if(projection.w>0.5)
        gl_Position=vec4(p.x/half_h,p.z/half_v,
                         2.0*p.y*projection.z-1.0,1.0);
    else gl_Position=vec4(p.x/half_h,p.z/half_v,-projection.z,p.y);
    capture_forward_depth=p.y;
    pixel_uv=local_uv+(is_model!=0 ? vec2(0.0):native_uv_offset);
    if(is_model==0) {
        baked_light=jk_capture_color(native_light,native_cmp_or_mode,
                                     native_sector_tint);
    } else {
        // Model attributes 3,6,7,8 match gpu_3do_preview.vert. Preserve
        // its authored light-mode/ambient floor, without expensive point lights.
        float mode=min(native_cmp_or_mode.x,model_effective_light_mode);
        if(mode>3.5)mode=1.0;
        float raw=clamp((mode>=2.5 ? native_light.x:0.0)+
                        native_uv_offset.x,0.0,jk_light_limit);
        float bright=mode<0.5 ? 1.0:max(raw,model_ambient);
        if(jk_light_limit<=1.0)bright=min(bright,1.0);
        baked_light=jk_capture_color(
            vec3(pow(max(bright,0.0),1.8))*native_vertex_rgb,
            model_cmp_tint,model_sector_tint);
    }
    clip_side=dot(clip_plane_view,vec4(p,1.0));
}
