#include "renderer/sprite_instance_info.hpp"
#include "renderer/gpu_3do_alpha.hpp"
#include "renderer/sun_shadow.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

int main() {
    using namespace jkgm;
    thing_opacity_registry fades;
    void const *const actor=reinterpret_cast<void const *>(0x123456U);
    void const *const second=reinterpret_cast<void const *>(0x123457U);
    assert(fades.sample(actor,10U,0U).opacity==1.0f);
    assert(!fades.sample(actor,10U,0U).dither);
    assert(!fades.set(nullptr,10U,0.5f,false,1000U,1.0f));
    assert(!fades.set(actor,10U,-0.1f,false,1000U,1.0f));
    assert(!fades.set(actor,10U,1.1f,false,1000U,1.0f));
    assert(!fades.set(actor,10U,0.5f,false,1000U,-1.0f));
    assert(!fades.set(actor,10U,0.5f,false,1000U,3601.0f));
    assert(!fades.set(actor,10U,std::numeric_limits<float>::quiet_NaN(),
                      false,1000U,0.0f));
    assert(fades.set(actor,10U,0.0f,false,1000U,2.0f));
    assert(fades.sample(actor,10U,1000U).opacity==1.0f);
    assert(fades.sample(actor,10U,2000U).opacity==0.5f);
    assert(fades.sample(actor,10U,3000U).opacity==0.0f);
    assert(fades.sample(second,10U,3000U).opacity==1.0f);
    // Retarget during an active fade; dither changes immediately.
    assert(fades.set(actor,10U,1.0f,true,2000U,2.0f));
    assert(fades.sample(actor,10U,2000U).dither);
    assert(fades.sample(actor,10U,2000U).opacity==0.5f);
    assert(std::abs(fades.sample(actor,10U,3000U).opacity-0.75f)<1e-5f);
    assert(fades.sample(actor,10U,5000U).opacity==1.0f);
    // Old fade cannot apply to a recycled native SithThing pointer.
    assert(fades.sample(actor,11U,3000U).opacity==1.0f);
    assert(!fades.sample(actor,11U,3000U).dither);
    assert(fades.set(actor,11U,0.3f,false,3500U,0.0f));
    assert(fades.sample(actor,11U,3500U).opacity==0.3f);
    fades.forget(actor);
    assert(fades.sample(actor,11U,3500U).opacity==1.0f);
    fades.clear();

    // The normal-alpha vs dither selection reuses the original GPU policy.
    assert(choose_gpu_3do_alpha_mode(true,false,false,false,1.f,false)==
           gpu_3do_alpha_mode::blend_only);
    assert(choose_gpu_3do_alpha_mode(false,false,false,false,1.f,true)==
           gpu_3do_alpha_mode::dithered);
    assert(choose_gpu_3do_alpha_mode(true,false,false,true,1.f,true)==
           gpu_3do_alpha_mode::blend_only);

    // Existing blended 3DO MATs may cast coverage-based shadows.
    assert(sun_shadow_model_material_casts_depth(1U,false,false,
                                                  0.35f,true,0.50f));
    assert(sun_shadow_model_material_casts_depth(2U,false,false,
                                                  0.35f,false,1.0f));
    assert(!sun_shadow_model_material_casts_depth(1U,false,false,
                                                   0.35f,true,0.0f));
    assert(!sun_shadow_model_material_casts_depth(1U,true,false,
                                                   0.35f,true,0.5f));
    assert(!sun_shadow_model_material_casts_depth(1U,false,true,
                                                   0.35f,true,0.5f));
    assert(!sun_shadow_model_material_casts_depth(2U,false,false,
                                                   0.35f,true,0.5f));
    assert(!sun_shadow_model_material_casts_depth(1U,false,false,
                                                   0.0f,false,0.5f));
    return 0;
}
