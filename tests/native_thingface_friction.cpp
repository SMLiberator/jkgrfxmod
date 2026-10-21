#include "renderer/native_thingface_friction.hpp"
#include <cassert>
using namespace jkgm;
int main() {
    // The patch targets two complete x86 instructions, not a function
    // prologue. The JKGm build must match these bytes before detouring.
    assert(native_thingface_ice_point==0x004F7833U);
    assert(native_thingface_ice_bytes[0]==0x8b);
    assert(native_thingface_ice_bytes[6]==0x8b);

    assert(native_thingface_ice_multiplier(2,thingflag_icy)==0.3f);
    assert(native_thingface_ice_multiplier(2,thingflag_very_icy)==0.1f);
    assert(native_thingface_ice_multiplier(
        2,thingflag_icy|thingflag_very_icy)==0.1f);
    assert(native_thingface_ice_multiplier(2,0)==1.0f);
    assert(native_thingface_ice_multiplier(1,thingflag_icy)==1.0f);
    assert(native_thingface_ice_multiplier(4,thingflag_very_icy)==1.0f);
    assert(native_thingface_ice_multiplier(0,thingflag_icy)==1.0f);
}
