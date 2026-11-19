#include "../common/material.hpp"
#include <cassert>
#include <cmath>

int main()
{
    using namespace jkgm;
    material legacy;
    assert(!legacy.matcap_map && !legacy.matcap_surface_map);
    assert(legacy.matcap_roughness==0.0f);
    assert(get<0>(legacy.matcap_factor)==1.0f);
    assert(get<1>(legacy.matcap_factor)==1.0f);
    assert(get<2>(legacy.matcap_factor)==1.0f);
    assert(!legacy.clearcoat_surface_map && !legacy.clearcoat_normal_map);
    assert(legacy.clearcoat_strength==0.0f);
    assert(legacy.clearcoat_roughness==0.1f);
    assert(legacy.clearcoat_transmission==1.0f);
    assert(legacy.reflection_mode==material_reflection_mode::envmap);
    assert(legacy.reflection_strength==0.0f);

    // New layers do not silently change the base reflection mode, strength,
    // metallicity, roughness or existing default-specular eligibility.
    legacy.clearcoat_surface_map=fs::path("coat.png");
    legacy.clearcoat_strength=1.0f;
    legacy.clearcoat_transmission=0.45f;
    legacy.matcap_map=fs::path("pearlescent.png");
    legacy.matcap_factor=color_rgb(0.7f,0.5f,1.2f);
    assert(legacy.reflection_strength==0.0f);
    assert(legacy.reflection_mode==material_reflection_mode::envmap);
    assert(!legacy.has_explicit_specular_settings);
    assert(get<2>(legacy.matcap_factor)==1.2f);
    return 0;
}
