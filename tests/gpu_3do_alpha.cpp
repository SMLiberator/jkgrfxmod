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
    assert(gpu_3do_needs_emissive_coverage(false,0.f,1.f,0.f,0.f));
    assert(!gpu_3do_needs_emissive_coverage(true,0.f,1.f,0.f,0.f));
    assert(!gpu_3do_needs_emissive_coverage(false,0.f,0.f,0.f,0.f));
    assert(!gpu_3do_needs_emissive_coverage(false,1.f,1.f,0.f,0.f));
    assert(!gpu_3do_fragment_visible(forced,false,1.f,0.5f));
    assert(gpu_3do_fragment_visible(forced,true,1.f,0.5f));
    return 0;
}
