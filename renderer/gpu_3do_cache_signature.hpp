#pragma once
// Exact byte signature for GPU 3DO source-state validation. Hash only fields
// whose values contribute to cached geometry. Runtime verifies native memory
// ranges before calling add_bytes; this helper itself does not touch JK memory.
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace jkgm {
    struct gpu_3do_byte_signature {
        uint64_t value=14695981039346656037ULL;
        void add_bytes(void const *data,size_t count) {
            auto const *bytes=static_cast<uint8_t const *>(data);
            for(size_t i=0;i<count;i++) {
                value^=bytes[i];
                value*=1099511628211ULL;
            }
        }
        template<typename T> void add(T const &v) {
            static_assert(std::is_trivially_copyable<T>::value,
                          "GPU 3DO signature must hash deterministic scalar state");
            add_bytes(&v,sizeof(v));
        }
    };
}
