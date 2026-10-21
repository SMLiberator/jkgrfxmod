#include "renderer/native_thingface_friction.hpp"
#include <cassert>
using namespace jkgm;

int main() {
    // The Detour targets two complete original x86 instructions.
    assert(native_thingface_ice_point==0x004F7833U);
    assert(native_thingface_ice_bytes[0]==0x8b);
    assert(native_thingface_ice_bytes[6]==0x8b);

    assert(thingflag_icy==0x80000000U);
    assert(native_thingface_ice_multiplier(2,thingflag_icy)==0.3f);
    assert(native_thingface_ice_multiplier(2,thingflag_icy|0x40U)==0.3f);
    // Older experimental assignments must no longer cause icy physics.
    assert(native_thingface_ice_multiplier(2,0x20U)==1.0f);
    assert(native_thingface_ice_multiplier(2,0x40000000U)==1.0f);
    assert(native_thingface_ice_multiplier(2,0x10000U)==1.0f);
    assert(native_thingface_ice_multiplier(2,0)==1.0f);
    assert(native_thingface_ice_multiplier(1,thingflag_icy)==1.0f);
    assert(native_thingface_ice_multiplier(4,thingflag_icy)==1.0f);
    assert(native_thingface_ice_multiplier(0,thingflag_icy)==1.0f);
}
