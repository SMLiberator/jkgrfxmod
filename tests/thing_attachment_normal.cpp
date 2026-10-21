#include "renderer/thing_attachment_normal.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace jkgm;
int main() {
    constexpr uintptr_t base=0x10000000U,thing=base+0x100,
        parent=base+0x600,face=base+0x1000;
    std::vector<uint8_t> bytes(0x2000);
    auto put=[&](uintptr_t at,void const *p,size_t n) {
        assert(at>=base && at-base+n<=bytes.size());
        std::memcpy(bytes.data()+at-base,p,n);
    };
    auto u32=[&](uintptr_t at,uint32_t n){put(at,&n,4);};
    auto vec=[&](uintptr_t at,thing_node_vector const &v){
        put(at,v.data(),12);
    };
    thing_node_reader read=[&](uintptr_t at,void *p,size_t n) {
        if(at<base || at-base+n>bytes.size())return false;
        std::memcpy(p,bytes.data()+at-base,n);
        return true;
    };

    u32(thing+0x0c,10);
    u32(thing+0x34,1);
    u32(thing+0x44,uint32_t(face));
    vec(face+0x34,{{0,0,2}});
    thing_node_vector output{{-1,-1,-1}};
    assert(read_thing_attachment_normal(read,thing,output));
    assert(output[0]==0 && output[1]==0 && output[2]==1);

    // A Thing-face attachment's normal follows the attached Thing's
    // current R/L/U basis. This transform must not be applied to walls.
    u32(thing+0x34,2);
    u32(thing+0x58,uint32_t(parent));
    u32(parent+0x0c,10);
    float rotated[9]{0,1,0, 0,0,1, 1,0,0};
    put(parent+0x88,rotated,sizeof(rotated));
    assert(read_thing_attachment_normal(read,thing,output));
    assert(output[0]==1 && output[1]==0 && output[2]==0);

    // No face exists for an ordinary Thing attachment.
    u32(thing+0x34,4);
    auto saved=output;
    assert(!read_thing_attachment_normal(read,thing,output));
    assert(output==saved);
    // Invalid parent Thing pointers cannot leak an arbitrary face normal.
    u32(thing+0x34,2);
    u32(thing+0x58,0U);
    assert(!read_thing_attachment_normal(read,thing,output));
    u32(thing+0x58,uint32_t(parent));
    vec(face+0x34,{{0,0,std::numeric_limits<float>::quiet_NaN()}});
    assert(!read_thing_attachment_normal(read,thing,output));
    assert(output==saved);
}
