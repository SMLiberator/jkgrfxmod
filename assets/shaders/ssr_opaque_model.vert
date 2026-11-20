#version 330 core
layout(location=0) in vec3 local_position;
layout(location=1) in vec2 local_uv;
layout(location=2) in vec4 local_face_normal_mode;
uniform int packed_vertex_format;
uniform vec4 view_right,view_forward,view_up,view_origin;
uniform vec4 projection;
uniform vec3 camera_origin,camera_right,camera_forward,camera_up;
out vec3 receiver_position,receiver_normal;
out vec2 receiver_uv;
vec3 unpack_normal(vec3 encoded) {
    vec3 value=round(encoded*1023.0)-512.0;
    return max(value,vec3(0.0))/511.0+min(value,vec3(0.0))/512.0;
}
void main() {
    vec3 face_n=packed_vertex_format!=0
        ?unpack_normal(local_face_normal_mode.xyz)
        :local_face_normal_mode.xyz;
    vec3 view=local_position.x*view_right.xyz+
              local_position.y*view_forward.xyz+
              local_position.z*view_up.xyz+view_origin.xyz;
    vec3 vn=face_n.x*view_right.xyz+
            face_n.y*view_forward.xyz+
            face_n.z*view_up.xyz;
    // Match rdMatrix_Multiply34's view-position convention: xyz are
    // right, forward and up, not OpenGL's conventional camera XYZ.
    receiver_position=camera_origin+
        view.x*camera_right+view.y*camera_forward+view.z*camera_up;
    receiver_normal=normalize(vn.x*camera_right+
                             vn.y*camera_forward+vn.z*camera_up);
    receiver_uv=local_uv;
    float h=max(projection.x,0.0001);
    float v=h/max(projection.y,0.0001);
    gl_Position=vec4(view.x/h,view.z/v,-projection.z,view.y);
}
