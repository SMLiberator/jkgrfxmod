#pragma once
// Direct single-axis equivalents of JK.dll GetCurrentCameraRLUVecs.
// The original pushes SithCamera.orient.uvec, lvec, rvec in that order.
// The native camera orientation is stored in world space; these getters do
// not use the separate active rdCamera view matrix or cached Thing positions.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace jkgm {
    enum class cog_camera_axis {right=0,look=1,up=2};
    constexpr std::uintptr_t cog_current_camera_index_addr=0x008ee4e8U;
    constexpr std::uintptr_t cog_current_camera_array_addr=0x008ec2c0U;
    constexpr std::uintptr_t cog_current_camera_stride=0x4e0U;
    constexpr std::uintptr_t cog_current_camera_orient=0x34U;
    constexpr int cog_current_camera_count=7;

    template<class Read>
    bool cog_current_camera_axis(Read&& read,cog_camera_axis axis,
                                 std::array<float,3> &out)
    {
        out={{0.0f,0.0f,0.0f}};
        std::int32_t index=-1;
        if(!read(cog_current_camera_index_addr,&index,sizeof(index)) ||
           index<0 || index>=cog_current_camera_count)return false;
        int const component=static_cast<int>(axis);
        if(component<0 || component>2)return false;
        std::uintptr_t const address=cog_current_camera_array_addr+
            std::uintptr_t(index)*cog_current_camera_stride+
            cog_current_camera_orient+std::uintptr_t(component)*12U;
        std::array<float,3> vec{};
        if(!read(address,vec.data(),sizeof(vec)))return false;
        for(float v:vec)if(!std::isfinite(v))return false;
        out=vec;
        return true;
    }
}
