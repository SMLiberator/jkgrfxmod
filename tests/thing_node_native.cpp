#include "renderer/thing_node_native.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
using namespace jkgm;
static bool close(float a,float b) {return std::fabs(a-b)<0.0001f;}
int main() {
    constexpr uintptr_t base=0x10000000U;
    std::vector<uint8_t> memory(0x2000);
    uintptr_t const thing=base+0x100;
    uintptr_t const model=base+0x800;
    auto set32=[&](uintptr_t a,uint32_t v) {
        std::memcpy(memory.data()+a-base,&v,4);
    };
    set32(thing+0x0c,10);  // valid SithThing
    set32(thing+0xc4,1);   // RD_THING_MODEL3
    set32(thing+0xc8,uint32_t(model));
    set32(model+0x54,3);   // three hierarchy nodes
    float const orient[12]={0,1,0, -1,0,0, 0,0,1, 0,0,0};
    float const position[3]={20,30,40};
    std::memcpy(memory.data()+thing+0x88-base,orient,sizeof(orient));
    std::memcpy(memory.data()+thing+0xb8-base,position,sizeof(position));
    bool signature_ok=true;
    thing_node_reader reader=[&](uintptr_t a,void *dst,size_t len)->bool {
        if(a==jkgm_node_matrix_address && len==jkgm_node_matrix_prologue.size()) {
            auto sig=jkgm_node_matrix_prologue;
            if(!signature_ok)sig[0]=0x90;
            std::memcpy(dst,sig.data(),sig.size());
            return true;
        }
        if(a==jkgm_node_angles_address && len==jkgm_node_angles_prologue.size()) {
            auto sig=jkgm_node_angles_prologue;
            if(!signature_ok)sig[0]=0x90;
            std::memcpy(dst,sig.data(),sig.size());
            return true;
        }
        if(a<base || a-base>memory.size() ||
           len>memory.size()-(a-base))return false;
        std::memcpy(dst,memory.data()+a-base,len);
        return true;
    };
    assert(validate_native_thing_node_accessor(reader));
    assert(validate_native_thing_node_angles(reader));
    signature_ok=false;
    assert(!validate_native_thing_node_accessor(reader));
    assert(!validate_native_thing_node_angles(reader));
    signature_ok=true;
    thing_node_native_input input{};
    assert(resolve_native_thing_node(reader,thing,2,input));
    assert(input.rd_thing==thing+0xc4);
    assert(close(input.world_placement[0],0));
    assert(close(input.world_placement[1],1));
    assert(close(input.world_placement[3],-1));
    assert(close(input.world_placement[9],20));
    assert(close(input.world_placement[10],30));
    assert(close(input.world_placement[11],40));
    assert(!resolve_native_thing_node(reader,thing,3,input));
    assert(!resolve_native_thing_node(reader,thing,-1,input));
    set32(thing+0xc4,4); // Sprite is not a model
    assert(!resolve_native_thing_node(reader,thing,0,input));
    set32(thing+0xc4,1);
    set32(model+0x54,99999);
    assert(!resolve_native_thing_node(reader,thing,0,input));
    set32(model+0x54,3);
    set32(thing+0xc8,0);
    assert(!resolve_native_thing_node(reader,thing,0,input));
    set32(thing+0xc8,uint32_t(model));
    float nan=std::numeric_limits<float>::quiet_NaN();
    std::memcpy(memory.data()+thing+0xb8-base,&nan,4);
    assert(!resolve_native_thing_node(reader,thing,0,input));
}
