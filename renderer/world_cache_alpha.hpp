#pragma once
// Conservative opaque cached-world material policy. Solid adjoins/grates
// are valid GPU receivers only if they have provably opaque coverage or an
// explicit alpha-mask policy. Mixed/translucent/special MATs stay native.
#include "world_cache_visibility.hpp"
#include <cmath>
#include <cstdint>

namespace jkgm {
enum class world_cache_draw_policy : uint8_t {
    native=0,solid=1,textured=2,masked=3
};
struct world_cache_material_info {
    int albedo_source=0;
    bool has_image=false,alpha=false,masked=false,dithered=false;
    bool special=false,emissive_only=false;
    float opacity=1.0f,transition=0.0f;
    float width=1.0f,height=1.0f,alpha_cutoff=0.5f;
};
inline world_cache_draw_policy choose_world_cache_draw_policy(
    world_cache_draw_candidate const &face,
    world_cache_material_info const &mat)
{
    if((face.face_type&0x2U)!=0U ||
       !std::isfinite(mat.opacity) || mat.opacity<0.999f ||
       mat.special || mat.emissive_only || mat.dithered ||
       !std::isfinite(mat.transition) || mat.transition!=0.0f)
        return world_cache_draw_policy::native;
    if(face.geometry_mode==3U) {
        return mat.albedo_source==2
            ? world_cache_draw_policy::solid
            : world_cache_draw_policy::native;
    }
    if(face.geometry_mode!=4U || mat.albedo_source!=1 ||
       !mat.has_image || !std::isfinite(mat.width) ||
       !std::isfinite(mat.height) || mat.width<=0.0f ||
       mat.height<=0.0f)
        return world_cache_draw_policy::native;
    // A mixed-alpha MAT needs an additional sorted transparency pass.
    // Only explicit masking can write a correct depth/SSAO silhouette.
    if(mat.masked) {
        if(!face.solid_adjoin || !std::isfinite(mat.alpha_cutoff) ||
           mat.alpha_cutoff<=0.0f || mat.alpha_cutoff>1.0f)
            return world_cache_draw_policy::native;
        return world_cache_draw_policy::masked;
    }
    if(mat.alpha)return world_cache_draw_policy::native;
    return world_cache_draw_policy::textured;
}
} // namespace jkgm
