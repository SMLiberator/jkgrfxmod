#include "renderer/sun_caustics.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <numeric>
int main()
{
    auto const a=jkgm::generate_sun_caustic_pattern();
    auto const b=jkgm::generate_sun_caustic_pattern();
    static_assert(jkgm::sun_caustic_pattern_size==256,"fixed texture size");
    assert(a==b); // determinism: one texture per context, no random seed
    auto const extrema=std::minmax_element(a.begin(),a.end());
    assert(*extrema.first<16U);  // dark field between light strands
    assert(*extrema.second>220U); // concentrated bright caustic ridges
    std::size_t bright=0,dark=0;
    for(std::uint8_t v:a) {
        if(v>160U)++bright;
        if(v<55U)++dark;
    }
    assert(bright>500 && bright<40000);
    assert(dark>500 && dark<60000);
    assert(jkgm::sun_caustic_hash(0,0)==jkgm::sun_caustic_hash(0,0));
    assert(jkgm::sun_caustic_hash(0,0)!=jkgm::sun_caustic_hash(1,0));
}
