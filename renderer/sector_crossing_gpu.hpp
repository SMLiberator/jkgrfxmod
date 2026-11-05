#pragma once
// Per-3DO opt-in sector-crossing staging. Pure C++ / read-only engine state.
// The expensive plane/Adjoin snapshot belongs to the level, not the Thing;
// a crossing Thing gets only four small shading descriptors.
#include "sector_crossing.hpp"
#include "gpu_3do_preview.hpp"
#include "gpu_3do_modelview.hpp"
#include "jk_cmp_gpu.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace jkgm {
struct sector_crossing_plane_range {
    uint32_t first=0,count=0;
};
constexpr uint32_t sector_crossing_gpu_max_planes=96;

// Conservative bounding SPHERE of the transformed local mesh box.
// The affine RLU columns are not assumed normalized (scaled/joint meshes).
inline bool sector_crossing_instance_sphere(
    gpu_3do_instance const &instance,
    sector_crossing_vec3 &center,float &radius)
{
    if(!instance.geometry || instance.geometry->groups.empty() ||
       !gpu_3do_matrix_finite(instance.transform))return false;
    sector_crossing_vec3 lo{{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    }},hi{{-std::numeric_limits<float>::max(),
          -std::numeric_limits<float>::max(),
          -std::numeric_limits<float>::max()}};
    bool present=false;
    for(auto const &group:instance.geometry->groups) {
        if(!group.count)continue;
        if(!sector_crossing_valid_box(group.bounds_min,group.bounds_max))
            return false;
        for(int i=0;i<3;++i) {
            lo[i]=std::min(lo[i],group.bounds_min[i]);
            hi[i]=std::max(hi[i],group.bounds_max[i]);
        }
        present=true;
    }
    if(!present)return false;
    sector_crossing_vec3 local_center{},half{};
    for(int i=0;i<3;++i) {
        local_center[i]=lo[i]+(hi[i]-lo[i])*0.5f;
        half[i]=(hi[i]-lo[i])*0.5f;
    }
    center=gpu_3do_native_transform_point(instance.transform,local_center);
    if(!sector_crossing_finite(center))return false;
    // Test all eight affine corners of the local AABB. This is the
    // tightest origin-centered sphere around this transformed box,
    // including nonuniform scale, shear and mirrored R/L/U bases. The
    // previous sum of column lengths was conservative but needlessly
    // expanded the search to unrelated nearby sectors.
    double radius_squared=0.0;
    for(int x=-1;x<=1;x+=2)
        for(int y=-1;y<=1;y+=2)
            for(int z=-1;z<=1;z+=2) {
                double dx=double(instance.transform[0])*half[0]*x+
                          double(instance.transform[3])*half[1]*y+
                          double(instance.transform[6])*half[2]*z;
                double dy=double(instance.transform[1])*half[0]*x+
                          double(instance.transform[4])*half[1]*y+
                          double(instance.transform[7])*half[2]*z;
                double dz=double(instance.transform[2])*half[0]*x+
                          double(instance.transform[5])*half[1]*y+
                          double(instance.transform[8])*half[2]*z;
                radius_squared=std::max(radius_squared,dx*dx+dy*dy+dz*dz);
            }
    double r=std::sqrt(radius_squared);
    if(!std::isfinite(r)||r<0.0||r>100000.0)return false;
    radius=static_cast<float>(r);
    return true;
}

template<class Read>
std::shared_ptr<gpu_3do_crossing_state const>
sector_crossing_stage_instance(
    sector_crossing_snapshot const &snapshot,
    std::vector<sector_crossing_plane_range> const &plane_ranges,
    gpu_3do_instance const &instance,Read const &read,
    bool can_replace_tint,uint32_t identity_cmp=0U)
{
    if(!instance.owning_sector_valid || !instance.geometry ||
       instance.shadow_only || instance.camera_hud ||
       instance.is_sprite_quad || instance.is_projected_decal ||
       instance.transient_procedural ||
       snapshot.world==0 || plane_ranges.size()!=snapshot.sectors.size())
        return {};
    sector_crossing_vec3 origin{};
    float radius=0.0f;
    if(!sector_crossing_instance_sphere(instance,origin,radius))return {};
    auto const choices=sector_crossing_collect_candidates(
        snapshot,origin,radius,instance.owning_sector_id);
    if(choices.overflow || choices.count<2 ||
       choices.count>sector_crossing_candidates::capacity)return {};
    auto result=std::make_shared<gpu_3do_crossing_state>();
    result->count=choices.count;
    float owner_light=0.0f;
    for(uint32_t i=0;i<choices.count;++i) {
        auto const index=choices.indices[i];
        if(index>=snapshot.sectors.size())return {};
        auto const &sector=snapshot.sectors[index];
        auto const &range=plane_ranges[index];
        if(!sector.valid || range.count==0U ||
           range.count>sector_crossing_gpu_max_planes ||
           range.count!=sector.planes.size() ||
           (i==0 && sector.id!=instance.owning_sector_id))return {};
        // +0x04 SithSector.ambientLight, +0x08 extraLight,
        // +0x0c CMP pointer, +0x10 tint RGB: 24 contiguous bytes.
        std::array<uint8_t,24> native{};
        if(!read(sector.native_sector+0x04U,native.data(),native.size()))
            return {};
        float ambient=0.0f,extra=0.0f;
        uint32_t cmp=0;
        std::array<float,3> tint{};
        std::memcpy(&ambient,native.data(),4);
        std::memcpy(&extra,native.data()+4,4);
        std::memcpy(&cmp,native.data()+8,4);
        std::memcpy(tint.data(),native.data()+12,12);
        if(!std::isfinite(ambient) || !std::isfinite(extra) ||
           ambient< -8.0f || ambient>8.0f ||
           extra< -8.0f || extra>8.0f)return {};
        for(float t:tint)
            if(!std::isfinite(t) || t<0.0f || t>1.0f)return {};
        auto &slot=result->sectors[i];
        slot.bounds_min=sector.bounds_min;
        slot.bounds_max=sector.bounds_max;
        slot.interior_min=sector.interior_min;
        slot.interior_max=sector.interior_max;
        slot.interior_valid=sector.has_interior_box;
        slot.plane_start=range.first;
        slot.plane_count=range.count;
        if(i==0)owner_light=ambient+extra;
        // Preserve global sithRender extra, IR override and possible
        // Thing emission in the native per-Thing rdCamera snapshot by
        // adjusting it by the candidate sector's relative ambient delta.
        slot.ambient=i==0?instance.ambient:
            std::clamp(instance.ambient+(ambient+extra)-owner_light,0.0f,1.0f);
        if(i==0) {
            slot.cmp_tint=instance.colormap_tint;
            slot.sector_tint=can_replace_tint && instance.sector_tint_valid
                ? instance.sector_tint : std::array<float,3>{{0.0f,0.0f,0.0f}};
        } else {
            // The native cache skips the multiplier on the identity CMP.
            // Explicit zero on a NON-identity CMP is still meaningful.
            std::array<float,3> sampled{};
            if(jk_cmp_read_effective_tint(read,cmp,identity_cmp,sampled))
                slot.cmp_tint=sampled;
            if(can_replace_tint)slot.sector_tint=tint;
        }
    }
    return result;
}
} // namespace jkgm
