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
uniform vec3 thing_sector_tint_delta; // own SithSector - camera SithSector
uniform vec4 jk_color_add_fade; // RGB additive/255, A fade
uniform float attenuation_min;
uniform int num_lights;
uniform int pixel_dynamic_lighting; // fragment handles JK diffuse when enabled
uniform int nonrigid_light_transform; // use transformed distances for scaled R/L/U
uniform vec4 light_position_radius[16]; // local XYZ and JK light radius
uniform float light_intensity[16];

// Native D3D applies the RGB multiplier to each *vertex* after gamma
// conversion, then rasterization interpolates the combined RGB. Multiplying
// separately interpolated light and tint per pixel gives incorrect gradients.
out vec3 jk_lit_rgb;
out vec3 local_fragment_position;
out vec3 local_fragment_normal;
// Raw JK vertex attributes for opt-in smooth per-fragment light response.
// Separate from jk_lit_rgb so native Gouraud remains the default/fallback.
out vec3 pixel_light_face_anchor;
out vec3 pixel_light_face_normal;
out vec3 pixel_light_vertex_rgb;
out vec3 pixel_light_state; // static light, face extra light, mode
uniform vec4 view_right, view_forward, view_up, view_origin;
// Visible SPRs: GL 3.3 gl_InstanceID selects one cached quad transform
// from a packed uniform array. Ordinary models continue to use view_*.
uniform int sprite_instance_count;
uniform vec4 sprite_instance_matrix[128]; // 32 instances * (right, forward, up, origin)
uniform vec4 projection; // tan(half horizontal FOV), aspect, inverse far
out vec2 pixel_uv;
out vec3 view_normal;
// Decals reuse the existing light-anchor (xyz) and extra-light (w)
// attributes as normalized projector coordinates and smooth angular alpha.
uniform int decal_receiver_lighting;
out vec4 decal_coverage;
// Four-way sector crossing is opt-in and only active on models actually
// overlapping multiple native Adjoin-linked sectors. The ordinary
// single-sector path stays bit-for-bit unchanged.
uniform int sector_crossing_count;
uniform vec4 sector_crossing_info[4]; // x: ambient floor
uniform vec4 sector_crossing_cmp[4];  // xyz: CMP RGB multiplier
uniform vec4 sector_crossing_tint[4]; // xyz: sector-only color tint delta
out vec3 sector_crossing_lit[4];
void main() {
    vec4 model_right=view_right;
    vec4 model_forward=view_forward;
    vec4 model_up=view_up;
    vec4 model_origin=view_origin;
    if(sprite_instance_count>0) {
        int base=gl_InstanceID*4;
        model_right=sprite_instance_matrix[base];
        model_forward=sprite_instance_matrix[base+1];
        model_up=sprite_instance_matrix[base+2];
        model_origin=sprite_instance_matrix[base+3];
    }
    decal_coverage=decal_receiver_lighting!=0
        ? vec4(local_face_anchor,local_extra_light)
        : vec4(0.5,0.5,0.5,1.0);
    // Preserve rdMatrix_TransformPoint34's additive operand order.
    float x = (model_forward.x*local_position.y+
               model_up.x*local_position.z+
               model_right.x*local_position.x)+model_origin.x;
    float y = (model_forward.y*local_position.y+
               model_up.y*local_position.z+
               model_right.y*local_position.x)+model_origin.y;
    float z = (model_up.z*local_position.z+
               model_forward.z*local_position.y+
               model_right.z*local_position.x)+model_origin.z;
    float h=max(projection.x,0.0001);
    float v=h/max(projection.y,0.0001);
    gl_Position=vec4(x/h,z/v,-projection.z,y);
    // GPU fog/SSAO depth is recovered per-fragment from gl_FragCoord.w.
    // Never clamp depth here: vertices of huge faces can lie behind the
    // camera, and clamping before interpolation distorts nearby fog.
    pixel_uv=local_uv;
    local_fragment_position=local_position;
    local_fragment_normal=local_light_normal;
    pixel_light_face_anchor=local_face_anchor;
    pixel_light_face_normal=local_normal;
    pixel_light_vertex_rgb=local_vertex_rgb;
    pixel_light_state=vec3(local_static_light,local_extra_light,local_light_mode);
    // A Camera-type Thing is a HUD overlay: no native ambient/dynamic
    // lighting, colormap tint, palette fade or per-camera color effects.
    if(camera_hud_thing!=0) {
        jk_lit_rgb=vec3(1.0);
    } else {
    // Always compute the baked/static vertex result here, including the
    // nonlinear gamma/color effects BEFORE raster interpolation. Moving
    // this work to the fragment shader changes baked lighting on distant
    // models even when a point light makes no actual contribution.
    // Pixel mode suppresses only the dynamic-light loop below.
    // JK: FullyLit=0, NotLit=1, Diffuse=2, Gouraud=3.
    // Per-face ExtraLight and the camera's ambient floor are evaluated
    // before the gamma-1.8 vertex conversion (like the native D3D path).
    float mode=local_light_mode>3.5 ? 1.0
        : min(local_light_mode,effective_light_mode);
    float l=mode>=2.5 ? local_static_light : 0.0;
    if(mode>=1.5 && num_lights>0 && pixel_dynamic_lighting==0) {
        vec3 anchor=mode>=2.5 ? local_position : local_face_anchor;
        vec3 normal=mode>=2.5 ? local_light_normal : local_normal;
        // The local-space shortcut assumes orthonormal R/L/U. For scaled
        // animation nodes, evaluate distance and incidence in the camera's
        // rigid basis (a rotation of world space). Compute the inverse-
        // transpose normal once per vertex, not once per affecting light.
        if(nonrigid_light_transform!=0) {
            vec3 r=model_right.xyz,f=model_forward.xyz,u=model_up.xyz;
            vec3 cr=cross(f,u),cf=cross(u,r),cu=cross(r,f);
            float determinant=dot(r,cr);
            normal=normalize(sign(determinant)*
                (cr*normal.x+cf*normal.y+cu*normal.z));
        }
        for(int i=0;i<num_lights;i++) {
            vec3 delta=light_position_radius[i].xyz-anchor;
            if(nonrigid_light_transform!=0)
                delta=model_right.xyz*delta.x+
                      model_forward.xyz*delta.y+model_up.xyz*delta.z;
            // Disabled pixel lighting must reproduce JK-style vertex
            // Gouraud, independent of edits to jk_point_lighting.glsl.
            float dist=length(delta);
            float along=dot(normal,delta/max(dist,0.00001));
            float range_distance=mode>=2.5
                ? dist : abs(dot(normal,delta));
            if(range_distance<light_position_radius[i].w && along>0.0)
                l+=max(light_intensity[i]-
                       range_distance*attenuation_min,0.0)*along;
            if(l>=1.0) break;
        }
    }
    float bright=mode<0.5 ? 1.0
        : max(clamp(l+local_extra_light,0.0,1.0),ambient_light);
    jk_lit_rgb=vec3(pow(clamp(bright,0.0,1.0),1.8))*local_vertex_rgb;
    jk_lit_rgb*=colormap_tint; // before native hardware color effects
    if(jk_color_filter.w>0.5 ||
       any(greaterThan(abs(thing_sector_tint_delta),vec3(0.00001)))) {
        // Approximate the hardware rdCache per-vertex RGB8 color operation.
        vec3 v=pow(clamp(jk_lit_rgb,0.0,1.0),vec3(1.0/1.8));
        if(any(greaterThan(jk_color_filter.xyz,vec3(0.5))))
            v*=step(vec3(0.5),jk_color_filter.xyz);
        vec3 t=clamp(jk_color_tint.xyz+thing_sector_tint_delta,
                     vec3(-4.0),vec3(4.0));
        vec3 scale=vec3(1.0)+t-0.5*(t.yzx+t.zxy);
        v=clamp(v*scale,0.0,1.0);
        v=clamp(v+jk_color_add_fade.xyz,0.0,1.0);
        v*=jk_color_add_fade.w;
        jk_lit_rgb=pow(clamp(v,0.0,1.0),vec3(1.8));
    }
    // Re-run JK's vertex gamma/color effects ONLY for crossing Things,
    // once per candidate sector. This retains interpolation of gamma-
    // corrected vertex RGB, unlike recomputing baked brightness per pixel.
    if(sector_crossing_count>1) {
        for(int i=0;i<4;i++) {
            if(i>=sector_crossing_count)break;
            if(i==0) {sector_crossing_lit[i]=jk_lit_rgb;continue;}
            float sector_bright=mode<0.5 ? 1.0 :
                max(clamp(l+local_extra_light,0.0,1.0),
                    sector_crossing_info[i].x);
            vec3 candidate=vec3(pow(
                clamp(sector_bright,0.0,1.0),1.8))*local_vertex_rgb;
            candidate*=sector_crossing_cmp[i].xyz;
            if(jk_color_filter.w>0.5 ||
               any(greaterThan(abs(sector_crossing_tint[i].xyz),
                               vec3(0.00001)))) {
                vec3 encoded=pow(clamp(candidate,0.0,1.0),
                                 vec3(1.0/1.8));
                if(any(greaterThan(jk_color_filter.xyz,vec3(0.5))))
                    encoded*=step(vec3(0.5),jk_color_filter.xyz);
                vec3 tint=clamp(jk_color_tint.xyz+
                                sector_crossing_tint[i].xyz,
                                vec3(-4.0),vec3(4.0));
                vec3 scale=vec3(1.0)+tint-0.5*(tint.yzx+tint.zxy);
                encoded=clamp(encoded*scale,0.0,1.0);
                encoded=clamp(encoded+jk_color_add_fade.xyz,0.0,1.0);
                encoded*=jk_color_add_fade.w;
                candidate=pow(clamp(encoded,0.0,1.0),vec3(1.8));
            }
            sector_crossing_lit[i]=candidate;
        }
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
    vec3 r=model_right.xyz, f=model_forward.xyz, u=model_up.xyz;
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
