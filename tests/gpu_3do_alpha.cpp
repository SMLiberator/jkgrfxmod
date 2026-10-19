#include "renderer/gpu_3do_alpha.hpp"
#include <cassert>
#include <initializer_list>
using namespace jkgm;
int main() {
    auto solid=choose_gpu_3do_alpha_mode(false,false,false,false,1.f);
    auto mixed=choose_gpu_3do_alpha_mode(false,true,false,false,1.f);
    auto masked=choose_gpu_3do_alpha_mode(false,true,true,false,1.f);
    auto forced=choose_gpu_3do_alpha_mode(true,true,false,false,1.f);
    auto special=choose_gpu_3do_alpha_mode(false,true,false,true,1.f);
    auto faded=choose_gpu_3do_alpha_mode(false,false,false,false,0.5f);
    assert(solid==gpu_3do_alpha_mode::solid);
    assert(mixed==gpu_3do_alpha_mode::mixed);
    assert(masked==gpu_3do_alpha_mode::masked);
    assert(forced==gpu_3do_alpha_mode::blend_only);
    assert(special==gpu_3do_alpha_mode::blend_only);
    assert(faded==gpu_3do_alpha_mode::blend_only);
    auto dither=choose_gpu_3do_alpha_mode(false,true,false,false,0.45f,true);
    auto native_face_dither=choose_gpu_3do_alpha_mode(true,false,false,false,1.f,true);
    auto special_dither=choose_gpu_3do_alpha_mode(true,true,false,true,1.f,true);
    assert(dither==gpu_3do_alpha_mode::dithered);
    assert(native_face_dither==gpu_3do_alpha_mode::dithered);
    assert(special_dither==gpu_3do_alpha_mode::blend_only);
    assert(gpu_3do_draws_in_pass(dither,false));
    assert(!gpu_3do_draws_in_pass(dither,true));
    for(unsigned y=0;y<4;y++) for(unsigned x=0;x<4;x++) {
        assert(!gpu_3do_fragment_visible(dither,true,1.f,0.5f,x,y));
        assert(!gpu_3do_fragment_visible(dither,false,0.f,0.5f,x,y));
        assert(gpu_3do_fragment_visible(dither,false,1.f,0.5f,x,y));
    }
    const int bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    for(unsigned y=0;y<4;y++) for(unsigned x=0;x<4;x++)
        assert(gpu_3do_dither_threshold(x,y)==
               (float(bayer[x+4*y])+0.5f)/16.0f);
    int half_covered=0;
    for(unsigned y=0;y<4;y++) for(unsigned x=0;x<4;x++)
        half_covered+=gpu_3do_fragment_visible(dither,false,0.5f,0.5f,x,y)?1:0;
    assert(half_covered==8);
    for(float alpha : {0.f,0.3f,0.998f,0.999f,1.f}) {
        bool o=gpu_3do_fragment_visible(mixed,false,alpha,0.5f);
        bool t=gpu_3do_fragment_visible(mixed,true,alpha,0.5f);
        assert(!(o && t)); // no double blending/SSAO
        if(alpha>0.001f) assert(o || t);
        else assert(!o && !t);
    }
    assert(gpu_3do_fragment_visible(masked,false,0.6f,0.5f));
    assert(!gpu_3do_fragment_visible(masked,false,0.4f,0.5f));
    assert(!gpu_3do_fragment_visible(masked,true,0.6f,0.5f));
    // Transparent base must not suppress independent emission, even when
    // the material still has a texture image.
    assert(gpu_3do_emissive_only(0.f,1.f,0.f,0.f));
    assert(gpu_3do_emissive_only(0.f,0.f,0.f,1.f));
    assert(!gpu_3do_emissive_only(0.f,0.f,0.f,0.f));
    assert(!gpu_3do_emissive_only(0.5f,1.f,0.f,0.f));
    assert(!gpu_3do_emissive_only(1.f,1.f,0.f,0.f));
    assert(!gpu_3do_fragment_visible(forced,false,1.f,0.5f));
    assert(gpu_3do_fragment_visible(forced,true,1.f,0.5f));
    return 0;
}
