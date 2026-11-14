#pragma once
// Source-Thing flare visibility. A native SPR type 4 tests its source
// SithThing.position, not a center reconstructed from clipped billboard UVs.
// The native D3D vertex specular word carries an indexed source reference
// until the renderer converts that triangle into the OpenGL flare batch.
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace jkgm {
struct sprite_flare_center {
    std::array<float,2> pixel{{0.0f,0.0f}};
    float view_depth=0.0f;
};

constexpr uint32_t sprite_flare_source_magic=0xB6A90000U;
constexpr uint32_t sprite_flare_source_mask=0xFFFF0000U;

inline bool sprite_flare_is_marker(uint32_t word) {
    return (word&sprite_flare_source_mask)==sprite_flare_source_magic;
}

class sprite_flare_source_registry {
    std::vector<std::array<float,3>> positions_;
public:
    // Index 0 is intentionally invalid: an unreadable source Thing must
    // never be assigned a plausible projected billboard-derived position.
    uint32_t add(std::array<float,3> const &source,bool valid=true) {
        if(!valid || positions_.size()>=0xFFFFU)
            return sprite_flare_source_magic;
        for(float coordinate:source)
            if(!std::isfinite(coordinate))
                return sprite_flare_source_magic;
        positions_.push_back(source);
        return sprite_flare_source_magic |
               static_cast<uint32_t>(positions_.size());
    }
    bool lookup(uint32_t marker,std::array<float,3> &out) const {
        if(!sprite_flare_is_marker(marker))return false;
        uint32_t const index=marker&0xFFFFU;
        if(!index || index>positions_.size())return false;
        out=positions_[index-1U];
        return true;
    }
    void clear() { positions_.clear(); }
    size_t size() const {return positions_.size();}
};

// All native submissions and ExecuteBuffer conversion use the same process
// registry on JK's render thread. Clear after consuming each frame.
inline sprite_flare_source_registry &sprite_flare_sources() {
    static sprite_flare_source_registry registry;
    return registry;
}

// Project the source Thing through JK's *actual* rdCamera world-to-view
// matrix. Matrix34 uses column-vector coordinates (right/look/up/translation).
// The resolved R16F scene-depth buffer stores native JK depth (32 * view Y),
// matching game_opaque_pass.frag and gpu_3do_preview.frag. The projected
// center must use these SAME units, not raw world-space view Y.
// No UV derivatives, triangle-center solve or temporal fade.
inline bool project_sprite_flare_thing_center(
    std::array<float,3> const &source,
    std::array<float,12> const &view,
    float tan_half_horizontal_fov,float aspect,
    std::array<float,2> viewport,sprite_flare_center &out)
{
    out={};
    if(!std::isfinite(tan_half_horizontal_fov) ||
       !(tan_half_horizontal_fov>0.0f) || !std::isfinite(aspect) ||
       !(aspect>0.0f) || !std::isfinite(viewport[0]) ||
       !std::isfinite(viewport[1]) || !(viewport[0]>0.0f) ||
       !(viewport[1]>0.0f))return false;
    for(float component:source)if(!std::isfinite(component))return false;
    for(float component:view)if(!std::isfinite(component))return false;
    auto transform=[&](int axis) {
        return double(view[axis])*source[0]+
               double(view[3+axis])*source[1]+
               double(view[6+axis])*source[2]+view[9+axis];
    };
    double const x=transform(0),depth=transform(1),z=transform(2);
    // JK's linear depth target stores 32 units per meter, not view Y.
    // The R16F sky sentinel begins at 65500; reject unsampleable centers.
    double const native_depth=depth*32.0;
    if(!std::isfinite(native_depth) || native_depth<=0.0 ||
       native_depth>=65500.0)
        return false;
    double const h=tan_half_horizontal_fov;
    double const v=h/aspect;
    double const ndcx=x/(depth*h),ndcy=z/(depth*v);
    double const px=(ndcx+1.0)*0.5*viewport[0];
    double const py=(ndcy+1.0)*0.5*viewport[1];
    if(!std::isfinite(px) || !std::isfinite(py) ||
       px<0.0 || px>=viewport[0] || py<0.0 || py>=viewport[1])
        return false;
    out.pixel={{float(px),float(py)}};
    out.view_depth=float(native_depth);
    return true;
}
} // namespace jkgm
