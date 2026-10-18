#version 330 core
// Prototype: object-space 3DO vertices are transformed by the GPU, rather
// than JK's per-frame CPU clipping/projection + Direct3D execute-buffer path.
layout(location=0) in vec3 local_position;
uniform vec4 model_right, model_forward, model_up, model_origin;
uniform vec4 camera_right, camera_forward, camera_up, camera_origin;
uniform vec4 projection; // tan(half horizontal FOV), aspect, reciprocal far, unused
out float view_depth;
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
    // JK's 16-bit D3D path puts -1/far in homogeneous clip Z.
    // Separate validation required before adopting this in the main shader.
    gl_Position=vec4(x/h,z/v,-projection.z,y);
    view_depth=max(y,0.0);
}
