#pragma once
// Pure transform helpers for future GetThingNodePos / GetThingNodeRVec /
// GetThingNodeLVec / GetThingNodeUVec COG verbs and node attachments.
//
// Input must be the CURRENT WORLD-SPACE rdMatrix34 for that joint. JK's
// rdModel3_GetMeshMatrix() builds one and refreshes puppet matrices if stale.
// Do NOT pass the model's static hierarchy posRotMatrix here.
#include "gpu_3do_modelview.hpp"
#include <array>

namespace jkgm {
    using thing_node_vector=std::array<float,3>;
    struct thing_node_pose {
        thing_node_vector right{};
        thing_node_vector look{};
        thing_node_vector up{};
        thing_node_vector origin{};
    };

    inline bool get_thing_node_pose(gpu_3do_matrix34 const &world_matrix,
                                    thing_node_pose &out)
    {
        if(!gpu_3do_matrix_finite(world_matrix)) return false;
        // Preserve native mirrored/scaled R/L/U axes; do not force a
        // right-handed rotation or silently normalize negative scales.
        out.right={{world_matrix[0],world_matrix[1],world_matrix[2]}};
        out.look ={{world_matrix[3],world_matrix[4],world_matrix[5]}};
        out.up   ={{world_matrix[6],world_matrix[7],world_matrix[8]}};
        out.origin={{world_matrix[9],world_matrix[10],world_matrix[11]}};
        return true;
    }

    inline bool get_thing_node_point(gpu_3do_matrix34 const &world_matrix,
                                     thing_node_vector const &local,
                                     thing_node_vector &world)
    {
        if(!gpu_3do_matrix_finite(world_matrix))return false;
        for(float value:local)if(!std::isfinite(value))return false;
        world=gpu_3do_native_transform_point(world_matrix,local);
        for(float value:world)if(!std::isfinite(value))return false;
        return true;
    }

    // Compose with a local offset matrix. For node-bound sprites or lights,
    // this preserves the whole node orientation, not just its translation.
    inline bool get_thing_node_attachment(
        gpu_3do_matrix34 const &world_node,
        gpu_3do_matrix34 const &local_offset,
        gpu_3do_matrix34 &world_attachment)
    {
        if(!gpu_3do_matrix_finite(world_node) ||
           !gpu_3do_matrix_finite(local_offset))return false;
        auto const matrix=gpu_3do_native_modelview(world_node,local_offset);
        if(!gpu_3do_matrix_finite(matrix))return false;
        world_attachment=matrix;
        return true;
    }
}
