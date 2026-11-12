#version 330 core
// Deliberately inexpensive secondary-camera capture: one transform and
// baked/ambient lighting, no sun, point lights, GGX, SSAO or recursion.
layout(location=0) in vec3 local_position;
layout(location=1) in vec2 local_uv;
layout(location=3) in vec3 native_light; // world RGB or 3DO static light in .x
layout(location=6) in vec2 native_uv_offset; // world only
layout(location=8) in vec3 native_vertex_rgb; // 3DO only
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection; // tan(half FOV), aspect, inverse far, unused
uniform int is_model;
out vec2 pixel_uv;
out vec3 baked_light;
out float clip_side;
uniform vec4 clip_plane_view;
void main() {
    vec3 p=local_position.x*view_right.xyz+
           local_position.y*view_forward.xyz+
           local_position.z*view_up.xyz+view_origin.xyz;
    float half_h=max(projection.x,0.0001);
    float half_v=half_h/max(projection.y,0.0001);
    gl_Position=vec4(p.x/half_h,p.z/half_v,-projection.z,p.y);
    pixel_uv=local_uv+(is_model!=0 ? vec2(0.0):native_uv_offset);
    baked_light=is_model!=0
        ? max(native_vertex_rgb,vec3(0.0))*
          clamp(vec3(0.65+native_light.x*0.35),vec3(0.35),vec3(1.5))
        : clamp(native_light,vec3(0.45),vec3(1.5));
    clip_side=dot(clip_plane_view,vec4(p,1.0));
}
