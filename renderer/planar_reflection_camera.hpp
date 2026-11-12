#pragma once
// GL-independent mirrored-camera math. A reflection capture must NEVER
// mutate JK's live rdCamera or depend on its sector being valid below water.
#include "engine_camera_data.hpp"
#include "gpu_3do_modelview.hpp"
#include <array>
#include <cmath>

namespace jkgm {
    // The plane is dot(normal, world_position) == offset.
    // Normal need not be unit length; horizontal water uses {0,0,1}, Z.
    struct planar_reflection_plane {
        std::array<float,3> normal{{0.0f,0.0f,1.0f}};
        float offset=0.0f;
    };

    inline bool normalize_planar_reflection_plane(
        planar_reflection_plane const &input,planar_reflection_plane &output)
    {
        auto const &n=input.normal;
        if(!engine_camera_detail::finite(n) || !std::isfinite(input.offset))
            return false;
        float const length2=n[0]*n[0]+n[1]*n[1]+n[2]*n[2];
        if(!std::isfinite(length2) || length2<1e-12f)return false;
        float const inv=1.0f/std::sqrt(length2);
        output.normal={{n[0]*inv,n[1]*inv,n[2]*inv}};
        output.offset=input.offset*inv;
        return std::isfinite(output.offset);
    }

    inline std::array<float,3> reflect_planar_direction(
        std::array<float,3> const &v,std::array<float,3> const &unit_normal)
    {
        float const d=engine_camera_detail::dot(v,unit_normal);
        return {{v[0]-2.0f*d*unit_normal[0],
                 v[1]-2.0f*d*unit_normal[1],
                 v[2]-2.0f*d*unit_normal[2]}};
    }

    inline engine_camera make_planar_reflection_camera(
        engine_camera const &source,planar_reflection_plane const &plane)
    {
        engine_camera out{};
        if(!source.valid || !engine_camera_detail::finite(source.position) ||
           !engine_camera_detail::finite(source.right) ||
           !engine_camera_detail::finite(source.forward) ||
           !engine_camera_detail::finite(source.up))return out;
        planar_reflection_plane normalized{};
        if(!normalize_planar_reflection_plane(plane,normalized))return out;
        out=source;
        auto const &n=normalized.normal;
        float const distance=engine_camera_detail::dot(source.position,n)-
                             normalized.offset;
        out.position={{source.position[0]-2.0f*distance*n[0],
                       source.position[1]-2.0f*distance*n[1],
                       source.position[2]-2.0f*distance*n[2]}};
        out.right=reflect_planar_direction(source.right,n);
        out.forward=reflect_planar_direction(source.forward,n);
        out.up=reflect_planar_direction(source.up,n);
        // A reflection reverses handedness. Capture rasterization must
        // explicitly reverse front-face culling, not silently re-flip an axis.
        out.valid=engine_camera_detail::finite(out.position) &&
                  engine_camera_detail::finite(out.right) &&
                  engine_camera_detail::finite(out.forward) &&
                  engine_camera_detail::finite(out.up);
        return out;
    }

    // World-to-camera rdMatrix34 layout consumed by BOTH GPU-cached world
    // vertices and gpu_3do_native_modelview(camera, model_placement).
    inline gpu_3do_matrix34 planar_camera_native_view(
        engine_camera const &camera)
    {
        gpu_3do_matrix34 m{};
        if(!camera.valid)return m;
        for(int i=0;i<3;++i) {
            m[3*i]=camera.right[i];
            m[3*i+1]=camera.forward[i];
            m[3*i+2]=camera.up[i];
        }
        m[9]=-engine_camera_detail::dot(camera.right,camera.position);
        m[10]=-engine_camera_detail::dot(camera.forward,camera.position);
        m[11]=-engine_camera_detail::dot(camera.up,camera.position);
        return m;
    }
} // namespace jkgm
