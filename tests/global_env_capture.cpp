#include "renderer/global_env_capture.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

int main() {
    jkgm::global_env_capture_controls controls;
    auto const pos=std::array<float,3>{{12.0f,-4.0f,3.0f}};
    assert(!controls.snapshot());
    assert(controls.request(pos,0x123400056ULL,0x833A10U,0.5f));
    auto first=controls.snapshot();
    assert(first && first->position==pos && first->fade_time==0.5f);
    assert(first->exclude_owner==0x123400056ULL);
    assert(!controls.request(pos,1U,0x833A10U,-1.0f));
    assert(!controls.request(pos,1U,0x833A10U,3601.0f));
    assert(!controls.request({{std::numeric_limits<float>::quiet_NaN(),0,0}},
                             1U,0x833A10U,0.0f));
    auto still=controls.snapshot();
    assert(still && still->sequence==first->sequence);
    assert(controls.request({{4.0f,1.0f,2.0f}},2U,0x833A10U,0.0f));
    auto second=controls.snapshot();
    assert(second && second->sequence>first->sequence);
    assert(second->position[0]==4.0f);
    controls.prepare_world(0x833A10U);
    assert(controls.snapshot());
    controls.prepare_world(0x833A11U);
    assert(!controls.snapshot());
    controls.clear();
    assert(!controls.snapshot());
    return 0;
}
