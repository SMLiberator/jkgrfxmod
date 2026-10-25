#version 330 core
// CPU-clipped shadow triangles lie directly on the native receiver plane.
layout(location=0) in vec3 world_position;
layout(location=1) in vec3 world_plane_normal;
uniform vec3 camera_origin,camera_right,camera_up,camera_forward;
uniform vec2 view_tangents;
flat out vec3 receiver_plane_normal;
flat out vec3 receiver_plane_point;
void main()
{
    vec3 p=world_position-camera_origin;
    float depth=dot(p,camera_forward);
    receiver_plane_normal=world_plane_normal;
    receiver_plane_point=world_position;
    // Same camera convention as gpu_3do_preview.vert. Depth arrives as
    // R16F linear view distance*32 in the resolved opaque G-buffer.
    gl_Position=vec4(dot(p,camera_right)/max(view_tangents.x,0.0001),
                     dot(p,camera_up)/max(view_tangents.y,0.0001),
                     -0.001,depth);
}
