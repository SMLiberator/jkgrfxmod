#pragma once
// SPR 'type' is a mutually exclusive mode, not a bitmask.
// 0 = classic JK camera-facing billboard
// 1 = cylindrical billboard: preserve Thing Up while facing the camera
// 2 = fixed plane: preserve the Thing's complete Right/Look/Up basis
// 3 = projected decal, based on fixed-plane projector axes
// 4 = center-occluded screen-facing flare
// Values 5..127 are reserved (unknown types fall back to classic behavior).
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
    constexpr uint32_t sprite_default=0U;
    constexpr uint32_t sprite_billboard=1U;
    constexpr uint32_t sprite_fixed=2U;
    constexpr uint32_t sprite_project_decal=3U;
    constexpr uint32_t sprite_center_occluded_flare=4U;

    inline bool sprite_is_flare(uint32_t type) {
        return type==sprite_center_occluded_flare;
    }
    inline bool sprite_is_decal(uint32_t type) {
        return type==sprite_project_decal;
    }
    inline bool sprite_needs_custom_axes(uint32_t type) {
        return type==sprite_billboard || type==sprite_fixed ||
               type==sprite_project_decal;
    }

    using sprite_vec3=std::array<float,3>;
    struct sprite_axes {
        sprite_vec3 right{{1,0,0}},look{{0,1,0}},up{{0,0,1}};
    };
    inline float sprite_dot(sprite_vec3 const &a,sprite_vec3 const &b)
    { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
    inline sprite_vec3 sprite_cross(sprite_vec3 const &a,sprite_vec3 const &b)
    { return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}}; }
    inline bool sprite_normalize(sprite_vec3 &a)
    {
        float const d=sprite_dot(a,a);
        if(!std::isfinite(d)||d<1.e-8f) return false;
        float const inv=1.0f/std::sqrt(d);
        for(float &x:a)x*=inv;
        return true;
    }
    inline bool sprite_project_orthogonal(sprite_vec3 &a,sprite_vec3 const &normal)
    {
        float const d=sprite_dot(a,normal);
        for(int i=0;i<3;i++)a[i]-=d*normal[i];
        return sprite_normalize(a);
    }
    inline bool compute_sprite_axes(uint32_t type,sprite_axes const &thing,
                                    sprite_axes const &camera,sprite_axes &out)
    {
        out=camera; // also the safe fallback for types 5..127
        if(type==sprite_default || type==sprite_center_occluded_flare ||
           !sprite_needs_custom_axes(type))return true;

        if(type==sprite_fixed || type==sprite_project_decal) {
            out.right=thing.right;
            out.look=thing.look;
            out.up=thing.up;
            if(!sprite_normalize(out.right) ||
               !sprite_project_orthogonal(out.look,out.right))return false;
            out.up=sprite_cross(out.right,out.look);
            if(!sprite_normalize(out.up))return false;
            // Do not silently flip a fixed-plane projector whose source axes
            // were degenerate or mirrored; the clipping direction matters.
            if(sprite_dot(out.up,thing.up)<0.5f)return false;
            return true;
        }

        // Cylindrical billboard (type 1): preserve Up, aim Look toward camera.
        out.up=thing.up;
        if(!sprite_normalize(out.up))return false;
        out.look=camera.look;
        if(sprite_project_orthogonal(out.look,out.up)) {
            out.right=sprite_cross(out.look,out.up);
            return sprite_normalize(out.right);
        }
        // Camera looks almost along the locked Up axis. Use a stable
        // orthogonal tangent, rather than producing a zero-area quad.
        out.right=camera.right;
        if(!sprite_project_orthogonal(out.right,out.up)) {
            out.right=thing.right;
            if(!sprite_project_orthogonal(out.right,out.up))return false;
        }
        out.look=sprite_cross(out.up,out.right);
        return sprite_normalize(out.look);
    }
}
