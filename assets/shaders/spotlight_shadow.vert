#version 330 core
// Depth-only capture of pre-cached, level-wide SithWorld polygons. Positions
// are original JK world XYZ, independent of the player's camera/frustum.
layout(location=0) in vec3 world_position;
layout(location=1) in vec2 world_uv;
out vec2 shadow_material_uv;
uniform vec3 shadow_origin;
uniform vec3 shadow_right;
uniform vec3 shadow_up;
uniform vec3 shadow_forward;
uniform vec4 shadow_limits; // width at far plane, height, range, near
uniform int shadow_orthographic;
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
