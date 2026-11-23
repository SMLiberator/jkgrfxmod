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

    using jkgm::global_env_fade_origin;
    using jkgm::global_env_fade_factor;
    using jkgm::choose_global_env_fade_origin;
    // True fade progress, including the capture's first frame and a
    // completed fade, must agree with the shader's effective blend.
    assert(global_env_fade_factor(0.0f,0.0f)==1.0f);
    assert(global_env_fade_factor(2.0f,0.0f)==0.0f);
    assert(global_env_fade_factor(2.0f,0.5f)==0.25f);
    assert(global_env_fade_factor(2.0f,1.0f)==0.5f);
    assert(global_env_fade_factor(2.0f,4.0f)==1.0f);
    assert(global_env_fade_factor(2.0f,-1.0f)==0.0f);
    // An initial fade with no prior capture has no texture base, so
    // it still blends from the inherited/default authored EnvMap.
    assert(choose_global_env_fade_origin(false,false,1.0f,0.0f)==
           global_env_fade_origin::none);
    // fadeTime=0 always switches instantly, even during another fade.
    assert(choose_global_env_fade_origin(true,true,0.0f,0.5f)==
           global_env_fade_origin::none);
    // A completed old fade uses its current endpoint directly.
    assert(choose_global_env_fade_origin(true,false,1.0f,1.0f)==
           global_env_fade_origin::current_capture);
    assert(choose_global_env_fade_origin(true,true,1.0f,1.0f)==
           global_env_fade_origin::current_capture);
    // A still-starting old fade should not jump to its current endpoint.
    assert(choose_global_env_fade_origin(true,true,1.0f,0.0f)==
           global_env_fade_origin::previous_capture);
    // Interrupted partial fades require a one-time HDR composition.
    for(float old_mix: {0.01f,0.2f,0.5f,0.99f})
        assert(choose_global_env_fade_origin(true,true,1.0f,old_mix)==
               global_env_fade_origin::composited_capture);
    return 0;
}
