#include "renderer/camera_hud_thing.hpp"
#include <cassert>
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
    return 0;
}
