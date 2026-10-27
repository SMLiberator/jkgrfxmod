#version 330 core
// Depth-only capture of pre-cached, level-wide SithWorld polygons. Positions
// are original JK world XYZ, independent of the player's camera/frustum.
layout(location=0) in vec3 world_position;
layout(location=1) in vec2 world_uv;
layout(location=2) in vec3 shadow_polygon_center; // only world shadow VAOs
out vec2 shadow_material_uv;
uniform vec3 shadow_origin;
uniform vec3 shadow_right;
uniform vec3 shadow_up;
uniform vec3 shadow_forward;
uniform vec4 shadow_limits; // width at far plane, height, range, near
uniform int shadow_orthographic;
uniform vec4 shadow_caster_padding; // texels, texel-to-NDC scale, centroid mode, unused
uniform vec3 shadow_caster_model_center; // 3DO mesh-group center in local coordinates
// Identity for static SithWorld, instance matrix for GPU-cached 3DOs.
uniform vec3 shadow_model_right;
uniform vec3 shadow_model_forward;
uniform vec3 shadow_model_up;
uniform vec3 shadow_model_origin;
void main()
{
    shadow_material_uv=world_uv;
    vec3 world=shadow_model_right*world_position.x+
               shadow_model_forward*world_position.y+
               shadow_model_up*world_position.z+shadow_model_origin;
    vec3 relative=world-shadow_origin;
    if(shadow_orthographic!=0) {
        gl_Position=vec4(
            2.0*dot(relative,shadow_right)/shadow_limits.x-1.0,
            2.0*dot(relative,shadow_up)/shadow_limits.y-1.0,
            2.0*dot(relative,shadow_forward)/shadow_limits.z-1.0,
            1.0);
        // Expand only the shadow caster silhouette in light-space, without
        // moving its depth or visible model. This helps small raster cracks
        // at touching wall/floor edges and costs no extra shadow lookups.
        if(shadow_caster_padding.x>0.0) {
            vec3 local_center=shadow_caster_padding.z>0.5
                ? shadow_polygon_center : shadow_caster_model_center;
            vec3 center_world=shadow_model_right*local_center.x+
                              shadow_model_forward*local_center.y+
                              shadow_model_up*local_center.z+
                              shadow_model_origin;
            vec3 from_center=world-center_world;
            vec2 radial=vec2(dot(from_center,shadow_right)/shadow_limits.x,
                             dot(from_center,shadow_up)/shadow_limits.y);
            float radius=length(radial);
            if(radius>0.0000001)
                gl_Position.xy+=(radial/radius)*
                    shadow_caster_padding.x*shadow_caster_padding.y;
        }
        return;
    }
    float distance=dot(relative,shadow_forward);
    float far_plane=shadow_limits.z;
    float near_plane=shadow_limits.w;
    gl_Position=vec4(
        2.0*dot(relative,shadow_right)*far_plane/shadow_limits.x,
        2.0*dot(relative,shadow_up)*far_plane/shadow_limits.y,
        (far_plane+near_plane)/(far_plane-near_plane)*distance-
            (2.0*far_plane*near_plane)/(far_plane-near_plane),
        distance);
}
