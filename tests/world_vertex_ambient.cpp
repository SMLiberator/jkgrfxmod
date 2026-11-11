#include "renderer/world_vertex_ambient.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

namespace {
struct memory {
    std::unordered_map<uintptr_t,uint8_t> bytes;
    template<class T>
    void put(uintptr_t addr,T const &value) {
        auto const *src=reinterpret_cast<uint8_t const *>(&value);
        for(size_t i=0;i<sizeof(value);++i)bytes[addr+i]=src[i];
    }
    bool read(uintptr_t addr,void *dst,size_t size) const {
        auto *out=static_cast<uint8_t *>(dst);
        for(size_t i=0;i<size;++i) {
            auto const f=bytes.find(addr+i);
            if(f==bytes.end())return false;
            out[i]=f->second;
        }
        return true;
    }
};
} // namespace

int main() {
    jkgm::world_vertex_ambient_grid grid;
    float result=-1.0f;
    assert(!grid.sample(7,{{0.0f,0.0f,0.0f}},result));
    assert(grid.add(7,{{0.0f,0.0f,0.0f}},0.0f));
    assert(grid.add(7,{{0.5f,0.0f,0.0f}},2.0f));
    assert(grid.add(8,{{0.0f,0.0f,0.0f}},0.5f));
    assert(grid.sample(7,{{0.1f,0.0f,0.0f}},result));
    assert(std::fabs(result-1.0f)<0.00001f); // same cell average
    assert(grid.sample(8,{{0.1f,0.0f,0.0f}},result));
    assert(std::fabs(result-0.5f)<0.00001f); // same point, other sector
    result=123.0f;
    assert(!grid.sample(7,{{100.0f,0.0f,0.0f}},result));
    assert(result==123.0f); // native ambient fallback remains possible
    assert(!grid.add(7,{{0.0f,0.0f,0.0f}},-1.0f));
    assert(!grid.add(7,{{0.0f,0.0f,0.0f}},
                    std::numeric_limits<float>::infinity()));
    assert(grid.sample_count()==3U);

    // Exercise the actual native SithWorld surface layout and sector IDs.
    uintptr_t constexpr world=0x00100000U;
    uintptr_t constexpr sectors=0x00200000U;
    uintptr_t constexpr surfaces=0x00300000U;
    uintptr_t constexpr lights0=0x00400000U;
    uintptr_t constexpr lights1=0x00400100U;
    uintptr_t constexpr lights_sky=0x00400200U;
    uintptr_t constexpr portal_adjoin=0x00500000U;
    uintptr_t constexpr portal_indices=0x00510000U;
    uintptr_t constexpr world_vertices=0x00600000U;
    memory mem;
    mem.put(world+0x4cU,uint32_t(2));
    mem.put(world+0x50U,uint32_t(sectors));
    mem.put(world+0x88U,uint32_t(3));
    mem.put(world+0x8cU,uint32_t(world_vertices));
    mem.put(world+0xa8U,uint32_t(4)); // 4th face: invisible portal
    mem.put(world+0xacU,uint32_t(surfaces));
    mem.put(sectors,uint32_t(7));
    mem.put(sectors+0x08U,1.9f); // a bright authored sector floor
    mem.put(sectors+0x98U,uint32_t(8));
    mem.put(sectors+0x98U+0x08U,0.05f); // dim neighboring sector
    auto face=[&](uint32_t index,uint32_t sector,uint32_t ptr,
                  float extra) {
        uintptr_t const f=surfaces+uintptr_t(index)*0x5cU;
        mem.put(f+0x08U,sector);
        mem.put(f+0x0cU,uint32_t(0));
        mem.put(f+0x10U,uint32_t(0));
        mem.put(f+0x1cU,uint32_t(4)); // textured
        mem.put(f+0x20U,uint32_t(3)); // Gouraud
        mem.put(f+0x44U,extra);
        mem.put(f+0x54U,ptr);
    };
    face(0,uint32_t(sectors),uint32_t(lights0),0.2f);
    face(1,uint32_t(sectors+0x98U),uint32_t(lights1),0.0f);
    face(2,uint32_t(sectors),uint32_t(lights_sky),0.0f);
    mem.put(lights0+0U,0.0f);
    mem.put(lights0+4U,0.5f);
    mem.put(lights0+8U,1.5f);
    for(uint32_t i=0;i<3;++i) {
        mem.put(lights1+i*4U,2.0f);
        mem.put(lights_sky+i*4U,2.0f);
    }
    // This portal is invisible GeoMode 0, has no MAT, and is NOT in
    // world_cache_mesh. The native table must be authoritative.
    uintptr_t const portal_face=surfaces+3U*0x5cU;
    mem.put(portal_face+0x08U,uint32_t(sectors));
    mem.put(portal_face+0x0cU,uint32_t(portal_adjoin));
    mem.put(portal_face+0x10U,uint32_t(0)); // not sky
    mem.put(portal_face+0x1cU,uint32_t(0)); // invisible
    mem.put(portal_face+0x28U,uint32_t(3));
    mem.put(portal_face+0x2cU,uint32_t(portal_indices));
    mem.put(portal_adjoin,uint32_t(0x2U)); // passable
    mem.put(portal_adjoin+0x04U,uint32_t(sectors+0x98U));
    std::array<std::array<float,3>,3> const aperture{{
        {{0.0f,-0.5f,-0.5f}},
        {{0.0f, 0.5f,-0.5f}},
        {{0.0f, 0.0f, 0.5f}}
    }};
    for(uint32_t i=0;i<3;++i) {
        mem.put(portal_indices+uintptr_t(i)*4U,i);
        for(uint32_t k=0;k<3;++k)
            mem.put(world_vertices+uintptr_t(i)*12U+
                    uintptr_t(k)*4U,aperture[i][k]);
    }
    jkgm::world_cache_mesh mesh{};
    mesh.vertices.resize(9);
    mesh.indices={0,1,2,3,4,5,6,7,8};
    for(size_t i=0;i<mesh.vertices.size();++i)
        mesh.vertices[i].position={{0.2f,0.1f,0.0f}};
    jkgm::world_cache_surface_range range{};
    range.surface_index=0;range.first_index=0;range.count=3;
    mesh.surfaces.push_back(range);
    range.surface_index=1;range.first_index=3;
    mesh.surfaces.push_back(range);
    range.surface_index=2;range.first_index=6;
    range.surface_flags=0x200U; // sky is excluded from probes
    mesh.surfaces.push_back(range);
    auto reader=[&](uintptr_t addr,void *dst,size_t n) {
        return mem.read(addr,dst,n);
    };
    jkgm::world_vertex_ambient_stats stats{};
    assert(jkgm::capture_world_vertex_ambient(
        reader,world,surfaces,mesh,true,1.0f,grid,stats));
    assert(stats.surfaces_seen==3 && stats.surfaces_sampled==2);
    assert(stats.surfaces_skipped==1 && stats.vertices_sampled==6);
    assert(grid.sample_count()==6U && grid.cell_count()==2U);
    assert(stats.portals_captured==1U && grid.portal_count()==1U);
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-1.9f)<0.00001f); // sector ExtraLight is a floor
    assert(grid.sample(8,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-2.0f)<0.00001f);
    assert(!grid.sample(9,{{0.2f,0.1f,0.0f}},result));
    // The disabled path also caps the completed vertex + face sum at 1.
    assert(jkgm::capture_world_vertex_ambient(
        reader,world,surfaces,mesh,false,1.0f,grid,stats));
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-1.0f)<0.00001f);
    // Valid but distant geometry must not cause light bleed.
    assert(!grid.sample(7,{{10.0f,0.1f,0.0f}},result));

    // Exactly on the portal plane both owners must produce the same blend.
    float from_seven=0.0f,from_eight=0.0f;
    auto transition=grid.sample_with_portals(
        7,{{0.0f,0.1f,0.0f}},from_seven);
    auto reverse=grid.sample_with_portals(
        8,{{0.0f,0.1f,0.0f}},from_eight);
    assert(transition==jkgm::world_vertex_ambient_result::mixed);
    assert(reverse==jkgm::world_vertex_ambient_result::mixed);
    assert(std::fabs(from_seven-from_eight)<0.00001f);
    // On either side the result should vary gradually, not step on owner swap.
    grid.sample_with_portals(7,{{0.01f,0.1f,0.0f}},from_seven);
    grid.sample_with_portals(8,{{-0.01f,0.1f,0.0f}},from_eight);
    assert(std::fabs(from_seven-from_eight)<0.05f);

    // The new 2x2x2 GPU volume reuses exactly this passable-portal
    // geometry without legacy cell probes. Neighbor selection and mixing
    // must be symmetric as the native owning sector changes.
    uint32_t neighbor=0;
    float weight=0.0f;
    assert(grid.nearest_portal(7,{{0.0f,0.1f,0.0f}},neighbor,weight));
    assert(neighbor==8U && std::fabs(weight-0.5f)<0.00001f);
    assert(grid.nearest_portal(8,{{0.0f,0.1f,0.0f}},neighbor,weight));
    assert(neighbor==7U && std::fabs(weight-0.5f)<0.00001f);
    assert(grid.nearest_portal(7,{{0.5f,0.1f,0.0f}},neighbor,weight));
    assert(neighbor==8U && weight>0.0f && weight<0.5f);
    assert(!grid.nearest_portal(
        7,{{1.01f,0.1f,0.0f}},neighbor,weight));
    assert(weight==0.0f);

    // Near an infinite extension of the plane, but outside the real
    // polygon/radius: don't pick up the neighboring sector.
    // The grid now applies the registered sector's ExtraLight floor even
    // to test-injected samples; choose a value above that floor here.
    assert(grid.add(7,{{0.0f,2.5f,0.0f}},1.3f));
    assert(grid.add(8,{{0.0f,2.5f,0.0f}},1.8f));
    assert(grid.sample_with_portals(
        7,{{0.0f,2.5f,0.0f}},result)==
        jkgm::world_vertex_ambient_result::local);
    assert(std::fabs(result-1.3f)<0.00001f);
    assert(!grid.nearest_portal(
        7,{{0.0f,2.5f,0.0f}},neighbor,weight));

    // A solid Adjoin must not contribute a portal, even if geometry is
    // nearby and the level still contains world vertices in both sectors.
    mem.put(portal_adjoin,uint32_t(0));
    assert(jkgm::capture_world_vertex_ambient(
        reader,world,surfaces,mesh,true,1.0f,grid,stats));
    assert(stats.portals_captured==0U && grid.portal_count()==0U);
    assert(!grid.nearest_portal(
        7,{{0.0f,0.1f,0.0f}},neighbor,weight));
    assert(grid.sample_with_portals(
        7,{{0.0f,0.1f,0.0f}},result)==
        jkgm::world_vertex_ambient_result::local);

    // A sky-flagged adjoin is not a valid world ambient portal either.
    mem.put(portal_adjoin,uint32_t(2));
    mem.put(portal_face+0x10U,uint32_t(0x200U));
    assert(jkgm::capture_world_vertex_ambient(
        reader,world,surfaces,mesh,true,1.0f,grid,stats));
    assert(grid.portal_count()==0U);
    assert(!grid.nearest_portal(
        7,{{0.0f,0.1f,0.0f}},neighbor,weight));

    // If the Thing's home sector lacks nearby samples, the portal can
    // borrow a neighboring sample without falling back to JKL ambient.
    grid.reset();
    assert(grid.add(8,{{0.0f,0.0f,0.0f}},0.4f));
    std::vector<std::array<float,3>> const opening{
        {{0.0f,-0.5f,-0.5f}},
        {{0.0f, 0.5f,-0.5f}},
        {{0.0f, 0.0f, 0.5f}}
    };
    assert(grid.add_portal(7,8,opening));
    assert(grid.sample_with_portals(
        7,{{0.0f,0.0f,0.0f}},result)==
        jkgm::world_vertex_ambient_result::portal_only);
    assert(std::fabs(result-0.4f)<0.00001f);

    // A fully dark vertex array in a sector with 1.0 ExtraLight must
    // sample as lit, even though the static vertex intensities are zero.
    // Use the same native world lighting policy as world_cache_lighting.hpp.
    mem.put(portal_face+0x10U,uint32_t(0)); // restore ordinary adjoin
    mem.put(sectors+0x08U,1.0f);
    mem.put(surfaces+0x44U,0.0f);
    for(uint32_t i=0;i<3;++i)mem.put(lights0+i*4U,0.0f);
    assert(jkgm::capture_world_vertex_ambient(
        reader,world,surfaces,mesh,true,1.0f,grid,stats));
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-1.0f)<0.00001f);

    // The authored face extra is additive before taking the sector floor.
    // It may increase brightness even when the sector ExtraLight is zero.
    mem.put(sectors+0x08U,0.0f);
    mem.put(surfaces+0x44U,0.3f);
    assert(jkgm::capture_world_vertex_ambient(
        reader,world,surfaces,mesh,true,1.0f,grid,stats));
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-0.3f)<0.00001f);

    // Negative sector ExtraLight must not darken authored vertex values.
    mem.put(sectors+0x08U,-0.8f);
    mem.put(lights0,0.6f);
    assert(jkgm::capture_world_vertex_ambient(
        reader,world,surfaces,mesh,true,1.0f,grid,stats));
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-(0.9f+0.3f+0.3f)/3.0f)<0.00001f);

    // The next tests emulate SetSectorLight's intermediate fade steps,
    // without rebuilding the world cache or its passable-portal polygons.
    // A per-cell mean of max(raw,sector) differs from max(mean(raw),sector).
    mem.put(sectors+0x08U,0.0f);
    mem.put(surfaces+0x44U,0.0f);
    mem.put(lights0,0.2f);
    mem.put(lights0+4U,1.4f);
    mem.put(lights0+8U,0.6f);
    assert(jkgm::capture_world_vertex_ambient(
        reader,world,surfaces,mesh,true,1.0f,grid,stats));
    assert(grid.tracked_sector_count()==2U);
    assert(grid.portal_count()==1U);
    size_t const saved_cells=grid.cell_count();
    uint32_t const saved_samples=grid.sample_count();
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-(0.2f+1.4f+0.6f)/3.0f)<0.00001f);
    mem.put(sectors+0x08U,1.0f);
    auto change=grid.refresh_sector_floors(reader,true);
    assert(change.checked==2U && change.changed==1U);
    assert(change.cells==1U && change.vertices==3U);
    assert(change.unreadable==0U);
    assert(grid.cell_count()==saved_cells && grid.sample_count()==saved_samples);
    assert(grid.portal_count()==1U);
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-(1.0f+1.4f+1.0f)/3.0f)<0.00001f);
    assert(grid.sample(8,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-2.0f)<0.00001f);
    change=grid.refresh_sector_floors(reader,true);
    assert(change.changed==0U && change.cells==0U && change.vertices==0U);

    // A fade step affects precisely one sector's cells, even as the
    // object continues to sample across the unchanged passable portal.
    mem.put(sectors+0x08U,0.5f);
    change=grid.refresh_sector_floors(reader,true);
    assert(change.changed==1U && change.cells==1U);
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-(0.5f+1.4f+0.6f)/3.0f)<0.00001f);
    float blended=0.0f;
    assert(grid.sample_with_portals(
        7,{{0.0f,0.1f,0.0f}},blended)==
        jkgm::world_vertex_ambient_result::mixed);
    assert(blended>0.8f && blended<2.0f);

    // Failed native reads do not wipe out an earlier valid sector floor.
    mem.put(sectors+0x08U,1.8f);
    auto unreadable=[&](uintptr_t addr,void *dst,size_t size) {
        return addr==sectors+0x08U?false:reader(addr,dst,size);
    };
    change=grid.refresh_sector_floors(unreadable,true);
    assert(change.unreadable==1U && change.changed==0U);
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-(0.5f+1.4f+0.6f)/3.0f)<0.00001f);
    change=grid.refresh_sector_floors(reader,true);
    assert(change.unreadable==0U && change.changed==1U);
    assert(grid.sample(7,{{0.2f,0.1f,0.0f}},result));
    assert(std::fabs(result-1.8f)<0.00001f);

    // An effective floor already capped at 2 stays unchanged when an
    // authored value above the cap changes again.
    mem.put(sectors+0x08U,5.0f);
    change=grid.refresh_sector_floors(reader,true);
    assert(change.changed==1U);
    mem.put(sectors+0x08U,6.0f);
    change=grid.refresh_sector_floors(reader,true);
    assert(change.changed==0U && change.cells==0U);

    // Native sector ID zero is legal. It must not be used as a sentinel
    // for a missing neighbor in the geometry-only portal lookup.
    grid.reset();
    assert(grid.add_portal(1U,0U,opening));
    assert(grid.nearest_portal(
        1U,{{0.0f,0.0f,0.0f}},neighbor,weight));
    assert(neighbor==0U && std::fabs(weight-0.5f)<0.00001f);
    assert(grid.nearest_portal(
        0U,{{0.0f,0.0f,0.0f}},neighbor,weight));
    assert(neighbor==1U && std::fabs(weight-0.5f)<0.00001f);
}
