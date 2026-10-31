#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jkgm {
    // JK GOB handles do not share stdio seek/size conventions. Starting from
    // a newly opened resource, consume bytes sequentially until read returns
    // zero. Never seek to end or ask the resource layer for fileSize.
    // A short read is not necessarily EOF: the next read may return more.
    template <class Read>
    bool read_sound_source_sequential(Read&& read, size_t max_bytes,
                                      std::vector<uint8_t>& output)
    {
        output.clear();
        std::array<uint8_t,16U*1024U> block{};
        for(;;) {
            size_t const n=read(block.data(),block.size());
            if(n>block.size() || n>max_bytes ||
               output.size()>max_bytes-n) {
                output.clear();
                return false;
            }
            if(n==0)return !output.empty();
            output.insert(output.end(),block.data(),block.data()+n);
        }
    }
}
