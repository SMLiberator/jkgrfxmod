#version 330 core
// View-space transform is composed on CPU exactly once per 3DO instance,
// matching JK rdMatrix_Multiply34(camera.orient, meshPlacement).
// Local mesh data remains permanently GPU resident; only 12 floats change.
layout(location=0) in vec3 local_position;
layout(location=1) in vec2 local_uv;
layout(location=2) in vec3 local_normal;
uniform vec4 view_right, view_forward, view_up, view_origin;
uniform vec4 projection; // tan(half horizontal FOV), aspect, inverse far
out float view_depth;
out vec2 pixel_uv;
out vec3 view_normal;
void main() {
    // Preserve rdMatrix_TransformPoint34's additive operand order.
    float x = (view_forward.x*local_position.y+
               view_up.x*local_position.z+
               view_right.x*local_position.x)+view_origin.x;
    float y = (view_forward.y*local_position.y+
               view_up.y*local_position.z+
               view_right.y*local_position.x)+view_origin.y;
    float z = (view_up.z*local_position.z+
               view_forward.z*local_position.y+
               view_right.z*local_position.x)+view_origin.z;
    float h=max(projection.x,0.0001);
    float v=h/max(projection.y,0.0001);
    gl_Position=vec4(x/h,z/v,-projection.z,y);
    view_depth=max(y,0.0);
    pixel_uv=local_uv;
    vec3 vn=view_right.xyz*local_normal.x+
            view_forward.xyz*local_normal.y+
            view_up.xyz*local_normal.z;
    view_normal=normalize(vec3(vn.x*h,vn.z*v,vn.y));
}
