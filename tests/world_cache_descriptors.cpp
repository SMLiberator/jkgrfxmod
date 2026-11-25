#include "renderer/world_cache_descriptors.hpp"
#include "renderer/world_cache_live_upload.hpp"
#include <cassert>
#include <cstdint>
#include <vector>
using namespace jkgm;
int main() {
    static_assert(sizeof(world_cache_descriptor_vertex)==24,"world format");
    static_assert(sizeof(world_cache_surface_descriptor)==48,"descriptor");
    world_cache_mesh mesh{};
    for(uint32_t f=0;f<4;++f) {
        uint32_t const base=uint32_t(mesh.vertices.size());
        uint32_t const first=uint32_t(mesh.indices.size());
        for(uint32_t v=0;v<4;++v) {
            world_cache_vertex vert{};
            vert.position={{float(f*4+v),float(v),0.0f}};
            vert.uv={{123456.75f+float(v),-32.25f}};
            vert.normal={{0.0f,-1.0f,0.0f}};
            mesh.vertices.push_back(vert);
        }
        for(auto k:{0U,1U,2U,0U,2U,3U})
            mesh.indices.push_back(base+k);
        world_cache_surface_range range{};
        range.surface_index=10U+f;
        range.first_index=first;
        range.count=6U;
        mesh.surfaces.push_back(range);
    }
    std::vector<world_cache_descriptor_vertex> packed;
    std::vector<world_cache_surface_descriptor> desc;
    assert(build_world_cache_descriptors(mesh,packed,desc));
    assert(packed.size()==16 && desc.size()==4);
    for(uint32_t i=0;i<packed.size();++i) {
        assert(packed[i].position==mesh.vertices[i].position);
        assert(packed[i].uv==mesh.vertices[i].uv);
        assert(packed[i].surface==i/4);
    }
    for(auto const &d:desc) {
        assert(d.normal_scroll_x[1]==-1.0f);
        assert(d.tint_scroll_y[0]==0.0f);
        assert(d.cmp_reserved[0]==1.0f);
    }
    std::vector<uint32_t> lookup(14U,0xffffffffU);
    for(uint32_t i=0;i<4;++i)lookup[10U+i]=i;
    std::vector<std::array<float,3>> tint(16,{{0.25f,0.5f,0.75f}});
    std::vector<std::array<float,3>> cmp(16,{{1.0f,0.0f,0.25f}});
    std::vector<std::array<float,2>> uv(16,{{-17.0f,99.25f}});
    world_cache_draw_candidate a{};
    a.surface_index=12U;a.first_index=12U;a.count=6U;
    assert(stage_world_cache_surface_descriptor(
        mesh,lookup,a,tint,cmp,uv,desc));
    assert(desc[2].normal_scroll_x[3]==-17.0f);
    assert(desc[2].tint_scroll_y[0]==0.25f);
    assert(desc[2].tint_scroll_y[3]==99.25f);
    assert(desc[2].cmp_reserved[1]==0.0f);
    assert(desc[1].normal_scroll_x[3]==0.0f);
    auto const stream=plan_world_cache_descriptor_stream(mesh,lookup,{a});
    assert(stream.valid && stream.touched_vertices==1);
    assert(stream.ranges.size()==1 && stream.ranges[0].first_vertex==2);
    auto uploaded=std::vector<world_cache_surface_descriptor>(4U);
    auto const dirty=plan_world_cache_live_upload(desc,uploaded,stream);
    assert(!dirty.empty());
    record_world_cache_live_upload(desc,uploaded,dirty);
    assert(uploaded[2].normal_scroll_x[3]==-17.0f);
    assert(uploaded[0].normal_scroll_x[3]==0.0f);
    assert(plan_world_cache_live_upload(desc,uploaded,stream).empty());
    a.surface_index=13U;a.first_index=18U;
    assert(stage_world_cache_surface_descriptor(
        mesh,lookup,a,tint,cmp,uv,desc));
    assert(desc[3].tint_scroll_y[3]==99.25f);
    a.count=3U;
    assert(!stage_world_cache_surface_descriptor(
        mesh,lookup,a,tint,cmp,uv,desc));
    auto bad=mesh;
    bad.vertices[7].normal={{1.0f,0.0f,0.0f}};
    assert(!build_world_cache_descriptors(bad,packed,desc));
    assert(packed.empty() && desc.empty());
}
