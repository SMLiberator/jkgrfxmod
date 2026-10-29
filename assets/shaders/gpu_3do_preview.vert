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
uniform int camera_hud_thing; // SithThing type=camera: unlit textured 3DO
// Native JK applies these to RGB8 *vertex colors* before texturing.
uniform vec3 colormap_tint; // nonidentity CMP header RGB multiplier
uniform vec4 jk_color_filter; // XYZ selected channel mask, W active
uniform vec4 jk_color_tint;
uniform vec4 jk_color_add_fade; // RGB additive/255, A fade
uniform float attenuation_min;
uniform int num_lights;
uniform int nonrigid_light_transform; // use transformed distances for scaled R/L/U
uniform vec4 light_position_radius[16]; // local XYZ and JK light radius
uniform float light_intensity[16];
// Native D3D applies the RGB multiplier to each *vertex* after gamma
// conversion, then rasterization interpolates the combined RGB. Multiplying
// separately interpolated light and tint per pixel gives incorrect gradients.
out vec3 jk_lit_rgb;
out vec3 local_fragment_position;
out vec3 local_fragment_normal;
uniform vec4 view_right, view_forward, view_up, view_origin;
uniform vec4 projection; // tan(half horizontal FOV), aspect, inverse far
out vec2 pixel_uv;
out vec3 view_normal;
// Decals reuse the existing light-anchor (xyz) and extra-light (w)
// attributes as normalized projector coordinates and smooth angular alpha.
uniform int decal_receiver_lighting;
out vec4 decal_coverage;
void main() {
    decal_coverage=decal_receiver_lighting!=0
        ? vec4(local_face_anchor,local_extra_light)
        : vec4(0.5,0.5,0.5,1.0);
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
    // GPU fog/SSAO depth is recovered per-fragment from gl_FragCoord.w.
    // Never clamp depth here: vertices of huge faces can lie behind the
    // camera, and clamping before interpolation distorts nearby fog.
    pixel_uv=local_uv;
    local_fragment_position=local_position;
    local_fragment_normal=local_light_normal;
    // A Camera-type Thing is a HUD overlay: no native ambient/dynamic
    // lighting, colormap tint, palette fade or per-camera color effects.
    if(camera_hud_thing!=0) {
        jk_lit_rgb=vec3(1.0);
    } else {
    // JK: FullyLit=0, NotLit=1, Diffuse=2, Gouraud=3.
    // Per-face ExtraLight and the camera's ambient floor are evaluated
    // before the gamma-1.8 vertex conversion (like the native D3D path).
    float mode=local_light_mode>3.5 ? 1.0
        : min(local_light_mode,effective_light_mode);
    float l=mode>=2.5 ? local_static_light : 0.0;
    if(mode>=1.5 && num_lights>0) {
        vec3 anchor=mode>=2.5 ? local_position : local_face_anchor;
        vec3 normal=mode>=2.5 ? local_light_normal : local_normal;
        // The local-space shortcut assumes orthonormal R/L/U. For scaled
        // animation nodes, evaluate distance and incidence in the camera's
        // rigid basis (a rotation of world space). Compute the inverse-
        // transpose normal once per vertex, not once per affecting light.
        if(nonrigid_light_transform!=0) {
            vec3 r=view_right.xyz,f=view_forward.xyz,u=view_up.xyz;
            vec3 cr=cross(f,u),cf=cross(u,r),cu=cross(r,f);
            float determinant=dot(r,cr);
            normal=normalize(sign(determinant)*
                (cr*normal.x+cf*normal.y+cu*normal.z));
        }
        for(int i=0;i<num_lights;i++) {
            vec3 delta=light_position_radius[i].xyz-anchor;
            if(nonrigid_light_transform!=0)
                delta=view_right.xyz*delta.x+
                      view_forward.xyz*delta.y+view_up.xyz*delta.z;
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
    jk_lit_rgb=vec3(pow(clamp(bright,0.0,1.0),1.8))*local_vertex_rgb;
    jk_lit_rgb*=colormap_tint; // before native hardware color effects
    if(jk_color_filter.w>0.5) {
        // Approximate the hardware rdCache per-vertex RGB8 color operation.
        vec3 v=pow(clamp(jk_lit_rgb,0.0,1.0),vec3(1.0/1.8));
        if(any(greaterThan(jk_color_filter.xyz,vec3(0.5))))
            v*=step(vec3(0.5),jk_color_filter.xyz);
        vec3 t=jk_color_tint.xyz;
        vec3 scale=vec3(1.0)+t-0.5*(t.yzx+t.zxy);
        v=clamp(v*scale,0.0,1.0);
        v=clamp(v+jk_color_add_fade.xyz,0.0,1.0);
        v*=jk_color_add_fade.w;
        jk_lit_rgb=pow(clamp(v,0.0,1.0),vec3(1.8));
    }
    }
    // Match the patched native 3DO normal sideband. Non-Gouraud faces can
    // still carry smooth vertex normals for SSAO; lighting-mode selection
    // must not force the AO G-buffer back to flat polygon normals.
    // An animated 3DO node can have non-unit R/L/U columns (including
    // per-axis scale and mirroring). Positions use the native affine matrix,
    // but normals must use its inverse transpose, never the raw columns.
    // The sign of the determinant preserves the orientation of reflected
    // node normals. Fall back for collapsed/singular nodes.
    vec3 r=view_right.xyz, f=view_forward.xyz, u=view_up.xyz;
    vec3 cr=cross(f,u), cf=cross(u,r), cu=cross(r,f);
    float det=dot(r,cr);
    vec3 vn=abs(det)>1e-8
        ? sign(det)*(cr*local_light_normal.x+
                     cf*local_light_normal.y+
                     cu*local_light_normal.z)
        : (r*local_light_normal.x+
           f*local_light_normal.y+
           u*local_light_normal.z);
    vec3 projected=vec3(vn.x*h,vn.z*v,vn.y);
    view_normal=dot(projected,projected)>1e-16
        ? normalize(projected) : vec3(0.0,0.0,1.0);
}
