#include "renderer/sun_shadow.hpp"
#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>

using namespace jkgm;

int main()
{
    // Empty-world data is a valid shadow shell for 3DO-only levels.
    constexpr uintptr_t world_addr=0x120000U;
    std::array<unsigned char,0xb0> header{};
    decal_native_read read=[&](uintptr_t addr,void *out,size_t size) {
        if(addr<world_addr || addr-world_addr>header.size() ||
           size>header.size()-(addr-world_addr))return false;
        std::memcpy(out,header.data()+(addr-world_addr),size);
        return true;
    };
    decal_world_geometry world{};
    assert(world.load(read,world_addr,250000U,true));
    assert(world.valid_world && world.receivers.empty());
    assert(world.matches(read,world_addr));
    spotlight_shadow_static_mesh mesh{};
    assert(build_spotlight_shadow_static_mesh(world,mesh));
    assert(mesh.indices.empty());
    decal_world_geometry decals{};
    assert(!decals.load(read,world_addr,250000U,false));
    // A model-built level can have no usable native world receivers.
    decal_volume_aabb architecture{};
    architecture.low={{100.0f,200.0f,0.0f}};
    architecture.high={{120.0f,220.0f,0.0f}};
    sun_shadow_projection projection{};
    assert(make_sun_shadow_projection(world,{{0.0f,0.0f,1.0f}},
                                      projection,{architecture}));
    auto near=sun_shadow_clip(projection,{{110.0f,210.0f,0.0f}});
    assert(near.x>-1.0f && near.x<1.0f);
    assert(near.y>-1.0f && near.y<1.0f);
    assert(near.z>-1.0f && near.z<1.0f);

    // Nearby view coverage stays compact even with a distant level shell.
    assert(fit_sun_shadow_to_camera(projection,{{105.0f,205.0f,2.0f}},
        {{1.0f,0.0f,0.0f}},{{0.0f,0.0f,1.0f}},
        {{0.0f,1.0f,0.0f}},0.5f,0.4f,20.0f));
    assert(projection.width<50.0f && projection.height<50.0f);
    assert(stabilize_sun_shadow_projection(projection,512U));
    assert(!stabilize_sun_shadow_projection(projection,0U));
    auto visible=sun_shadow_clip(projection,{{105.0f,215.0f,2.0f}});
    assert(visible.x>-1.0f && visible.x<1.0f);
    assert(visible.y>-1.0f && visible.y<1.0f);

    // A small native shell must not crop the remote 3DO architecture.
    decal_world_surface_geometry shell{};
    shell.polygon={{{0.0f,0.0f,0.0f}},{{1.0f,0.0f,0.0f}},
                   {{0.0f,1.0f,0.0f}}};
    world.receivers.push_back(shell);
    assert(make_sun_shadow_projection(world,{{0.0f,0.0f,1.0f}},
                                      projection,{architecture}));
    near=sun_shadow_clip(projection,{{110.0f,210.0f,0.0f}});
    assert(near.x>-1.0f && near.x<1.0f);
    assert(near.y>-1.0f && near.y<1.0f);
    assert(near.z>-1.0f && near.z<1.0f);
}
