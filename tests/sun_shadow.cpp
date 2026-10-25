#include "renderer/sun_shadow.hpp"
#include <cassert>

using namespace jkgm;

int main()
{
    decal_world_geometry world{};
    world.valid_world=true;
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
