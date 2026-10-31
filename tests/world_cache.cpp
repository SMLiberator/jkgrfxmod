#include "renderer/world_cache.hpp"
#include "renderer/world_cache_portals.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace jkgm;

static decal_world_surface_geometry make_surface(uint32_t index,float x)
{
    decal_world_surface_geometry s{};
    s.surface_index=index;
    s.material=0x10000U+index;
    s.cel=3;
    s.normal={{0.0f,-1.0f,0.0f}};
    s.polygon={{{x,0.0f,0.0f}},
               {{x+2.0f,0.0f,0.0f}},
               {{x+2.0f,0.0f,2.0f}},
               {{x,0.0f,2.0f}}};
    s.has_shadow_uv=true;
    s.shadow_uv={{{0,0}},{{64,0}},{{64,64}},{{0,64}}};
    return s;
}

int main()
{
    decal_world_geometry missing_world{};
    world_cache_mesh invalid{};
    assert(!build_world_cache_mesh(missing_world,invalid));
    assert(invalid.vertices.empty());

    // A valid model-only level has a valid but empty world snapshot.
    missing_world.valid_world=true;
    assert(build_world_cache_mesh(missing_world,invalid));
    assert(invalid.indices.empty() && invalid.surfaces.empty());

    decal_world_geometry world{};
    world.valid_world=true;
    world.receivers.push_back(make_surface(2U,0.0f));
    world.receivers.push_back(make_surface(7U,10.0f));
    world.receivers[1].solid_adjoin=true;
    world.receivers[1].has_shadow_uv=false; // safe opaque fallback is native
    world_cache_mesh mesh{};
    assert(build_world_cache_mesh(world,mesh));
    assert(mesh.vertices.size()==8 && mesh.indices.size()==12);
    assert(mesh.surfaces.size()==2U);
    assert(mesh.surfaces[0].surface_index==2U);
    assert(mesh.surfaces[0].first_index==0U);
    assert(mesh.surfaces[0].count==6U);
    assert(mesh.surfaces[1].surface_index==7U);
    assert(mesh.surfaces[1].first_index==6U);
    assert(mesh.surfaces[1].count==6U);
    assert(mesh.surfaces[1].solid_adjoin);
    assert(mesh.uv_ready_surfaces==1U && mesh.missing_uv_surfaces==1U);
    assert(mesh.indices[0]==0U && mesh.indices[1]==1U &&
           mesh.indices[2]==2U && mesh.indices[3]==0U &&
           mesh.indices[4]==2U && mesh.indices[5]==3U);
    assert(mesh.indices[6]==4U && mesh.indices[11]==7U);
    assert(mesh.vertices[1].position[0]==2.0f);
    assert(mesh.vertices[2].uv[1]==64.0f);
    assert(mesh.vertices[5].uv[0]==0.0f); // missing source UV not fabricated
    assert(mesh.vertices[3].normal[1]==-1.0f);

    // A snapshot is immutable, but COG-adjusted GeoMode/cel are read live.
    constexpr uintptr_t base=0x100000U;
    std::array<uint8_t,0x10000U> storage{};
    auto set_u32=[&](uint32_t index,size_t offset,uint32_t value) {
        std::memcpy(storage.data()+size_t(index)*0x5cU+offset,
                    &value,sizeof(value));
    };
    set_u32(2,0x1c,4U); // visible
    set_u32(2,0x34,uint32_t(world.receivers[0].material));
    set_u32(2,0x38,3U);
    set_u32(7,0x1c,0U); // hidden by COG
    set_u32(7,0x34,uint32_t(world.receivers[1].material));
    set_u32(7,0x38,3U);
    decal_native_read read=[&](uintptr_t addr,void *dst,size_t n) {
        if(addr<base || addr-base>storage.size() ||
           n>storage.size()-(addr-base))return false;
        std::memcpy(dst,storage.data()+(addr-base),n);
        return true;
    };
    auto state=inspect_world_cache_live_state(read,base,mesh);
    assert(state.drawn_candidates==1U && state.hidden==1U &&
           state.missing_uv==0U && state.unreadable==0U);

    set_u32(7,0x1c,4U); // now visible, but missing UV remains native
    set_u32(2,0x38,4U); // animated MAT cel
    set_u32(2,0x34,0x20000U); // changed native material
    state=inspect_world_cache_live_state(read,base,mesh);
    assert(state.drawn_candidates==1U && state.hidden==0U);
    assert(state.missing_uv==1U && state.changed_cel==1U &&
           state.changed_material==1U);
    state=inspect_world_cache_live_state(
        decal_native_read{},base,mesh);
    assert(state.unreadable==2U);

    // OpenJKDF2 sector-list membership: validate entire pointers and
    // contiguous SithSurface spans before filtering any candidate.
    auto put32=[&](uintptr_t addr,uint32_t value) {
        assert(addr>=base && addr-base+4U<=storage.size());
        std::memcpy(storage.data()+(addr-base),&value,4U);
    };
    constexpr uintptr_t native_world=base+0x1000U;
    constexpr uintptr_t sector_table=base+0x2000U;
    constexpr uintptr_t list_addr=base+0x8000U;
    constexpr uintptr_t count_addr=base+0xa000U;
    put32(native_world+0x4cU,2U);
    put32(native_world+0x50U,uint32_t(sector_table));
    put32(sector_table+0x24U,1U);
    put32(sector_table+0x28U,uint32_t(base+2U*0x5cU));
    put32(sector_table+0x98U+0x24U,1U);
    put32(sector_table+0x98U+0x28U,uint32_t(base+7U*0x5cU));
    put32(count_addr,1U);
    put32(list_addr,uint32_t(sector_table));
    decal_world_geometry snapshot{};
    snapshot.valid_world=true;
    snapshot.world=native_world;
    snapshot.surface_count=9U;
    snapshot.surfaces_addr=base;
    std::vector<uint8_t> membership;
    std::vector<world_cache_draw_candidate> possible{{
        2U,0U,6U,0x10000U,0
    },{
        7U,6U,6U,0x10001U,0
    }};
    auto portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(portal.valid && portal.visible_sector_count==1U);
    assert(portal.member_surfaces==1U && portal.matched_candidates==1U);
    assert(portal.rejected_candidates==1U);
    assert(membership.size()==9U && membership[2U] && !membership[7U]);
    // A stale/corrupt visible-sector pointer must fail closed, not return
    // a partial membership mask.
    put32(list_addr,uint32_t(sector_table+1U));
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(!portal.valid && membership.empty());
    put32(list_addr,uint32_t(sector_table+0x98U));
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(portal.valid && portal.matched_candidates==1U &&
           membership[7U] && !membership[2U]);

    // The patched JKGM limit is 7000, not vanilla's 128 sectors.
    put32(native_world+0x4cU,129U);
    put32(count_addr,129U);
    for(uint32_t i=0;i<129U;++i)
        put32(list_addr+uintptr_t(i)*4U,
              uint32_t(sector_table+uintptr_t(i)*0x98U));
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(portal.valid && portal.visible_sector_count==129U);
    assert(portal.member_surfaces==2U && portal.matched_candidates==2U);
    put32(native_world+0x4cU,7002U);
    put32(count_addr,7001U);
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(!portal.valid && membership.empty());
    assert(std::strcmp(portal.reason,"count_or_table_out_of_range")==0);

    // Malformed/nonfinite geometry may not poison persistent GL buffers.
    world.receivers[0].polygon[0][0]=std::numeric_limits<float>::infinity();
    assert(!build_world_cache_mesh(world,mesh));
    assert(mesh.indices.empty());
    world.receivers[0].polygon[0][0]=0.0f;
    world.receivers[0].shadow_uv[0][0]=
        std::numeric_limits<float>::quiet_NaN();
    assert(build_world_cache_mesh(world,mesh));
    assert(mesh.uv_ready_surfaces==0U && mesh.missing_uv_surfaces==2U);
    return 0;
}
