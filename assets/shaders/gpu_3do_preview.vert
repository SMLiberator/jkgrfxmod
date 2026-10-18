#version 330 core
// Mesh-local vertices, normals and face-corner UVs stay GPU resident.
// JK supplies only the live joint/world matrix; all projection is on GPU.
layout(location=0) in vec3 local_position;
layout(location=1) in vec2 local_uv;
layout(location=2) in vec3 local_normal;
uniform vec4 model_right, model_forward, model_up, model_origin;
uniform vec4 camera_right, camera_forward, camera_up, camera_origin;
uniform vec4 projection; // horizontal tan(FOV/2), aspect, 1/far
out float view_depth;
out vec2 pixel_uv;
out vec3 view_normal;
void main() {
    vec3 world = model_origin.xyz
               + model_right.xyz*local_position.x
               + model_forward.xyz*local_position.y
               + model_up.xyz*local_position.z;
    vec3 rel=world-camera_origin.xyz;
    float x=dot(rel,camera_right.xyz);
    float y=dot(rel,camera_forward.xyz);
    float z=dot(rel,camera_up.xyz);
    float h=max(projection.x,0.0001);
    float v=h/max(projection.y,0.0001);
    gl_Position=vec4(x/h,z/v,-projection.z,y);
    view_depth=max(y,0.0);
    pixel_uv=local_uv;
    vec3 nworld=model_right.xyz*local_normal.x
               +model_forward.xyz*local_normal.y
               +model_up.xyz*local_normal.z;
    view_normal=normalize(vec3(dot(nworld,camera_right.xyz)*h,
                               dot(nworld,camera_up.xyz)*v,
                               dot(nworld,camera_forward.xyz)));
}
