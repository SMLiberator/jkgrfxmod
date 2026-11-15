#pragma once
// Camera-relative Thing depth and distances for COG. Native JKGM's active
// rdCamera (global 0x73A418) holds a world-to-view rdMatrix34 at +0x08.
// The forward (Y) component matches SithThing.transformedPos.y, but unlike
// the engine's cached field this calculation is fresh even for hidden Things.
// Depth and squared distance require no square root; actual distance uses one.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace jkgm {
    constexpr std::uintptr_t cog_active_rd_camera_address=0x0073a418U;
    enum class cog_camera_measure {depth,distance_sq,distance};

    template<class Read>
    float cog_get_thing_camera_measure(Read&& read,std::uintptr_t thing,
                                       cog_camera_measure measure)
    {
        if(thing<0x10000U || thing>0x7fffffffU-0xc4U)return -1.0f;
        std::uint32_t kind=0,signature=0;
        std::array<float,3> position{};
        if(!read(thing+0x0cU,&kind,sizeof(kind)) ||
           !read(thing+0x74U,&signature,sizeof(signature)) ||
           kind==0 || kind>15 || signature==0 ||
           !read(thing+0xb8U,position.data(),sizeof(position)))return -1.0f;

        std::uint32_t camera=0;
        if(!read(cog_active_rd_camera_address,&camera,sizeof(camera)) ||
           camera<0x10000U || camera>0x7fffffffU-0x38U)return -1.0f;
        // The matrix is in JK's column-vector rdMatrix34 layout:
        // [r.x,r.y,r.z, l.x,l.y,l.z, u.x,u.y,u.z, t.x,t.y,t.z].
        std::array<float,12> view{};
        if(!read(std::uintptr_t(camera)+0x08U,view.data(),
                 sizeof(view)))return -1.0f;
        for(float v:position)if(!std::isfinite(v))return -1.0f;
        for(float v:view)if(!std::isfinite(v))return -1.0f;
        // Reject uninitialized/invalid camera matrices without taking
        // square roots. The active rdCamera view must be orthonormal.
        for(int i=0;i<3;++i) {
            float const x=view[i],y=view[i+3],z=view[i+6];
            float const norm2=x*x+y*y+z*z;
            if(norm2<0.9604f || norm2>1.0404f)return -1.0f;
        }
        auto const dot_row=[&](int a,int b) {
            return view[a]*view[b]+view[a+3]*view[b+3]+
                   view[a+6]*view[b+6];
        };
        if(std::fabs(dot_row(0,1))>0.02f ||
           std::fabs(dot_row(0,2))>0.02f ||
           std::fabs(dot_row(1,2))>0.02f)return -1.0f;

        float const x=position[0]*view[0]+position[1]*view[3]+
                      position[2]*view[6]+view[9];
        float const y=position[0]*view[1]+position[1]*view[4]+
                      position[2]*view[7]+view[10];
        float const z=position[0]*view[2]+position[1]*view[5]+
                      position[2]*view[8]+view[11];
        if(!std::isfinite(x) || !std::isfinite(y) ||
           !std::isfinite(z))return -1.0f;
        if(measure==cog_camera_measure::depth)return y;
        float const squared=x*x+y*y+z*z;
        if(!std::isfinite(squared))return -1.0f;
        if(measure==cog_camera_measure::distance_sq)return squared;
        if(measure==cog_camera_measure::distance)return std::sqrt(squared);
        return -1.0f;
    }
}
