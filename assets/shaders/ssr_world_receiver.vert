#version 330 core
// Lightweight screen-reflection receiver pass. Reuses the cached SithWorld
// VAO but draws only forward-blended world faces that opt into mode "screen".
layout(location=0) in vec3 world_position;
layout(location=1) in vec2 native_uv;
layout(location=2) in vec3 native_normal;
layout(location=6) in vec2 native_uv_offset;
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection;
out vec3 receiver_position,receiver_normal;
out vec2 receiver_uv;
void main() {
    receiver_position=world_position;
    receiver_normal=native_normal;
    receiver_uv=native_uv+native_uv_offset;
    float x=(view_forward.x*world_position.y+
             view_up.x*world_position.z+
             view_right.x*world_position.x)+view_origin.x;
    float y=(view_forward.y*world_position.y+
             view_up.y*world_position.z+
             view_right.y*world_position.x)+view_origin.y;
    float z=(view_up.z*world_position.z+
             view_forward.z*world_position.y+
             view_right.z*world_position.x)+view_origin.z;
    float h=max(projection.x,0.0001);
    float v=h/max(projection.y,0.0001);
    gl_Position=vec4(x/h,z/v,-projection.z,y);
}
