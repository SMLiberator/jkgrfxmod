#pragma once
// Per-program OpenGL texture-unit registry. See doc/opengl_texture_units.md.
// Fragment samplers of incompatible types must never alias, even when a
// branch disables the effect at runtime (all sampler uniforms start at 0).
#include <cstddef>
namespace jkgm { namespace texture_units {
enum class sampler_kind { tex2d, shadow, buffer, depth_variant };
struct binding { char const *uniform_name; int unit; sampler_kind kind; };
constexpr int forward_decal_light=9; // displacement disabled on decal-only pass
constexpr int forward_decal_occlusion=11;
constexpr int forward_spotlight_shadow=10;
constexpr int forward_sun_near=16,forward_sun_far=17,forward_sun_dynamic=18;
constexpr int forward_sector_buffer=19;
constexpr int forward_next_extended_unit=20; // never use sector's samplerBuffer
constexpr int forward_next_extended_required_units=21; // GL fragment unit count
// GLSL gpu_3do_preview.frag (including #include fragments)
constexpr binding gpu_3do_preview[]={
    {"albedo_map",0,sampler_kind::tex2d},
    {"albedo_map2",6,sampler_kind::tex2d},
    {"emissive_map",1,sampler_kind::tex2d},
    {"scene_depth",8,sampler_kind::tex2d},
    {"displacement_map",9,sampler_kind::tex2d},
    {"palette_fullbright_mask",11,sampler_kind::tex2d},
    {"decal_light_texture",9,sampler_kind::tex2d},
    {"decal_occlusion",11,sampler_kind::tex2d},
    {"surface_map",2,sampler_kind::tex2d},
    {"normal_map",3,sampler_kind::tex2d},
    {"normal_map2",5,sampler_kind::tex2d},
    {"env_map",4,sampler_kind::tex2d},
    {"env_map2",7,sampler_kind::tex2d},
    {"sector_crossing_planes",19,sampler_kind::buffer},
    {"translucent_shadow_image",10,sampler_kind::shadow},
    {"translucent_spotlight_image_0",12,sampler_kind::tex2d},
    {"translucent_spotlight_image_1",13,sampler_kind::tex2d},
    {"translucent_spotlight_image_2",14,sampler_kind::tex2d},
    {"translucent_spotlight_image_3",15,sampler_kind::tex2d},
    {"translucent_sun_near_depth",16,sampler_kind::depth_variant},
    {"translucent_sun_far_depth",17,sampler_kind::depth_variant},
    {"translucent_sun_dynamic_depth",18,sampler_kind::shadow},
};

// GLSL world_cache.frag (including #include fragments)
constexpr binding world_cache[]={
    {"albedo_map",0,sampler_kind::tex2d},
    {"albedo_map2",6,sampler_kind::tex2d},
    {"emissive_map",1,sampler_kind::tex2d},
    {"surface_map",2,sampler_kind::tex2d},
    {"normal_map",3,sampler_kind::tex2d},
    {"normal_map2",5,sampler_kind::tex2d},
    {"env_map",4,sampler_kind::tex2d},
    {"env_map2",7,sampler_kind::tex2d},
    {"palette_fullbright_mask",11,sampler_kind::tex2d},
    {"scene_depth",8,sampler_kind::tex2d},
    // world_cache_direct_specular.glsl includes forward sun samplers.
    {"translucent_sun_near_depth",16,sampler_kind::depth_variant},
    {"translucent_sun_far_depth",17,sampler_kind::depth_variant},
    {"translucent_sun_dynamic_depth",18,sampler_kind::shadow},
};

// GLSL game_opaque_pass.frag (including #include fragments)
constexpr binding game_opaque_pass[]={
    {"albedo_map",0,sampler_kind::tex2d},
    {"albedo_map2",8,sampler_kind::tex2d},
    {"emissive_map",1,sampler_kind::tex2d},
    {"displacement_map",2,sampler_kind::tex2d},
    {"surface_map",3,sampler_kind::tex2d},
    {"env_map",4,sampler_kind::tex2d},
    {"env_map2",9,sampler_kind::tex2d},
    {"normal_map",6,sampler_kind::tex2d},
    {"normal_map2",7,sampler_kind::tex2d},
    {"translucent_sun_near_depth",16,sampler_kind::depth_variant},
    {"translucent_sun_far_depth",17,sampler_kind::depth_variant},
    {"translucent_sun_dynamic_depth",18,sampler_kind::shadow},
};

// GLSL game_trns_pass.frag (including #include fragments)
constexpr binding game_trns_pass[]={
    {"albedo_map",0,sampler_kind::tex2d},
    {"albedo_map2",8,sampler_kind::tex2d},
    {"emissive_map",1,sampler_kind::tex2d},
    {"scene_depth",5,sampler_kind::tex2d},
    {"displacement_map",2,sampler_kind::tex2d},
    {"surface_map",3,sampler_kind::tex2d},
    {"env_map",4,sampler_kind::tex2d},
    {"env_map2",9,sampler_kind::tex2d},
    {"normal_map",6,sampler_kind::tex2d},
    {"normal_map2",7,sampler_kind::tex2d},
    {"translucent_shadow_image",10,sampler_kind::shadow},
    {"translucent_spotlight_image_0",12,sampler_kind::tex2d},
    {"translucent_spotlight_image_1",13,sampler_kind::tex2d},
    {"translucent_spotlight_image_2",14,sampler_kind::tex2d},
    {"translucent_spotlight_image_3",15,sampler_kind::tex2d},
    {"translucent_sun_near_depth",16,sampler_kind::depth_variant},
    {"translucent_sun_far_depth",17,sampler_kind::depth_variant},
    {"translucent_sun_dynamic_depth",18,sampler_kind::shadow},
};

// GLSL post_opaque_composite.frag (including #include fragments)
constexpr binding post_opaque_composite[]={
    {"color_image",0,sampler_kind::tex2d},
    {"emissive_image",1,sampler_kind::tex2d},
    {"occlusion_image",2,sampler_kind::tex2d},
    {"depth_image",3,sampler_kind::tex2d},
    {"normal_image",5,sampler_kind::tex2d},
    {"spotlight_image_0",6,sampler_kind::tex2d},
    {"spotlight_image_1",7,sampler_kind::tex2d},
    {"spotlight_image_2",8,sampler_kind::tex2d},
    {"spotlight_image_3",9,sampler_kind::tex2d},
    {"spotlight_shadow_image",10,sampler_kind::shadow},
    {"sun_shadow_image",13,sampler_kind::depth_variant},
    {"sun_shadow_far_image",14,sampler_kind::depth_variant},
    {"sun_dynamic_image",15,sampler_kind::shadow},
};

template<std::size_t N>
constexpr bool no_incompatible_target_aliases(binding const (&x)[N]) {
    for(std::size_t i=0;i<N;++i)
        for(std::size_t j=i+1;j<N;++j)
            if(x[i].unit==x[j].unit && x[i].kind!=x[j].kind)
                return false;
    return true;
}
static_assert(no_incompatible_target_aliases(gpu_3do_preview),"Sampler target collision in gpu_3do_preview");
static_assert(no_incompatible_target_aliases(world_cache),"Sampler target collision in world_cache");
static_assert(no_incompatible_target_aliases(game_opaque_pass),"Sampler target collision in game_opaque_pass");
static_assert(no_incompatible_target_aliases(game_trns_pass),"Sampler target collision in game_trns_pass");
static_assert(no_incompatible_target_aliases(post_opaque_composite),"Sampler target collision in post_opaque_composite");
static_assert(forward_sector_buffer != forward_next_extended_unit,
              "Sector samplerBuffer must not alias an array sampler");
}} // namespace jkgm::texture_units
