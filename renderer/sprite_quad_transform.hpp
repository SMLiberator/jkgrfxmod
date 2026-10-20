#pragma once
// Camera-space native SPR quad: corners -R-U, +R-U, +R+U, -R+U.
// Rotate around its normal, preserving the center, material and texture UVs.
#include <array>
#include <cmath>
#include <cstring>

namespace jkgm {
    inline bool transform_sprite_camera_quad(
        float *xyz,float width,float height,float roll_degrees) {
        if(!xyz || !std::isfinite(width) || !std::isfinite(height) ||
           !std::isfinite(roll_degrees) || width<0.0f || height<0.0f)
            return false;
        if(width==1.0f && height==1.0f && roll_degrees==0.0f)
            return true;
        std::array<float,12> v{},result{};
        std::memcpy(v.data(),xyz,sizeof(v));
        constexpr float deg_to_rad=0.01745329251994329577f;
        float const angle=std::fmod(roll_degrees,360.0f)*deg_to_rad;
        float const ca=std::cos(angle),sa=std::sin(angle);
        for(int axis=0;axis<3;axis++) {
            float const center=0.25f*(v[axis]+v[axis+3]+v[axis+6]+v[axis+9]);
            float const right=0.5f*(v[axis+3]-v[axis]);
            float const up=0.5f*(v[axis+9]-v[axis]);
            if(!std::isfinite(center)||!std::isfinite(right)||
               !std::isfinite(up)) return false;
            float const rotated_r=width*(ca*right+sa*up);
            float const rotated_u=height*(ca*up-sa*right);
            result[axis]=center-rotated_r-rotated_u;
            result[axis+3]=center+rotated_r-rotated_u;
            result[axis+6]=center+rotated_r+rotated_u;
            result[axis+9]=center-rotated_r+rotated_u;
        }
        std::memcpy(xyz,result.data(),sizeof(result));
        return true;
    }
}
