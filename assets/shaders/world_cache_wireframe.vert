#version 330 core
// Same world -> rdCamera -> JK projected depth as gpu_3do_preview.vert.
// Static SithWorld positions are already world space: no model matrix.
layout(location=0) in vec3 world_position;
layout(location=1) in vec2 native_uv;
out vec2 material_uv;
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection; // tan(half horizontal FOV), aspect, inverse far
void main() {
    material_uv=native_uv;
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
