#pragma once
// Cached SithWorld material classification. Opaque pixels of mixed-alpha
// maps write the G-buffer; translucent pixels enter the sorted alpha pass.
// Respect native face translucency separately from texture alpha.
#include "world_cache_visibility.hpp"
#include <cmath>
#include <cstdint>

namespace jkgm {
enum class world_cache_draw_policy : uint8_t {
    native=0,solid=1,textured=2,masked=3,mixed=4,blended=5,dithered=6,
    emissive=7
};
enum class world_cache_pass : uint8_t {opaque,transparent};
struct world_cache_material_info {
    int albedo_source=0;
    bool has_image=false,alpha=false,masked=false,dithered=false;
    bool special=false,emissive_only=false;
    int blend_mode=0; // normal 0; additive 1; multiply 2; fluid 3
    float opacity=1.0f,transition=0.0f;
    float width=1.0f,height=1.0f,alpha_cutoff=0.5f;
};
// The shared GPU resolver returns 2 when a material replacement intentionally
// omits its albedo image (e.g. fluid with only a tint/normal/EnvMap). The
// shader samples neutral white for source 2. It is NOT an unresolved texture:
// only source 0 indicates failure after the native MAT/GPU lookup.
inline bool world_cache_albedo_ready(int source,bool has_image)
{
    return (source==1 && has_image) || (source==2 && !has_image);
}
inline world_cache_draw_policy choose_world_cache_draw_policy(
    world_cache_draw_candidate const &face,
    world_cache_material_info const &mat)
{
    if(!std::isfinite(mat.opacity) || mat.opacity<0.0f ||
       mat.opacity>1.0f ||
       (mat.special && mat.blend_mode!=1 && mat.blend_mode!=2 &&
        mat.blend_mode!=3) ||
       !std::isfinite(mat.transition) ||
       mat.transition<0.0f || mat.transition>1.0f)
        return world_cache_draw_policy::native;
    if(mat.emissive_only) {
        if(face.geometry_mode==3U && mat.albedo_source==2)
            return world_cache_draw_policy::emissive;
        // GeoMode 4 can omit the albedo image when it emits only light.
        if(face.geometry_mode==4U &&
           ((mat.albedo_source==1 && mat.has_image) ||
            mat.albedo_source==2) &&
           std::isfinite(mat.width) && std::isfinite(mat.height) &&
           mat.width>0.0f && mat.height>0.0f)
            return world_cache_draw_policy::emissive;
        return world_cache_draw_policy::native;
    }
    if(face.geometry_mode==3U) {
        if(mat.albedo_source!=2)return world_cache_draw_policy::native;
        return mat.special || (face.face_type&0x2U) || mat.opacity<0.999f
            ?world_cache_draw_policy::blended:world_cache_draw_policy::solid;
    }
    if(face.geometry_mode!=4U ||
       !world_cache_albedo_ready(mat.albedo_source,mat.has_image) ||
       !std::isfinite(mat.width) || !std::isfinite(mat.height) ||
       mat.width<=0.0f || mat.height<=0.0f)
        return world_cache_draw_policy::native;
    // JK's rdFace.type 0x2 forces 90/255 alpha on the entire face even
    // when its MAT is opaque. A material opacity override also forces
    // blending, rather than writing opaque depth for its alpha=1 texels.
    if(mat.special || (face.face_type&0x2U) || mat.opacity<0.999f)
        return world_cache_draw_policy::blended;
    if(mat.masked) {
        if(!std::isfinite(mat.alpha_cutoff) ||
           mat.alpha_cutoff<=0.0f || mat.alpha_cutoff>1.0f)
            return world_cache_draw_policy::native;
        return world_cache_draw_policy::masked;
    }
    if(mat.dithered)return world_cache_draw_policy::dithered;
    if(mat.alpha)return world_cache_draw_policy::mixed;
    return world_cache_draw_policy::textured;
}
inline bool world_cache_draws_in_pass(world_cache_draw_policy policy,
                                     world_cache_pass pass)
{
    if(pass==world_cache_pass::opaque)
        return policy!=world_cache_draw_policy::native &&
               policy!=world_cache_draw_policy::blended &&
           policy!=world_cache_draw_policy::emissive;
    return policy==world_cache_draw_policy::mixed ||
           policy==world_cache_draw_policy::blended ||
           policy==world_cache_draw_policy::emissive;
}
inline int world_cache_alpha_mode(world_cache_draw_policy policy)
{
    switch(policy) {
    case world_cache_draw_policy::mixed:return 1;
    case world_cache_draw_policy::masked:return 2;
    case world_cache_draw_policy::blended:
    case world_cache_draw_policy::emissive:return 3;
    case world_cache_draw_policy::dithered:return 4;
    default:return 0;
    }
}
} // namespace jkgm
