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
    // ThingFlag 0x20 is independent: the classifier takes no ThingFlags.
    assert(!is_camera_hud_model(2U,1U));
    return 0;
}
