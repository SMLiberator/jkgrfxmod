// CPU-only reflected-sector portal tests. No OpenGL context or JK process.
#include "renderer/planar_reflection_portals.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_map>

int main() {
    using namespace jkgm;
    std::unordered_map<uintptr_t,uint8_t> memory;
    auto put=[&](uintptr_t address,uint32_t value) {
        uint8_t src[4]{};
        std::memcpy(src,&value,4);
        for(unsigned i=0;i<4;++i)memory[address+i]=src[i];
    };
    decal_native_read read=[&](uintptr_t address,void *dst,size_t n) {
        auto *out=static_cast<uint8_t*>(dst);
        for(size_t i=0;i<n;++i) {
            auto const it=memory.find(address+i);
            if(it==memory.end())return false;
            out[i]=it->second;
        }
        return true;
    };
    constexpr uintptr_t world=0x20000U;
    constexpr uintptr_t sectors=0x30000U;
    constexpr uintptr_t surfaces=0x40000U;
    constexpr uintptr_t vertices=0x50000U;
    constexpr uintptr_t link0=0x70000U;
    constexpr uintptr_t link1=0x70040U;
    put(world+0x4cU,3U);
    put(world+0x50U,uint32_t(sectors));
    // Three sectors with one owned surface each; two portal links.
    for(uint32_t i=0;i<3;++i) {
        uintptr_t const sector=sectors+uintptr_t(i)*0x98U;
        put(sector+0x24U,1U);
        put(sector+0x28U,uint32_t(surfaces+i*0x5cU));
        put(sector+0x2cU,uint32_t(i==0?link0:i==1?link1:0U));
    }
    auto link=[&](uintptr_t at,uintptr_t dest,uintptr_t surf) {
        put(at,1U);                            // visibility flag
        put(at+4U,uint32_t(dest));              // neighboring sector
        put(at+8U,uint32_t(surf));              // portal surface in owner
        put(at+12U,0U);                        // mirror link unused
        put(at+16U,0U);                        // next
    };
    link(link0,sectors+0x98U,surfaces);
    link(link1,sectors+2U*0x98U,surfaces+0x5cU);

    decal_world_geometry geometry{};
    geometry.valid_world=true;
    geometry.world=world;
    geometry.vertex_count=24U;
    geometry.vertices_addr=vertices;
    geometry.surface_count=3U;
    geometry.surfaces_addr=surfaces;
    auto surface=[&](uint32_t index,float y,float x) {
        decal_world_surface_geometry s{};
        s.surface_index=index;
        s.material=0x10000U;
        s.normal={{0.0f,-1.0f,0.0f}};
        s.polygon={{{x-1.0f,y,-1.0f}},{{x+1.0f,y,-1.0f}},
                   {{x+1.0f,y,1.0f}},{{x-1.0f,y,1.0f}}};
        s.bounds_min={{x-1.0f,y,-1.0f}};
        s.bounds_max={{x+1.0f,y,1.0f}};
        return s;
    };
    geometry.receivers.push_back(surface(0U,5.0f,0.0f));
    geometry.receivers.push_back(surface(1U,10.0f,0.0f));
    geometry.receivers.push_back(surface(2U,15.0f,0.0f));
    world_cache_mesh mesh{};
    assert(build_world_cache_mesh(geometry,mesh));
    std::vector<uint32_t> lookup;
    assert(build_world_cache_surface_lookup(mesh,3U,lookup));
    gpu_3do_matrix34 view{};
    view[0]=1.0f;view[4]=1.0f;view[8]=1.0f;
    planar_reflection_portal_graph graph{};
    assert(graph.ensure(read,world,1U,geometry,mesh,lookup));
    std::vector<world_cache_sector_span> spans;
    auto result=graph.traverse({0U},mesh,lookup,view,1.0f,1.0f,spans);
    assert(result.valid);
    assert(result.seed_sectors==1U);
    assert(result.visible_sectors==3U);
    assert(result.surfaces_reachable==3U);
    assert(spans.size()==3U);
    // Disconnected or unprojectable portals must not invent reachability.
    auto distant_mesh=mesh;
    auto &portal=distant_mesh.surfaces[1];
    portal.bounds_min[0]=100.0f;
    portal.bounds_max[0]=102.0f;
    result=graph.traverse({0U},distant_mesh,lookup,view,1.0f,1.0f,spans);
    // Geometry bounds were cached by graph: rebuild on topology revision.
    graph.clear();
    assert(graph.ensure(read,world,2U,geometry,distant_mesh,lookup));
    result=graph.traverse({0U},distant_mesh,lookup,view,1.0f,1.0f,spans);
    assert(result.valid && result.visible_sectors==2U);
    // A near-plane crossing AABB fails OPEN to full viewport.
    auto near_rect=planar_reflection_portal_project_aabb(
        {{-1.0f,-1.0f,-1.0f}},{{1.0f,1.0f,1.0f}},
        view,1.0f,1.0f);
    assert(!near_rect.empty && near_rect.left== -1.0f &&
           near_rect.right==1.0f);
    auto first=planar_reflection_portal_project_aabb(
        {{-1.0f,5.0f,-1.0f}},{{1.0f,5.0f,1.0f}},
        view,1.0f,1.0f);
    auto second=planar_reflection_portal_project_aabb(
        {{10.0f,5.0f,-1.0f}},{{11.0f,5.0f,1.0f}},
        view,1.0f,1.0f);
    assert(!first.empty && second.empty);
    // An untextured portal can be ABSENT from the render mesh. Native face
    // indices and world vertices still allow a safe screen-space window.
    auto untextured_geometry=geometry;
    untextured_geometry.receivers.erase(
        untextured_geometry.receivers.begin()+1);
    world_cache_mesh untextured_mesh{};
    assert(build_world_cache_mesh(untextured_geometry,untextured_mesh));
    std::vector<uint32_t> untextured_lookup;
    assert(build_world_cache_surface_lookup(
        untextured_mesh,3U,untextured_lookup));
    assert(untextured_lookup[1]==std::numeric_limits<uint32_t>::max());
    constexpr uintptr_t indices=0x60000U;
    put(surfaces+0x5cU+0x28U,4U);
    put(surfaces+0x5cU+0x2cU,uint32_t(indices));
    auto put_float=[&](uintptr_t at,float value) {
        uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));
        put(at,bits);
    };
    std::array<std::array<float,3>,4> portal_vertices{{
        {{-1.0f,10.0f,-1.0f}},{{1.0f,10.0f,-1.0f}},
        {{1.0f,10.0f,1.0f}},{{-1.0f,10.0f,1.0f}}
    }};
    for(uint32_t i=0;i<4;++i) {
        put(indices+uintptr_t(i)*4U,i);
        for(unsigned axis=0;axis<3;++axis)
            put_float(vertices+uintptr_t(i)*12U+uintptr_t(axis)*4U,
                      portal_vertices[i][axis]);
    }
    graph.clear();
    assert(graph.ensure(read,world,4U,untextured_geometry,
                        untextured_mesh,untextured_lookup));
    result=graph.traverse({0U},untextured_mesh,untextured_lookup,
                          view,1.0f,1.0f,spans);
    assert(result.valid && result.visible_sectors==3U);
    // Invalid next-sector pointers fail OPEN to the whole-world path.
    put(link1+4U,0x12345U);
    graph.clear();
    assert(!graph.ensure(read,world,3U,geometry,mesh,lookup));
    result=graph.traverse({0U},mesh,lookup,view,1.0f,1.0f,spans);
    assert(!result.valid && spans.empty());
    return 0;
}
