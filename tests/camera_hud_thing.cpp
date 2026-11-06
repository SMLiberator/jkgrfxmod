#include "renderer/camera_hud_thing.hpp"
#include <cassert>
#include <algorithm>
#include <vector>
using namespace jkgm;
int main()
{
    assert(camera_hud_sith_thing_type==1U);
    assert(camera_hud_rd_model_type==1U);
    assert(is_camera_hud_model(1U,1U));
    // Non-3DO Camera Things must not enter the 3DO HUD pipeline.
    assert(!is_camera_hud_model(1U,0U));
    assert(!is_camera_hud_model(1U,4U));
    // A 3DO actor/world object sharing the same MAT/rdMesh is NOT a HUD.
    assert(!is_camera_hud_model(2U,1U));
    assert(!is_camera_hud_model(9U,1U));
    assert(!is_camera_hud_model(10U,1U));
    // 0x20 chooses depth occlusion ONLY for Camera-type Things.
    assert(!camera_hud_uses_world_depth(true,0U));
    assert(camera_hud_uses_world_depth(true,0x20U));
    assert(camera_hud_uses_world_depth(true,0x21U));
    assert(!camera_hud_uses_world_depth(false,0x20U));
    assert(!camera_hud_uses_world_depth(true,0x40U));
    assert(!camera_hud_uses_world_depth(
        is_camera_hud_model(2U,1U),0x20U)); // ordinary skybox
    // Overlay HUDs retain authored submission order regardless of tiny
    // frame-to-frame centroid changes. Optional depth sorting applies only
    // to world-depth 0x20 elements, underneath the overlay HUD layer.
    struct hud_range {
        bool world_depth;
        float depth;
        unsigned long long order;
    };
    auto sort_ranges=[](std::vector<hud_range> &ranges,bool sort_depth) {
        std::sort(ranges.begin(),ranges.end(),
            [sort_depth](hud_range const &a,hud_range const &b) {
                return camera_hud_draw_precedes(
                    a.world_depth,a.depth,a.order,
                    b.world_depth,b.depth,b.order,sort_depth);
            });
    };
    std::vector<hud_range> a{{false,2.0001f,0},{true,6.0f,1},
                             {false,2.0002f,2},{true,8.0f,3},
                             {false,1.9998f,4}};
    sort_ranges(a,true);
    assert(a[0].order==3 && a[1].order==1);
    assert(a[2].order==0 && a[3].order==2 && a[4].order==4);
    // Another camera frame reverses the near-coplanar overlay depths; its
    // alpha painter order must remain unchanged.
    for(auto &x:a)if(!x.world_depth)x.depth=3.0f-x.depth;
    sort_ranges(a,true);
    assert(a[2].order==0 && a[3].order==2 && a[4].order==4);
    // With world-depth sorting disabled, both kinds of HUDs preserve
    // authoring order, but 0x20 surfaces still draw before overlay HUDs.
    sort_ranges(a,false);
    assert(a[0].order==1 && a[1].order==3);
    assert(a[2].order==0 && a[3].order==2 && a[4].order==4);
    // Strict weak ordering is required by std::sort; every synthetic
    // triple satisfies transitivity (including mixed HUD modes).
    for(unsigned mode=0;mode<2;++mode)
        for(auto const &x:a)for(auto const &y:a)for(auto const &z:a) {
            auto before=[&](hud_range const &u,hud_range const &v) {
                return camera_hud_draw_precedes(
                    u.world_depth,u.depth,u.order,
                    v.world_depth,v.depth,v.order,mode!=0);
            };
            assert(!before(x,x));
            if(before(x,y) && before(y,z))assert(before(x,z));
        }
    return 0;
}
