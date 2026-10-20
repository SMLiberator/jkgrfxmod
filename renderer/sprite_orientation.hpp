#pragma once
// Shared Sprite orientation rules. JK's legacy type 2 follows the fixed-plane
// behavior of IJIM; extended flags occupy bits 2-5 of the type integer.
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
    constexpr uint32_t sprite_preserve_up=0x04U;
    constexpr uint32_t sprite_preserve_look=0x08U;
    constexpr uint32_t sprite_preserve_right=0x10U;
    constexpr uint32_t sprite_project_decal=0x20U;
    constexpr uint32_t sprite_extended_mask=0x3CU;

    using sprite_vec3=std::array<float,3>;
    struct sprite_axes {
        sprite_vec3 right{{1,0,0}}, look{{0,1,0}}, up{{0,0,1}};
    };
    inline float sprite_dot(sprite_vec3 const &a,sprite_vec3 const &b)
    { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
    inline sprite_vec3 sprite_cross(sprite_vec3 const &a,sprite_vec3 const &b)
    { return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}}; }
    inline bool sprite_normalize(sprite_vec3 &a)
    {
        auto const d=sprite_dot(a,a);
        if(!std::isfinite(d) || d<1.e-8f) return false;
        float const inv=1.0f/std::sqrt(d);
        for(float &x:a) x*=inv;
        return true;
    }
    inline bool sprite_project_orthogonal(sprite_vec3 &a,sprite_vec3 const &normal)
    {
        float const d=sprite_dot(a,normal);
        for(int i=0;i<3;i++) a[i]-=d*normal[i];
        return sprite_normalize(a);
    }
    // Returns false for invalid vectors, so caller may use vanilla rendering.
    inline bool compute_sprite_axes(uint32_t type,sprite_axes const &thing,
                                    sprite_axes const &camera,sprite_axes &out)
    {
        // Infernal Machine's type 2 keeps the Thing's Look direction, and
        // derives its horizontal axis from world Up. JK natively ignores SPR
        // types, so supply that legacy behavior here. Type 3 (undocumented
        // combination) and type 1 remain vanilla camera-facing.
        auto bits=type&sprite_extended_mask;
        if(bits==0 && type==2U) {
            out.look=thing.look;
            if(!sprite_normalize(out.look)) return false;
            sprite_vec3 const world_up{{0,0,1}};
            sprite_vec3 const fallback{{1,0,0}};
            out.right=sprite_cross(out.look,world_up);
            if(!sprite_normalize(out.right)) {
                out.right=sprite_cross(fallback,out.look);
                if(!sprite_normalize(out.right)) return false;
            }
            out.up=sprite_cross(out.right,out.look);
            return sprite_normalize(out.up);
        }
        if(bits==0) {out=camera;return true;}
        if((bits&sprite_project_decal)!=0) bits|=sprite_preserve_up|
            sprite_preserve_look|sprite_preserve_right;

        bool const right=(bits&sprite_preserve_right)!=0;
        bool const look=(bits&sprite_preserve_look)!=0;
        bool const up=(bits&sprite_preserve_up)!=0;

        out=sprite_axes{};
        if(right && look && up) {
            out=thing;
            return sprite_normalize(out.right)&&sprite_normalize(out.look)&&
                   sprite_normalize(out.up);
        }
        if(right && look) {
            out.right=thing.right;out.look=thing.look;
            if(!sprite_normalize(out.right)||
               !sprite_project_orthogonal(out.look,out.right)) return false;
            out.up=sprite_cross(out.right,out.look);
            return sprite_normalize(out.up);
        }
        if(right && up) {
            out.right=thing.right;out.up=thing.up;
            if(!sprite_normalize(out.right)||
               !sprite_project_orthogonal(out.up,out.right)) return false;
            out.look=sprite_cross(out.up,out.right);
            return sprite_normalize(out.look);
        }
        if(look && up) {
            out.look=thing.look;out.up=thing.up;
            if(!sprite_normalize(out.look)||
               !sprite_project_orthogonal(out.up,out.look)) return false;
            out.right=sprite_cross(out.look,out.up);
            return sprite_normalize(out.right);
        }
        if(up) {
            // Retain camera horizontal orientation through steep pitch.
            // Projecting camera Look was ill-conditioned near top-down
            // views and could suddenly select an unrelated Thing axis.
            out.up=thing.up;out.right=camera.right;
            if(!sprite_normalize(out.up)) return false;
            if(!sprite_project_orthogonal(out.right,out.up)) {
                out.look=camera.look;
                if(!sprite_project_orthogonal(out.look,out.up)) {
                    out.look=thing.look;
                    if(!sprite_project_orthogonal(out.look,out.up)) return false;
                }
                out.right=sprite_cross(out.look,out.up);
                if(!sprite_normalize(out.right)) return false;
            }
            out.look=sprite_cross(out.up,out.right);
            return sprite_normalize(out.look);
        }
        if(look) {
            out.look=thing.look;out.up=camera.up;
            if(!sprite_normalize(out.look)) return false;
            if(!sprite_project_orthogonal(out.up,out.look)) {
                out.up=thing.up;
                if(!sprite_project_orthogonal(out.up,out.look)) return false;
            }
            out.right=sprite_cross(out.look,out.up);
            return sprite_normalize(out.right);
        }
        if(right) {
            out.right=thing.right;out.up=camera.up;
            if(!sprite_normalize(out.right)) return false;
            if(!sprite_project_orthogonal(out.up,out.right)) {
                out.up=thing.up;
                if(!sprite_project_orthogonal(out.up,out.right)) return false;
            }
            out.look=sprite_cross(out.up,out.right);
            return sprite_normalize(out.look);
        }
        out=camera;
        return true;
    }
}
