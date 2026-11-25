#version 330 core
// Lightweight planar capture. World lighting uses the SAME per-vertex
// JK gamma/CMP/sector color math as world_cache.vert, before interpolation.
layout(location=0) in vec3 local_position;
layout(location=1) in vec2 local_uv;
layout(location=2) in vec4 model_face_normal_mode; // packed 2-bit mode lives in W
layout(location=3) in vec3 native_light;
layout(location=4) in vec3 native_light_state; // world light state / model normal
layout(location=5) in vec3 native_sector_tint; // world tint / model face anchor
layout(location=6) in vec2 native_uv_offset; // world UV offset / model face extra
layout(location=7) in vec3 native_cmp_or_mode; // world CMP / model light mode
layout(location=8) in vec3 native_vertex_rgb; // 3DO only
layout(location=9) in float local_face_descriptor;
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection;
uniform int is_model;
uniform int capture_sky_mode; // world aperture at far depth
uniform int capture_unlit; // CameraToMatEx: skip native/JK baked lighting
uniform int packed_vertex_format;
uniform samplerBuffer gpu_3do_face_descriptors;
uniform int world_surface_descriptor_mode;
uniform samplerBuffer world_surface_descriptors;
uniform samplerBuffer world_sector_descriptors;
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
    else gl_Position=vec4(p.x/half_h,p.z/half_v,
        capture_sky_mode!=0 && is_model==0
            ?p.y*(1.0-0.000002):-projection.z,p.y);
    capture_forward_depth=p.y;
    vec4 world_d0=vec4(0.0),world_d1=vec4(0.0),world_d2=vec4(0.0);
    if(is_model==0 && world_surface_descriptor_mode!=0) {
        // World cache attribute 8 is the face ID; model attribute 8 is RGB.
        int world_id=int(native_vertex_rgb.x+0.5);
        if(world_surface_descriptor_mode==2) {
            world_d0=texelFetch(world_surface_descriptors,2*world_id);
            world_d1=texelFetch(world_surface_descriptors,2*world_id+1);
            int sector_id=int(world_d1.y+0.5);
            vec4 sector_tint=texelFetch(world_sector_descriptors,2*sector_id);
            vec4 sector_cmp=texelFetch(world_sector_descriptors,2*sector_id+1);
            world_d2=sector_cmp;
            float scroll_y=world_d1.x;
            world_d1.xyz=sector_tint.xyz;
            world_d1.w=scroll_y;
        } else {
            world_d0=texelFetch(world_surface_descriptors,3*world_id);
            world_d1=texelFetch(world_surface_descriptors,3*world_id+1);
            world_d2=texelFetch(world_surface_descriptors,3*world_id+2);
        }
    }
    pixel_uv=local_uv+(is_model!=0 ? vec2(0.0):
        world_surface_descriptor_mode!=0
            ?vec2(world_d0.w,world_d1.w):native_uv_offset);
    if(capture_unlit!=0) {
        // Preserve material albedo/tint while ignoring world, vertex,
        // colormap and sector lighting (including ambient attenuation).
        baked_light=vec3(1.0);
    } else if(is_model==0) {
        baked_light=jk_capture_color(native_light,
            world_surface_descriptor_mode!=0?world_d2.xyz:native_cmp_or_mode,
            world_surface_descriptor_mode!=0?world_d1.xyz:native_sector_tint);
    } else {
        // Model attributes 3,6,7,8 match gpu_3do_preview.vert. Preserve
        // its authored light-mode/ambient floor, without expensive point lights.
        vec4 face0=vec4(0.0),face1=vec4(0.0);
        if(packed_vertex_format==3) {
            int face_id=int(local_face_descriptor+0.5);
            face0=texelFetch(gpu_3do_face_descriptors,2*face_id);
            face1=texelFetch(gpu_3do_face_descriptors,2*face_id+1);
        }
        float authored_mode=packed_vertex_format==3?face1.w:
            (packed_vertex_format!=0
                ?round(model_face_normal_mode.w*3.0):native_cmp_or_mode.x);
        float mode=min(authored_mode,model_effective_light_mode);
        if(mode>3.5)mode=1.0;
        float raw=clamp((mode>=2.5 ? native_light.x:0.0)+
                        (packed_vertex_format==3?face0.w:native_uv_offset.x),
                        0.0,jk_light_limit);
        float bright=mode<0.5 ? 1.0:max(raw,model_ambient);
        if(jk_light_limit<=1.0)bright=min(bright,1.0);
        baked_light=jk_capture_color(
            vec3(pow(max(bright,0.0),1.8))*native_vertex_rgb,
            model_cmp_tint,model_sector_tint);
    }
    clip_side=dot(clip_plane_view,vec4(p,1.0));
}
