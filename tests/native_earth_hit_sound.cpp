#include "renderer/native_earth_hit_sound.hpp"
#include <cassert>
using namespace jkgm;
int main() {
    assert(native_earth_sound_branch==0x004E8EBAU);
    assert(native_earth_sound_resume==0x004E8EC1U);
    assert(native_earth_sound_bytes[0]==0x8b);
    assert(native_earth_sound_bytes[4]==0x51);
    assert(native_earth_sound_bytes[5]==0x6a);
    assert(native_nonmetal_surface_impact_mode(0)==46);
    assert(native_nonmetal_surface_impact_mode(0x10000U)==46);
    assert(native_nonmetal_surface_impact_mode(0x00080000U)==48);
    assert(native_nonmetal_surface_impact_mode(0x00090000U)==48);
    // Metal dispatch occurs before this narrow branch.
    assert(native_sound_hitmetal==47);
    assert(native_sound_scrapehard==50);
    assert(native_sound_scrapemetal==51);
    assert(native_sound_scrapeearth==52);
}
