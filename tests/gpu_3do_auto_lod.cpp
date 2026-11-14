#include "renderer/gpu_3do_auto_lod.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
using namespace jkgm;
static gpu_3do_geometry grid(unsigned n) {
    gpu_3do_geometry mesh{};
    gpu_3do_group group{};
    group.material=0x1234U;
    group.cel=2;
    group.plane_start=3;
    group.plane_count=7;
    for(unsigned y=0;y<n;++y)
        for(unsigned x=0;x<n;++x) {
            float p[4][2]={{float(x),float(y)},{float(x+1),float(y)},
                            {float(x+1),float(y+1)},{float(x),float(y+1)}};
            for(auto const &t : {std::array<int,3>{{0,1,2}},
                                  std::array<int,3>{{0,2,3}}}) {
                for(int k:t) {
                    gpu_3do_vertex v{};
                    v.x=p[k][0];v.y=p[k][1];v.z=0.0f;
                    v.u=p[k][0];v.v=p[k][1];
                    v.nz=1.0f;v.lnz=1.0f;
                    v.fax=float(x);v.fay=float(y);v.faz=0.0f;
                    v.cr=v.cg=v.cb=1.0f;
                    mesh.indices.push_back(
                        static_cast<uint32_t>(mesh.vertices.size()));
                    mesh.vertices.push_back(v);
                }
            }
        }
    group.count=static_cast<uint32_t>(mesh.indices.size());
    mesh.groups.push_back(group);
    return mesh;
}
// Simulate authored JK 3DOs with many 1-triangle draw ranges. Previously
// each range failed the 64-triangle minimum even if the WHOLE mesh was dense.
static gpu_3do_geometry fragment_groups(gpu_3do_geometry mesh,
                                         bool half_different_mat=false,
                                         bool half_alpha=false)
{
    auto const basis=mesh.groups.front();
    mesh.groups.clear();
    for(uint32_t offset=0;offset<mesh.indices.size();offset+=3U) {
        gpu_3do_group g=basis;
        g.offset=offset;
        g.count=3U;
        auto const &v=mesh.vertices[mesh.indices[offset]];
        bool const right=v.x>=9.0f;
        if(half_different_mat && right)g.material=0x4321U;
        if(half_alpha && right)g.face_type|=2U;
        g.bounds_min={{v.x,v.y,v.z}};
        g.bounds_max=g.bounds_min;
        for(unsigned k=1;k<3U;++k) {
            auto const &p=mesh.vertices[mesh.indices[offset+k]];
            g.bounds_min[0]=std::min(g.bounds_min[0],p.x);
            g.bounds_min[1]=std::min(g.bounds_min[1],p.y);
            g.bounds_max[0]=std::max(g.bounds_max[0],p.x);
            g.bounds_max[1]=std::max(g.bounds_max[1],p.y);
        }
        mesh.groups.push_back(g);
    }
    return mesh;
}
int main() {
    // Raw world Y must never be treated as camera-relative depth.
    std::array<float,3> const world{{0.0f,10.0f,0.0f}};
    std::array<float,3> const camera0{{0.0f,0.0f,0.0f}};
    std::array<float,3> const along_y{{0.0f,1.0f,0.0f}};
    assert(std::fabs(gpu_3do_auto_lod_projected_radius_px(
        world,camera0,along_y,1.0,400.0,1.0)-400.0f/9.0f)<0.001f);
    std::array<float,3> const camera_moved{{0.0f,5.0f,0.0f}};
    assert(std::fabs(gpu_3do_auto_lod_projected_radius_px(
        world,camera_moved,along_y,1.0,400.0,1.0)-100.0f)<0.001f);
    std::array<float,3> const along_x{{1.0f,0.0f,0.0f}};
    std::array<float,3> const sideways_world{{10.0f,0.0f,0.0f}};
    assert(std::fabs(gpu_3do_auto_lod_projected_radius_px(
        sideways_world,camera0,along_x,1.0,400.0,1.0)-
        400.0f/9.0f)<0.001f);
    assert(gpu_3do_auto_lod_projected_radius_px(
        world,camera0,along_x,1.0,400.0,1.0)<0.0f);
    assert(select_gpu_3do_auto_lod(150.0f)==0U);
    assert(select_gpu_3do_auto_lod(60.0f)==1U);
    assert(select_gpu_3do_auto_lod(4.0f)==2U);
    assert(select_gpu_3do_auto_lod(99.0f,1U)==1U);
    assert(select_gpu_3do_auto_lod(35.0f,2U)==2U);
    assert(select_gpu_3do_auto_lod(120.0f,2U)==0U);
    // Altering the screen-size thresholds moves both transition bands
    // without changing native model GeoSets or the projected-radius units.
    assert(select_gpu_3do_auto_lod(60.0f,0U,50.0f,12.0f)==0U);
    assert(select_gpu_3do_auto_lod(30.0f,0U,50.0f,12.0f)==1U);
    assert(select_gpu_3do_auto_lod(10.0f,0U,50.0f,12.0f)==2U);
    assert(select_gpu_3do_auto_lod(65.0f,1U,50.0f,12.0f)==0U);
    assert(select_gpu_3do_auto_lod(14.0f,2U,50.0f,12.0f)==2U);
    assert(select_gpu_3do_auto_lod(20.0f,2U,50.0f,12.0f)==1U);
    // Forced debug makes reduced geometry visible directly at point-blank
    // range; a separate mode explicitly selects the untouched GeoSet 0.
    assert(select_gpu_3do_auto_lod(10000.0f,0U,90.0f,28.0f,1)==0U);
    assert(select_gpu_3do_auto_lod(10000.0f,0U,90.0f,28.0f,2)==1U);
    assert(select_gpu_3do_auto_lod(10000.0f,0U,90.0f,28.0f,3)==2U);
    assert(select_gpu_3do_auto_lod(0.0f,0U,90.0f,28.0f,3)==2U);
    assert(select_gpu_3do_auto_lod(NAN,0U,90.0f,28.0f,2)==1U);
    auto source=grid(18U);
    assert(!generate_gpu_3do_auto_lod(source,0U));
    assert(!generate_gpu_3do_auto_lod(source,3U));
    auto one=generate_gpu_3do_auto_lod(source,1U);
    auto two=generate_gpu_3do_auto_lod(source,2U);
    assert(one && two);
    assert(one->indices.size()<source.indices.size());
    assert(two->indices.size()<=one->indices.size());
    assert(source.indices.size()==18U*18U*6U); // original immutable
    for(auto const &result:{one,two}) {
        assert(result->groups.size()==1U);
        assert(result->groups[0].material==0x1234U);
        assert(result->groups[0].cel==2);
        assert(result->groups[0].plane_start==3U);
        // A simplified group must disable stale authored face-plane CPU
        // backface culling; its original planes no longer match topology.
        assert(result->groups[0].plane_count==0U);
        assert(result->groups[0].count==result->indices.size());
        assert(result->indices.size()%3U==0U);
        for(uint32_t i:result->indices) {
            assert(i<result->vertices.size());
            auto const &v=result->vertices[i];
            assert(std::isfinite(v.x)&&std::isfinite(v.y));
            assert(v.x>=0.0f&&v.x<=18.0f);
            assert(v.y>=0.0f&&v.y<=18.0f);
        }
    }
    // One 3DO may have many independent rdMesh nodes, each with dozens
    // of small material groups. ALL reducible meshes must be considered.
    auto many=fragment_groups(grid(18U));
    assert(many.groups.size()==18U*18U*2U);
    auto fragmented1=generate_gpu_3do_auto_lod(many,1U);
    auto fragmented2=generate_gpu_3do_auto_lod(many,2U);
    assert(fragmented1 && fragmented2);
    assert(fragmented1->indices.size()<many.indices.size());
    assert(fragmented2->indices.size()<=fragmented1->indices.size());
    assert(fragmented1->groups.size()<=many.groups.size());
    for(auto const &g:fragmented1->groups) {
        assert(g.material==0x1234U);
        assert(g.count>0U);
        assert(g.count%3U==0U);
        assert(g.offset+g.count<=fragmented1->indices.size());
    }
    // Multiple medium-sized independent 3DO meshes must also reduce,
    // even though neither contains a single 64-triangle group.
    auto part1=fragment_groups(grid(8U));
    auto part2=fragment_groups(grid(8U));
    assert(generate_gpu_3do_auto_lod(part1,1U));
    assert(generate_gpu_3do_auto_lod(part2,2U));
    // No material boundary may move or tear; a second MAT still renders.
    auto mixed=fragment_groups(grid(18U),true,false);
    auto mixed_lod=generate_gpu_3do_auto_lod(mixed,2U);
    assert(mixed_lod);
    bool saw_a=false,saw_b=false;
    for(auto const &g:mixed_lod->groups) {
        if(g.material==0x1234U)saw_a=true;
        if(g.material==0x4321U)saw_b=true;
    }
    assert(saw_a && saw_b);
    // Entirely translucent meshes remain untouched, and opaque areas
    // never collapse across an adjacent alpha material boundary.
    auto mixed_alpha=fragment_groups(grid(18U),false,true);
    auto mixed_alpha_lod=generate_gpu_3do_auto_lod(mixed_alpha,1U);
    assert(mixed_alpha_lod);
    size_t original_alpha=0U,generated_alpha=0U;
    for(auto const &g:mixed_alpha.groups)
        if(g.face_type&2U)original_alpha+=g.count;
    for(auto const &g:mixed_alpha_lod->groups)
        if(g.face_type&2U)generated_alpha+=g.count;
    assert(generated_alpha==original_alpha);
    // Low-poly models stay untouched, avoiding pointless memory and
    // silhouette quality loss.
    auto small=grid(3U);
    assert(!generate_gpu_3do_auto_lod(small,1U));
    // Opaque-only simplification: alpha faces cannot lose painter order.
    auto alpha=grid(18U);
    alpha.groups[0].face_type=2U;
    assert(!generate_gpu_3do_auto_lod(alpha,2U));
}
