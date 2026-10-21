#include "renderer/mots_native_collision_bridge.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm;
int main() {
    assert(jkgm_sphere_thing_intersect_address==0x508360U);
    assert(jkgm_sphere_thing_intersect_prologue[0]==0x83);
    mots_native_model_snapshot m{};
    m.nodes.resize(2);
    m.nodes[1].mesh_index=3;
    m.nodes[1].native_mesh_address=0x10120000U;
    mots_collision_query_hit hit{};
    hit.node=1;
    hit.distance=2.25f;
    hit.normal={{0.0f,-1.0f,0.0f}};
    mots_engine_type2_hit out{};
    assert(mots_prepare_engine_type2_hit(m,hit,out));
    assert(out.flags==jkgm_collision_hit_thing && out.distance==2.25f);
    assert(out.normal[1]==-1.0f);
    auto saved=out;
    hit.node=2;
    assert(!mots_prepare_engine_type2_hit(m,hit,out));
    assert(out.normal[1]==saved.normal[1] && out.distance==saved.distance);
    hit.node=1;hit.distance=std::numeric_limits<float>::infinity();
    assert(!mots_prepare_engine_type2_hit(m,hit,out));
    hit.distance=2.25f;hit.normal={{0,0,0}};
    assert(!mots_prepare_engine_type2_hit(m,hit,out));
    hit.normal={{1,0,0}};
    // Native MotS does not fill mesh/face outputs for sphere-tree hits.
    m.nodes[1].native_mesh_address=0;
    assert(mots_prepare_engine_type2_hit(m,hit,out));
}
