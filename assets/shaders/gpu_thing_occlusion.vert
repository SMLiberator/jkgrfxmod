#version 330 core
// 12-triangle conservative box proxy for JK's Thing-centered model radius.
// No VBO needed: positions are indexed using gl_VertexID, OpenGL core 3.3.
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection,center_radius;
const vec3 corners[8]=vec3[8](
 vec3(-1,-1,-1),vec3(1,-1,-1),vec3(1,1,-1),vec3(-1,1,-1),
 vec3(-1,-1,1),vec3(1,-1,1),vec3(1,1,1),vec3(-1,1,1));
const int ids[36]=int[36](
 0,1,2,0,2,3, 4,6,5,4,7,6,
 0,4,5,0,5,1, 1,5,6,1,6,2,
 2,6,7,2,7,3, 3,7,4,3,4,0);
void main(){
    vec3 world=center_radius.xyz+
               center_radius.w*corners[ids[gl_VertexID]];
    vec3 p=world.x*view_right.xyz+
           world.y*view_forward.xyz+
           world.z*view_up.xyz+view_origin.xyz;
    float h=max(projection.x,0.0001);
    float v=h/max(projection.y,0.0001);
    gl_Position=vec4(p.x/h,p.z/v,-projection.z,p.y);
}
