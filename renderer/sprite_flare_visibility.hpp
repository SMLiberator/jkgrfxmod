#pragma once
// CPU reference for the SPR 0x40 flare visibility rule.
// The runtime renderer must perform the comparison against the completed
// opaque scene depth *at the projected SPR center*, not at each flare pixel.
#include <algorithm>
#include <cmath>

namespace jkgm {
    // OpenGL default depth: zero at near plane, one at far plane.
    // A flare remains fully visible if its center is not behind opaque geo.
    // Reject off-screen, behind-eye and invalid projections in the caller.
    inline bool sprite_flare_center_unoccluded(
        float scene_depth,float center_depth,float depth_bias=0.0001f)
    {
        if(!std::isfinite(scene_depth)||!std::isfinite(center_depth)||
           !std::isfinite(depth_bias)||depth_bias<0.0f ||
           scene_depth<0.0f||scene_depth>1.0f ||
           center_depth<0.0f||center_depth>1.0f)
            return false;
        return center_depth<=scene_depth+depth_bias;
    }

    // A low-pass visibility envelope avoids flare popping at silhouette
    // boundaries. Values are multiplied by the sprite's per-Thing opacity;
    // neither the source SPR nor its shared MAT are modified.
    struct sprite_flare_fade {
        float visibility=0.0f;
        void reset(bool visible) {visibility=visible?1.0f:0.0f;}
        float update(bool visible,float dt,float fade_seconds=0.08f) {
            if(!std::isfinite(dt)||dt<0.0f||
               !std::isfinite(fade_seconds)||fade_seconds<0.0f)
                return visibility;
            float const goal=visible?1.0f:0.0f;
            if(fade_seconds<=0.0f) return visibility=goal;
            float const rate=std::min(1.0f,dt/fade_seconds);
            visibility+=rate*(goal-visibility);
            return visibility;
        }
    };

    inline float sprite_effective_opacity(float instance_opacity,
                                           float flare_visibility)
    {
        if(!std::isfinite(instance_opacity)||
           !std::isfinite(flare_visibility)) return 0.0f;
        return std::clamp(instance_opacity,0.0f,1.0f)*
               std::clamp(flare_visibility,0.0f,1.0f);
    }
}
