#include "renderer/cog_camera_axes.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

struct region {
    std::uintptr_t base;
    std::vector<unsigned char> data;
};
int main()
{
    std::vector<region> regions{
        {jkgm::cog_current_camera_index_addr,
            std::vector<unsigned char>(sizeof(std::int32_t))},
        {jkgm::cog_current_camera_array_addr,
            std::vector<unsigned char>(7U*0x4e0U)}
    };
    auto read=[&](std::uintptr_t at,void *dst,std::size_t bytes) {
        for(auto const &r:regions) {
            if(at>=r.base && at-r.base<=r.data.size() &&
               bytes<=r.data.size()-(at-r.base)) {
                std::memcpy(dst,r.data.data()+(at-r.base),bytes);
                return true;
            }
        }
        return false;
    };
    auto write=[&](std::uintptr_t at,auto const &v) {
        for(auto &r:regions) {
            if(at>=r.base && at-r.base<=r.data.size() &&
               sizeof(v)<=r.data.size()-(at-r.base)) {
                std::memcpy(r.data.data()+(at-r.base),&v,sizeof(v));
                return;
            }
        }
        assert(false && "bad fixture address");
    };
    using jkgm::cog_camera_axis;
    auto sample=[&](cog_camera_axis axis) {
        std::array<float,3> value{{42,42,42}};
        bool const ok=jkgm::cog_current_camera_axis(read,axis,value);
        return std::make_pair(ok,value);
    };
    auto set_axes=[&](int index,std::array<float,3> right,
                       std::array<float,3> look,std::array<float,3> up) {
        std::uintptr_t const base=jkgm::cog_current_camera_array_addr+
            std::uintptr_t(index)*jkgm::cog_current_camera_stride+
            jkgm::cog_current_camera_orient;
        write(base,right);
        write(base+12,look);
        write(base+24,up);
    };
    std::array<float,3> const zero{{0,0,0}};
    write(jkgm::cog_current_camera_index_addr,std::int32_t{0});
    set_axes(0,{{1,0,0}},{{0,1,0}},{{0,0,1}});
    set_axes(6,{{0,-1,0}},{{1,0,0}},{{0,0,1}});
    assert(sample(cog_camera_axis::right).first);
    assert((sample(cog_camera_axis::right).second==
            std::array<float,3>{{1,0,0}}));
    assert((sample(cog_camera_axis::look).second==
            std::array<float,3>{{0,1,0}}));
    assert((sample(cog_camera_axis::up).second==
            std::array<float,3>{{0,0,1}}));

    write(jkgm::cog_current_camera_index_addr,std::int32_t{6});
    assert((sample(cog_camera_axis::right).second==
            std::array<float,3>{{0,-1,0}}));
    assert((sample(cog_camera_axis::look).second==
            std::array<float,3>{{1,0,0}}));

    write(jkgm::cog_current_camera_index_addr,std::int32_t{7});
    assert(!sample(cog_camera_axis::right).first);
    assert(sample(cog_camera_axis::right).second==zero);
    write(jkgm::cog_current_camera_index_addr,std::int32_t{-1});
    assert(!sample(cog_camera_axis::up).first);
    assert(sample(cog_camera_axis::up).second==zero);

    write(jkgm::cog_current_camera_index_addr,std::int32_t{0});
    set_axes(0,{{NAN,0,0}},{{0,1,0}},{{0,0,1}});
    assert(!sample(cog_camera_axis::right).first);
    assert(sample(cog_camera_axis::right).second==zero);
    assert(sample(cog_camera_axis::look).first);
    assert(!sample(static_cast<cog_camera_axis>(3)).first);
}
