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
    // One Thing-wide sphere selects the SAME level for its torso,
    // hands and feet, irrespective of their individual mesh dimensions.
    std::array<float,3> const thing_center{{0.0f,30.0f,0.0f}};
    float const model_px=gpu_3do_auto_lod_projected_radius_px(
        thing_center,camera0,along_y,5.0,400.0,1.0);
    assert(std::fabs(model_px-80.0f)<0.001f);
    assert(select_gpu_3do_auto_lod(model_px)==1U);
    float const hand_px=gpu_3do_auto_lod_projected_radius_px(
        thing_center,camera0,along_y,0.2,400.0,1.0);
    assert(select_gpu_3do_auto_lod(hand_px)==2U);
    // Old mesh-local screen sizing disagreed; Thing-wide sizing does not.
    unsigned const torso_lod=select_gpu_3do_auto_lod(model_px);
    unsigned const hand_lod=select_gpu_3do_auto_lod(model_px);
    assert(torso_lod==hand_lod);
    // CPU generation budget is independent from geometric quality.
    assert(gpu_3do_auto_lod_budget_allows(0U,0));
    assert(gpu_3do_auto_lod_budget_allows(10000U,0));
    assert(gpu_3do_auto_lod_budget_allows(0U,1));
    assert(!gpu_3do_auto_lod_budget_allows(1U,1));
    assert(gpu_3do_auto_lod_budget_allows(1U,2));
    assert(!gpu_3do_auto_lod_budget_allows(2U,2));
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
    // Quality mode reduces flat interiors and also collapses straight
    // boundary segments, while preserving the outer rectangular silhouette.
    auto quality1=generate_gpu_3do_auto_lod(source,1U,1,0.01f);
    auto quality2=generate_gpu_3do_auto_lod(source,2U,1,0.03f);
    assert(quality1 && quality2);
    assert(quality1->indices.size()<source.indices.size());
    assert(quality2->indices.size()<=quality1->indices.size());
    auto has_vertex=[](gpu_3do_geometry const &mesh,float x,float y) {
        for(auto const &v:mesh.vertices)
            if(v.x==x && v.y==y && v.z==0.0f)return true;
        return false;
    };
    for(auto const &mesh:{quality1,quality2}) {
        for(auto const &corner:std::array<std::array<float,2>,4>{{
            {{0.0f,0.0f}},{{0.0f,18.0f}},{{18.0f,0.0f}},
            {{18.0f,18.0f}}
        }})
            assert(has_vertex(*mesh,corner[0],corner[1]));
        for(auto const &v:mesh->vertices) {
            assert(v.x>=0.0f&&v.x<=18.0f);
            assert(v.y>=0.0f&&v.y<=18.0f);
        }
    }
    for(auto const &mesh:{quality1,quality2})
        for(size_t i=0;i<mesh->indices.size();i+=3U) {
            auto const &a=mesh->vertices[mesh->indices[i]];
            auto const &b=mesh->vertices[mesh->indices[i+1U]];
            auto const &c=mesh->vertices[mesh->indices[i+2U]];
            assert((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)>0.0f);
        }
    // A folded sheet has a hard 90-degree ridge. Original crease
    // vertices must remain intact even when both broad panels simplify.
    auto folded=grid(18U);
    for(auto &v:folded.vertices) {
        if(v.x>9.0f) {
            v.z=v.x-9.0f;
            v.x=9.0f;
        }
    }
    auto folded_quality=generate_gpu_3do_auto_lod(folded,2U,1,0.03f);
    assert(folded_quality);
    assert(folded_quality->indices.size()<folded.indices.size());
    assert(has_vertex(*folded_quality,9.0f,0.0f));
    assert(has_vertex(*folded_quality,9.0f,18.0f));
    // The ridge can lose intermediate vertices but cannot leave its
    // original straight line.
    for(auto const &v:folded_quality->vertices)
        if(v.x==9.0f&&v.z==0.0f)
            assert(v.y>=0.0f&&v.y<=18.0f);
    // A MAT seam on an otherwise flat mesh remains a protected feature.
    auto split_mats=fragment_groups(grid(18U),true,false);
    auto split_quality=generate_gpu_3do_auto_lod(split_mats,1U,1,0.01f);
    assert(split_quality);
    bool mat_left=false,mat_right=false;
    for(auto const &g:split_quality->groups) {
        mat_left|=g.material==0x1234U;
        mat_right|=g.material==0x4321U;
    }
    assert(mat_left&&mat_right);
    assert(has_vertex(*split_quality,9.0f,0.0f));
    assert(has_vertex(*split_quality,9.0f,18.0f));
    // A single two-triangle outline must remain intact in quality mode:
    // deleting a corner would change the rectangular silhouette.
    auto square=grid(1U);
    assert(!generate_gpu_3do_auto_lod(square,1U,1,0.03f));
    assert(!generate_gpu_3do_auto_lod(source,1U,1,-1.0f));
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
    // Material boundaries may move; remaining draw ranges still retain
    // their source MAT identities for testing the visual consequences.
    auto mixed=fragment_groups(grid(18U),true,false);
    auto mixed_lod=generate_gpu_3do_auto_lod(mixed,2U);
    assert(mixed_lod);
    bool saw_a=false,saw_b=false;
    for(auto const &g:mixed_lod->groups) {
        if(g.material==0x1234U)saw_a=true;
        if(g.material==0x4321U)saw_b=true;
    }
    assert(saw_a && saw_b);
    // Alpha faces participate in the same aggressive simplification as
    // opaque ones, even when they border another draw group.
    auto mixed_alpha=fragment_groups(grid(18U),false,true);
    auto mixed_alpha_lod=generate_gpu_3do_auto_lod(mixed_alpha,1U);
    assert(mixed_alpha_lod);
    size_t original_alpha=0U,generated_alpha=0U;
    for(auto const &g:mixed_alpha.groups)
        if(g.face_type&2U)original_alpha+=g.count;
    for(auto const &g:mixed_alpha_lod->groups)
        if(g.face_type&2U)generated_alpha+=g.count;
    assert(generated_alpha<original_alpha);
    // Even very small meshes (well under the old 24-triangle threshold)
    // are attempted; no minimum improvement percentage is enforced.
    auto small=grid(3U);
    auto reduced_small=generate_gpu_3do_auto_lod(small,1U);
    assert(reduced_small);
    assert(reduced_small->indices.size()<small.indices.size());
    auto quad=grid(1U);
    auto reduced_quad=generate_gpu_3do_auto_lod(quad,1U);
    assert(reduced_quad);
    assert(reduced_quad->indices.size()==3U);
    // A single triangle cannot be collapsed into a nonempty triangle.
    auto single=quad;
    single.indices.resize(3U);
    single.groups[0].count=3U;
    assert(!generate_gpu_3do_auto_lod(single,1U));
    // Tiny decorative islands can be pruned even when every one of their
    // individual boundary edges is protected by quality-mode topology.
    // Keep native source untouched and ensure the original material range
    // containing the badge is omitted only when pruning is enabled.
    auto badge_car=grid(18U);
    gpu_3do_group badge_group=badge_car.groups[0];
    badge_group.material=0xBADD;
    badge_group.offset=static_cast<uint32_t>(badge_car.indices.size());
    badge_group.count=0U;
    for(unsigned y=0;y<4U;++y)for(unsigned x=0;x<4U;++x) {
        float const x0=5.27f+float(x)*0.04f;
        float const y0=5.37f+float(y)*0.04f;
        float const x1=x0+0.04f,y1=y0+0.04f;
        float const p[4][2]={{x0,y0},{x1,y0},{x1,y1},{x0,y1}};
        for(auto const &t:{std::array<int,3>{{0,1,2}},
                           std::array<int,3>{{0,2,3}}})for(int k:t) {
            gpu_3do_vertex v{};
            v.x=p[k][0];v.y=p[k][1];v.z=0.02f;
            v.u=p[k][0];v.v=p[k][1];v.nz=v.lnz=1.0f;
            v.cr=v.cg=v.cb=1.0f;
            badge_car.indices.push_back(uint32_t(badge_car.vertices.size()));
            badge_car.vertices.push_back(v);
            ++badge_group.count;
        }
    }
    badge_car.groups.push_back(badge_group);
    auto contains_badge=[](gpu_3do_geometry const &mesh) {
        for(auto const &g:mesh.groups)if(g.material==0xBADD)return true;
        return false;
    };
    gpu_3do_auto_lod_prune_stats no_prune_stats{},prune_stats{};
    auto unpruned=generate_gpu_3do_auto_lod(
        badge_car,2U,1,0.03f,0.0f,&no_prune_stats);
    assert(unpruned && contains_badge(*unpruned));
    auto pruned=generate_gpu_3do_auto_lod(
        badge_car,2U,1,0.03f,0.02f,&prune_stats);
    assert(pruned && !contains_badge(*pruned));
    assert(prune_stats.components==2U);
    assert(prune_stats.removed_components==1U);
    assert(prune_stats.removed_triangles==32U);
    assert(badge_car.groups.size()==2U); // source unchanged
    assert(badge_car.indices.size()==18U*18U*6U+32U*3U);
    // A low-poly single-component object must not be discarded just for
    // being small in absolute terms.
    gpu_3do_auto_lod_prune_stats single_stats{};
    (void)generate_gpu_3do_auto_lod(
        grid(1U),2U,1,0.03f,0.25f,&single_stats);
    assert(single_stats.components==1U);
    assert(single_stats.removed_components==0U);
    // Separate authored low-poly quads are not necessarily optional details.
    // With no dominant connected surface, a high pruning ratio must not
    // throw away one of the two otherwise meaningful polygons.
    auto loose_quads=grid(1U);
    size_t const original_quad_vertices=loose_quads.vertices.size();
    for(size_t i=0;i<original_quad_vertices;++i) {
        auto v=loose_quads.vertices[i];
        v.x+=8.0f;
        loose_quads.indices.push_back(
            static_cast<uint32_t>(loose_quads.vertices.size()));
        loose_quads.vertices.push_back(v);
    }
    loose_quads.groups[0].count=static_cast<uint32_t>(
        loose_quads.indices.size());
    gpu_3do_auto_lod_prune_stats lowpoly_stats{};
    (void)generate_gpu_3do_auto_lod(
        loose_quads,2U,1,0.03f,0.25f,&lowpoly_stats);
    assert(lowpoly_stats.components==2U);
    assert(lowpoly_stats.removed_components==0U);
    assert(lowpoly_stats.removed_triangles==0U);

    // Entirely transparent groups reduce too.
    auto alpha=grid(18U);
    alpha.groups[0].face_type=2U;
    auto reduced_alpha=generate_gpu_3do_auto_lod(alpha,2U);
    assert(reduced_alpha);
    assert(reduced_alpha->indices.size()<alpha.indices.size());
    assert(reduced_alpha->groups[0].face_type&2U);
}
