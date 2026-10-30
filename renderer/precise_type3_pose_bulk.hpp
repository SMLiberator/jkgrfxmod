#pragma once
// Take one refreshed JKGM rdThing pose snapshot per zero-radius query.
// rdModel3_GetMeshMatrix(0) refreshes every paJointMatrices entry when
// rdFrameNum is stale. Read that contiguous array once instead of calling
// the accessor for every active mesh. No pose survives the current query:
// movement, COG rotation and animation remain live on the next cast.
#include "mots_native_mesh_snapshot.hpp"
#include <cstring>
#include <functional>
#include <vector>

namespace jkgm {
    inline bool precise_type3_read_pose_batch(
        thing_node_reader const& read, uintptr_t rd_thing,
        uint32_t nodes,
        std::function<bool(gpu_3do_matrix34&)> const& refresh,
        std::vector<gpu_3do_matrix34> &out)
    {
        // JKGM rdThing::paJointMatrices is at +0x20 (x86).
        constexpr uintptr_t matrices_field=0x20U;
        if(!read || !refresh || nodes==0 ||
           nodes>mots_native_layout::max_nodes ||
           !thing_node_valid_address(rd_thing+matrices_field,4))
            return false;
        gpu_3do_matrix34 first{};
        if(!refresh(first) || !gpu_3do_matrix_finite(first))return false;

        uint32_t data=0;
        if(!read(rd_thing+matrices_field,&data,4) ||
           !thing_node_valid_address(data,4))return false;
        size_t const nbytes=size_t(nodes)*sizeof(gpu_3do_matrix34);
        if(uint64_t(data)+nbytes>0x80000000ULL)return false;

        out.resize(nodes);
        auto* dest=reinterpret_cast<uint8_t*>(out.data());
        for(size_t offset=0;offset<nbytes;) {
            size_t const count=std::min(
                nbytes-offset,mots_native_layout::max_read_chunk);
            if(!thing_node_valid_address(uintptr_t(data)+offset,count) ||
               !read(uintptr_t(data)+offset,dest+offset,count))
                return false;
            offset+=count;
        }
        // Refuse fast path if the native accessor and underlying pose array
        // disagree, for example on an unknown game executable.
        if(std::memcmp(first.data(),out[0].data(),sizeof(first))!=0)
            return false;
        // Nonfinite inactive node matrices may legitimately be ignored by
        // the existing narrow phase. It will validate active nodes itself.
        return true;
    }
}
