#include "renderer/cog_camera_distance.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

struct region {
    std::uintptr_t base;
    std::vector<unsigned char> bytes;
};

int main()
{
    std::vector<region> regions{
        {0x30000U,std::vector<unsigned char>(0xe0U)}, // SithThing
        {0x40000U,std::vector<unsigned char>(0x80U)}, // native rdCamera
        {jkgm::cog_active_rd_camera_address,
         std::vector<unsigned char>(4U)}
    };
    auto read=[&](std::uintptr_t at,void *dst,std::size_t size)->bool {
        for(auto const &r:regions) {
            if(at>=r.base && at-r.base<=r.bytes.size() &&
               size<=r.bytes.size()-(at-r.base)) {
                std::memcpy(dst,r.bytes.data()+(at-r.base),size);
                return true;
            }
        }
        return false;
    };
    auto write=[&](std::uintptr_t at,auto const &value) {
        for(auto &r:regions) {
            if(at>=r.base && at-r.base<=r.bytes.size() &&
               sizeof(value)<=r.bytes.size()-(at-r.base)) {
                std::memcpy(r.bytes.data()+(at-r.base),&value,sizeof(value));
                return;
            }
        }
        assert(false);
    };
    using jkgm::cog_camera_measure;
    auto measure=[&](cog_camera_measure type) {
        return jkgm::cog_get_thing_camera_measure(read,0x30000U,type);
    };
    assert(measure(cog_camera_measure::depth)==-1.0f);
    write(0x30000U+0x0cU,std::uint32_t{10});
    write(0x30000U+0x74U,std::uint32_t{17});
    write(jkgm::cog_active_rd_camera_address,std::uint32_t{0x40000U});
    write(0x30000U+0xb8U,std::array<float,3>{{3,4,12}});
    // Native rdCamera view matrix, identity camera at origin.
    std::array<float,12> matrix{{1,0,0, 0,1,0, 0,0,1, 0,0,0}};
    write(0x40000U+0x08U,matrix);
    assert(measure(cog_camera_measure::depth)==4.0f);
    assert(measure(cog_camera_measure::distance_sq)==169.0f);
    assert(measure(cog_camera_measure::distance)==13.0f);

    // World-to-view translation: camera at world (10,20,30).
    matrix[9]=-10;matrix[10]=-20;matrix[11]=-30;
    write(0x40000U+0x08U,matrix);
    write(0x30000U+0xb8U,std::array<float,3>{{13,24,42}});
    assert(measure(cog_camera_measure::depth)==4.0f);
    assert(measure(cog_camera_measure::distance_sq)==169.0f);
    assert(measure(cog_camera_measure::distance)==13.0f);

    // Camera facing +world X: native view Y is world X.
    matrix={{0,1,0, -1,0,0, 0,0,1, 0,0,0}};
    write(0x40000U+0x08U,matrix);
    write(0x30000U+0xb8U,std::array<float,3>{{3,4,12}});
    assert(measure(cog_camera_measure::depth)==3.0f);
    assert(measure(cog_camera_measure::distance_sq)==169.0f);
    write(0x30000U+0xb8U,std::array<float,3>{{-3,4,12}});
    assert(measure(cog_camera_measure::depth)==-3.0f); // behind camera

    // An invalid/freed Thing, unavailable camera or invalid matrix fails
    // safely, rather than reading stale transformedPos or using last frame.
    write(0x30000U+0x0cU,std::uint32_t{0});
    assert(measure(cog_camera_measure::distance_sq)==-1.0f);
    write(0x30000U+0x0cU,std::uint32_t{10});
    write(jkgm::cog_active_rd_camera_address,std::uint32_t{0});
    assert(measure(cog_camera_measure::depth)==-1.0f);
    write(jkgm::cog_active_rd_camera_address,std::uint32_t{0x40000U});
    matrix[1]=5.0f; // not an orthonormal view
    write(0x40000U+0x08U,matrix);
    assert(measure(cog_camera_measure::depth)==-1.0f);
    matrix={{1,0,0, 0,1,0, 0,0,1, 0,0,0}};
    write(0x40000U+0x08U,matrix);
    write(0x30000U+0xb8U,std::array<float,3>{{NAN,0,0}});
    assert(measure(cog_camera_measure::distance)==-1.0f);
}
