#pragma once
// Pure, testable routing for GPU-resident 3DO surface fragments.
// A texture containing both opaque and translucent texels participates
// in *both* render passes; the fragment shader selects disjoint pixels.
#include <cmath>
namespace jkgm {
    enum class gpu_3do_alpha_mode : int {
        solid=0, mixed=1, masked=2, blend_only=3
    };
    inline gpu_3do_alpha_mode choose_gpu_3do_alpha_mode(
        bool face_translucent,bool texture_has_alpha,bool alpha_mask,
        bool special_blend,float opacity)
    {
        if(face_translucent || special_blend || !std::isfinite(opacity) ||
           opacity<0.999f)
            return gpu_3do_alpha_mode::blend_only;
        if(alpha_mask) return gpu_3do_alpha_mode::masked;
        if(texture_has_alpha) return gpu_3do_alpha_mode::mixed;
        return gpu_3do_alpha_mode::solid;
    }
    // A replacement with no base image can deliberately use RGBA(0,0,0,0)
    // while emitting light. Its zero base alpha should not suppress emission.
    inline bool gpu_3do_needs_emissive_coverage(bool has_albedo,float alpha,
                                                 float r,float g,float b)
    {
        return !has_albedo && std::isfinite(alpha) && alpha<=0.001f &&
               (r>0.0f || g>0.0f || b>0.0f);
    }
    inline bool gpu_3do_draws_in_pass(gpu_3do_alpha_mode mode, bool transparent)
    {
        return transparent
            ? mode==gpu_3do_alpha_mode::mixed ||
              mode==gpu_3do_alpha_mode::blend_only
            : mode!=gpu_3do_alpha_mode::blend_only;
    }
    // Mirrors the discard branches in gpu_3do_preview.frag.
    // Source alpha already includes the material and face alpha multipliers.
    inline bool gpu_3do_fragment_visible(gpu_3do_alpha_mode mode,
                                         bool transparent,float alpha,float cutoff)
    {
        if(!gpu_3do_draws_in_pass(mode,transparent)) return false;
        if(transparent)
            return alpha>0.001f &&
                   (mode!=gpu_3do_alpha_mode::mixed || alpha<0.999f);
        return mode==gpu_3do_alpha_mode::masked
            ? alpha>=cutoff
            : mode!=gpu_3do_alpha_mode::mixed || alpha>=0.999f;
    }
}
