#version 330 core
// Mode 0: 12-triangle Thing-centered box. Mode 1: two-triangle
// screen-facing sphere silhouette at nearest sphere depth (conservative).
// No VBO needed: positions are indexed using gl_VertexID, OpenGL core 3.3.
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection,center_radius;
uniform int proxy_mode;
uniform vec4 rect_ndc; // left, right, bottom, top
uniform float rect_forward; // nearest depth of authored Thing sphere
const vec2 rect_corners[6]=vec2[6](
 vec2(0,0),vec2(1,0),vec2(1,1),
 vec2(0,0),vec2(1,1),vec2(0,1));
const vec3 corners[8]=vec3[8](
 vec3(-1,-1,-1),vec3(1,-1,-1),vec3(1,1,-1),vec3(-1,1,-1),
 vec3(-1,-1,1),vec3(1,-1,1),vec3(1,1,1),vec3(-1,1,1));
const int ids[36]=int[36](
 0,1,2,0,2,3, 4,6,5,4,7,6,
 0,4,5,0,5,1, 1,5,6,1,6,2,
 2,6,7,2,7,3, 3,7,4,3,4,0);
void main(){
    if(proxy_mode==1){
        vec2 corner=rect_corners[gl_VertexID];
        vec2 ndc=mix(rect_ndc.xz,rect_ndc.yw,corner);
        // Same depth convention as the 3D proxy, but CONSTANT at the
        // nearest sphere depth. Center depth can wrongly hide front geometry.
        gl_Position=vec4(ndc*rect_forward,-projection.z,rect_forward);
        return;
    }
    vec3 world=center_radius.xyz+
               center_radius.w*corners[ids[gl_VertexID]];
    vec3 p=world.x*view_right.xyz+
           world.y*view_forward.xyz+
           world.z*view_up.xyz+view_origin.xyz;
    float h=max(projection.x,0.0001);
    float v=h/max(projection.y,0.0001);
    gl_Position=vec4(p.x/h,p.z/v,-projection.z,p.y);
}
