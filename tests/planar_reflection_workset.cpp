#include "renderer/planar_reflection_workset.hpp"
#include <cassert>
#include <memory>
#include <limits>

int main()
{
    using namespace jkgm;
    planar_reflection_thing_scan scan{};
    planar_reflection_model_thing first{},second{},third{};
    first.thing=0x10000U;first.signature=17;
    second.thing=0x20000U;second.signature=25;
    third.thing=0x30000U;third.signature=40;
    scan.models={first,second,third};

    auto geometry=std::make_shared<gpu_3do_geometry>();
    geometry->indices={0,1,2};
    gpu_3do_group group{};
    group.count=3;
    geometry->groups.push_back(group);
    auto make_node=[&](uint64_t owner) {
        gpu_3do_instance node{};
        node.shadow_owner=owner;
        node.geometry=geometry;
        node.transform={{1,0,0,0,1,0,0,0,1,2,3,4}};
        return node;
    };
    auto node0=make_node(planar_reflection_thing_owner_key(first));
    auto node1=make_node(planar_reflection_thing_owner_key(first));
    auto node2=make_node(planar_reflection_thing_owner_key(second));
    node2.shadow_only=true; // Depth caster, not a usable visible model.
    auto node3=make_node(0); // Missing ownership must not get guessed.
    auto node4=make_node(planar_reflection_thing_owner_key(third));
    node4.camera_hud=true;
    auto node5=make_node(planar_reflection_thing_owner_key(second));
    node5.is_projected_decal=true;
    auto node6=make_node(planar_reflection_thing_owner_key(second));
    node6.transform[0]=std::numeric_limits<float>::infinity();
    auto node7=make_node((uint64_t(0x44000U)<<32)|3U);
    std::vector<gpu_3do_instance> nodes={
        node0,node1,node2,node3,node4,node5,node6,node7};
    auto work=build_planar_reflection_workset(scan,nodes);
    assert(work.matched_things==1U);
    assert(work.cached_instance_indices.size()==2U);
    assert(work.cached_instance_indices[0]==0U &&
           work.cached_instance_indices[1]==1U);
    assert(work.uncovered_things.size()==2U);
    assert(work.uncovered_things[0].thing==second.thing);
    assert(work.uncovered_things[1].thing==third.thing);
    assert(work.unowned_gpu_nodes==1U && work.unmatched_gpu_nodes==1U);

    engine_camera source{};
    source.valid=true;
    source.position={{0,0,3}};
    source.right={{1,0,0}};
    source.forward={{0,1,0}};
    source.up={{0,0,1}};
    planar_reflection_plane plane{};
    auto mirror=make_planar_reflection_camera(source,plane);
    assert(mirror.valid && mirror.position[2]==-3);
    std::vector<planar_reflection_cached_view> views;
    assert(build_planar_reflection_cached_views(work,nodes,mirror,views));
    assert(views.size()==2U && views[0].instance_index==0U);
    auto expected=gpu_3do_native_modelview(
        planar_camera_native_view(mirror),node0.transform);
    assert(views[0].modelview==expected);
    assert(nodes[0].transform==node0.transform);

    // A stale index or invalid camera must fail closed with no partial draw.
    auto invalid=work;
    invalid.cached_instance_indices.push_back(nodes.size());
    assert(!build_planar_reflection_cached_views(
        invalid,nodes,mirror,views));
    assert(views.empty());
    mirror.valid=false;
    assert(!build_planar_reflection_cached_views(work,nodes,mirror,views));
    assert(views.empty());
    scan.error=planar_reflection_thing_error::invalid_link;
    assert(build_planar_reflection_workset(scan,nodes)
               .cached_instance_indices.empty());
    return 0;
}
