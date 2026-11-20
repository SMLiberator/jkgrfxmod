#version 330 core
// Draw only the source reflective world triangles into the CURRENT main
// camera's opaque depth buffer. No lighting, shading or textures.
layout(location=0) in vec3 world_position;
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection;
void main() {
    vec3 p=world_position.x*view_right.xyz+
           world_position.y*view_forward.xyz+
           world_position.z*view_up.xyz+view_origin.xyz;
    float h=max(projection.x,0.0001);
    float v=h/max(projection.y,0.0001);
    gl_Position=vec4(p.x/h,p.z/v,-projection.z,p.y);
}
