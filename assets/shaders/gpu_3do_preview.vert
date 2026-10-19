#version 330 core
// View-space transform is composed on CPU exactly once per 3DO instance,
// matching JK rdMatrix_Multiply34(camera.orient, meshPlacement).
// Local mesh data remains permanently GPU resident; only 12 floats change.
layout(location=0) in vec3 local_position;
layout(location=1) in vec2 local_uv;
layout(location=2) in vec3 local_normal;
layout(location=3) in float local_static_light;
layout(location=4) in vec3 local_light_normal;
layout(location=5) in vec3 local_face_anchor;
layout(location=6) in float local_extra_light;
layout(location=7) in float local_light_mode;
layout(location=8) in vec3 local_vertex_rgb;
uniform float ambient_light;
uniform float effective_light_mode; // captured per Thing
uniform float attenuation_min;
uniform int num_lights;
uniform vec4 light_position_radius[16]; // local XYZ and JK light radius
uniform float light_intensity[16];
out float jk_light;
out vec3 vertex_rgb;
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
    // Native JK's rdCache stores reciprocal view depth as rhw=1/(32*y).
    // The native OpenGL path reconstructs vp_z=1/rhw=32*y, then writes
    // that value to its linear-depth G-buffer and uses it for fog.
    // SSAO and fog must use exactly the same units for GPU 3DOs; the
    // hardware Z coordinate above intentionally remains unscaled.
    view_depth=32.0*max(y,0.0);
    pixel_uv=local_uv;
    vertex_rgb=local_vertex_rgb;
    // JK: FullyLit=0, NotLit=1, Diffuse=2, Gouraud=3.
    // Per-face ExtraLight and the camera's ambient floor are evaluated
    // before the gamma-1.8 vertex conversion (like the native D3D path).
    float mode=local_light_mode>3.5 ? 1.0
        : min(local_light_mode,effective_light_mode);
    float l=mode>=2.5 ? local_static_light : 0.0;
    if(mode>=1.5) {
        for(int i=0;i<num_lights;i++) {
            vec3 anchor=mode>=2.5
                ? local_position : local_face_anchor;
            vec3 normal=mode>=2.5
                ? local_light_normal : local_normal;
            vec3 delta=light_position_radius[i].xyz-anchor;
            float dist=length(delta);
            float along=dot(normal,delta/max(dist,0.00001));
            float range_distance=mode>=2.5
                ? dist : abs(dot(normal,delta));
            if(range_distance<light_position_radius[i].w && along>0.0) {
                l+=(light_intensity[i]-
                    range_distance*attenuation_min)*along;
            }
            if(l>=1.0) break;
        }
    }
    float bright=mode<0.5 ? 1.0
        : max(clamp(l+local_extra_light,0.0,1.0),ambient_light);
    jk_light=pow(clamp(bright,0.0,1.0),1.8);
    // Match the patched native 3DO normal sideband. Non-Gouraud faces can
    // still carry smooth vertex normals for SSAO; lighting-mode selection
    // must not force the AO G-buffer back to flat polygon normals.
    vec3 vn=view_right.xyz*local_light_normal.x+
            view_forward.xyz*local_light_normal.y+
            view_up.xyz*local_light_normal.z;
    view_normal=normalize(vec3(vn.x*h,vn.z*v,vn.y));
}
