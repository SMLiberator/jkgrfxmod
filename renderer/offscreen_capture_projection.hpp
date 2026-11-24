#pragma once
// GL-independent projection description for reusable secondary cameras.
// Low-level secondary-camera projection: fov_degrees=0 selects orthographic;
// ortho_vertical_span is the view height in JK world units, independent of
// target MAT resolution. The CameraToMat COG API uses *negative* FOV for this.
#include <cmath>
#include <array>
namespace jkgm {
struct offscreen_capture_projection {
    float half_horizontal=0.0f,aspect=1.0f,reciprocal_far=0.0f;
    bool orthographic=false,valid=false;
};
inline offscreen_capture_projection make_offscreen_capture_projection(
    int target_width,int target_height,float fov_degrees,
    float ortho_vertical_span,float far_plane)
{
    offscreen_capture_projection p{};
    if(target_width<1 || target_height<1 ||
       target_width>16384 || target_height>16384 ||
       !std::isfinite(fov_degrees) || !std::isfinite(far_plane) ||
       far_plane<=0.1f || fov_degrees<0.0f || fov_degrees>=179.0f)
        return p;
    p.aspect=float(target_width)/float(target_height);
    p.reciprocal_far=1.0f/far_plane;
    p.orthographic=fov_degrees==0.0f;
    if(p.orthographic) {
        if(!std::isfinite(ortho_vertical_span) ||
           ortho_vertical_span<=0.0f)return {};
        p.half_horizontal=0.5f*ortho_vertical_span*p.aspect;
    } else {
        // Full horizontal FOV, matching existing planar projections.
        p.half_horizontal=std::tan(
            fov_degrees*0.008726646259971648f);
    }
    p.valid=std::isfinite(p.half_horizontal) &&
        p.half_horizontal>0.0f &&
        std::isfinite(p.reciprocal_far) &&
        std::isfinite(p.aspect) && p.aspect>0.0f;
    return p;
}
// CameraToMat COG convention: positive FOV = perspective degrees,
// negative FOV = orthographic vertical span in JK units, zero = no capture.
// Keep the low-level zero-FOV projection interface unchanged for other users.
inline offscreen_capture_projection make_camera_to_mat_projection(
    int target_width,int target_height,float signed_fov,float far_plane)
{
    if(!std::isfinite(signed_fov) || signed_fov==0.0f)return {};
    return make_offscreen_capture_projection(
        target_width,target_height,
        signed_fov<0.0f?0.0f:signed_fov,
        signed_fov<0.0f?-signed_fov:1.0f,far_plane);
}
// Camera-view axes are x=right, y=forward, z=up. This agrees with the
// capture GLSL for both projection modes, with no native rdCamera mutation.
inline std::array<float,4> offscreen_capture_clip_coordinates(
    offscreen_capture_projection const &p,
    std::array<float,3> const &view_point)
{
    if(!p.valid)return {{0.0f,0.0f,0.0f,0.0f}};
    float const half_vertical=p.half_horizontal/p.aspect;
    if(p.orthographic)return {{
        view_point[0]/p.half_horizontal,
        view_point[2]/half_vertical,
        2.0f*view_point[1]*p.reciprocal_far-1.0f,
        1.0f
    }};
    return {{
        view_point[0]/p.half_horizontal,
        view_point[2]/half_vertical,
        -p.reciprocal_far,
        view_point[1]
    }};
}
} // namespace jkgm
