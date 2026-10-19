#pragma once
// Pure, testable routing for GPU-resident 3DO surface fragments.
// A texture containing both opaque and translucent texels participates
// in *both* render passes; the fragment shader selects disjoint pixels.
#include <cmath>
namespace jkgm {
    enum class gpu_3do_alpha_mode : int {
        solid=0, mixed=1, masked=2, blend_only=3, dithered=4
    };
    inline gpu_3do_alpha_mode choose_gpu_3do_alpha_mode(
        bool face_translucent,bool texture_has_alpha,bool alpha_mask,
        bool special_blend,float opacity,bool alpha_dither=false)
    {
        // The material's screen-door override can turn even native JK
        // translucent faces into opaque depth-writing coverage. Never do
        // that for additive/multiply/fluid/depth-clip/emission-only effects.
        if(special_blend || !std::isfinite(opacity))
            return gpu_3do_alpha_mode::blend_only;
        if(alpha_dither) return gpu_3do_alpha_mode::dithered;
        if(face_translucent || opacity<0.999f)
            return gpu_3do_alpha_mode::blend_only;
        if(alpha_mask) return gpu_3do_alpha_mode::masked;
        if(texture_has_alpha) return gpu_3do_alpha_mode::mixed;
        return gpu_3do_alpha_mode::solid;
    }
    // Emissive-only material coverage is independent of albedo alpha:
    // emission must blend additively even when the base is fully invisible.
    // This works with or without a texture image.
    inline bool gpu_3do_emissive_only(float alpha,float r,float g,float b)
    {
        return std::isfinite(alpha) && alpha<=0.001f &&
               (r>0.0f || g>0.0f || b>0.0f);
    }
    inline bool gpu_3do_draws_in_pass(gpu_3do_alpha_mode mode, bool transparent)
    {
        return transparent
            ? mode==gpu_3do_alpha_mode::mixed ||
              mode==gpu_3do_alpha_mode::blend_only
            : mode!=gpu_3do_alpha_mode::blend_only;
    }
    // 4x4 screen-door order, shared with all three fragment shaders. Use
    // a spatially stable threshold; no temporal noise or depth-independent
    // transparent blending. Opaque SSAO/depth are preserved on kept pixels.
    inline float gpu_3do_dither_threshold(unsigned x,unsigned y)
    {
        x&=3U; y&=3U;
        // Equivalent to the 4x4 Bayer matrix, but avoids a lookup
        // table and dynamic indexing in every fragment shader.
        unsigned const lo=(((x^y)&1U)<<1U)|(y&1U);
        unsigned const hi=((((x>>1U)^(y>>1U))&1U)<<1U)|((y>>1U)&1U);
        return (float(lo*4U+hi)+0.5f)/16.0f;
    }
    // Mirrors the discard branches in gpu_3do_preview.frag.
    // Source alpha already includes the material and face alpha multipliers.
    inline bool gpu_3do_fragment_visible(gpu_3do_alpha_mode mode,
                                         bool transparent,float alpha,float cutoff,
                                         unsigned pixel_x=0,unsigned pixel_y=0)
    {
        if(!gpu_3do_draws_in_pass(mode,transparent)) return false;
        if(transparent)
            return alpha>0.001f &&
                   (mode!=gpu_3do_alpha_mode::mixed || alpha<0.999f);
        if(mode==gpu_3do_alpha_mode::dithered)
            return alpha>gpu_3do_dither_threshold(pixel_x,pixel_y);
        return mode==gpu_3do_alpha_mode::masked
            ? alpha>=cutoff
            : mode!=gpu_3do_alpha_mode::mixed || alpha>=0.999f;
    }
}
