#pragma once
// ABI-independent preparation of the native sithIntersect type-2 hit fields.
// A caller must validate all output pointers before writing them. No game
// state or output memory is modified by this helper on failure.
#include "mots_native_collision_query.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
    constexpr uintptr_t jkgm_sphere_thing_intersect_address=0x00508360U;
    constexpr std::array<uint8_t,12> jkgm_sphere_thing_intersect_prologue{{
        0x83,0xec,0x4c,0x8b,0x44,0x24,0x68,0x53,0x55,0x56,0x57,0x8b
    }};
    // A successful native Thing-sphere/mesh test returns the Thing hit bit.
    constexpr int jkgm_collision_hit_thing=1;
    struct mots_engine_type2_hit {
        float distance=0.0f;
        thing_node_vector normal{};
        // MotS tree hits do not write ppHitMesh or ppHitFace.
        int flags=jkgm_collision_hit_thing;
    };

    inline bool mots_prepare_engine_type2_hit(
        mots_native_model_snapshot const &owner,
        mots_collision_query_hit const &collision,
        mots_engine_type2_hit &output)
    {
        if(collision.node>=owner.nodes.size() ||
           !std::isfinite(collision.distance) || collision.distance<0.0f)
            return false;
        auto const &node=owner.nodes[collision.node];
        if(node.mesh_index<0)return false;
        float length2=0.0f;
        for(float component:collision.normal) {
            if(!std::isfinite(component))return false;
            length2+=component*component;
        }
        if(!std::isfinite(length2) || std::fabs(length2-1.0f)>0.01f)
            return false;
        mots_engine_type2_hit hit{};
        hit.distance=collision.distance;
        hit.normal=collision.normal;
        output=hit;
        return true;
    }
}
