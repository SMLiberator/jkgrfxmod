#include "renderer/precise_type3_pose_bulk.hpp"
#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
using namespace jkgm;
int main(){
    constexpr uintptr_t base=0x10000000;
    constexpr uintptr_t rd=base+0x100;
    constexpr uintptr_t matrices=base+0x4000;
    std::vector<uint8_t> memory(0x40000);
    auto put=[&](uintptr_t addr,void const* src,size_t bytes){
        assert(addr>=base && addr-base<=memory.size() &&
               bytes<=memory.size()-(addr-base));
        std::memcpy(memory.data()+(addr-base),src,bytes);
    };
    uint32_t const pointer=uint32_t(matrices);
    put(rd+0x20,&pointer,4);
    thing_node_reader const reader=[&](uintptr_t addr,void* dest,size_t bytes){
        if(addr<base || addr-base>memory.size() ||
           bytes>memory.size()-(addr-base) || bytes>0x10000)
            return false;
        std::memcpy(dest,memory.data()+(addr-base),bytes);
        return true;
    };
    gpu_3do_matrix34 first{{1,0,0,0,1,0,0,0,1,10,20,30}};
    std::array<gpu_3do_matrix34,3> small{{
        first,
        {{1,0,0,0,1,0,0,0,1,20,30,40}},
        {{1,0,0,0,1,0,0,0,1,30,40,50}}
    }};
    int refreshes=0;
    std::function<bool(gpu_3do_matrix34&)> refresh=[&](gpu_3do_matrix34 &out){
        ++refreshes;
        put(matrices,small.data(),sizeof(small));
        out=small[0];
        return true;
    };
    std::vector<gpu_3do_matrix34> output{};
    assert(precise_type3_read_pose_batch(reader,rd,3,refresh,output));
    assert(output.size()==3 && output[2][9]==30);
    assert(refreshes==1);
    small[1][9]=125; // Animations change between two separate casts.
    assert(precise_type3_read_pose_batch(reader,rd,3,refresh,output));
    assert(output[1][9]==125 && refreshes==2);

    // Incorrect native accessor/array disagreement must disable bulk path.
    std::function<bool(gpu_3do_matrix34&)> bad_refresh=[&](
        gpu_3do_matrix34 &out){
        put(matrices,small.data(),sizeof(small));
        out=small[1];
        return true;
    };
    assert(!precise_type3_read_pose_batch(
        reader,rd,3,bad_refresh,output));

    // A longer pose must be read in <=64 KiB validated chunks.
    constexpr uint32_t many=1500;
    std::vector<gpu_3do_matrix34> large(many,small[0]);
    large[many-1][9]=987.0f;
    size_t largest_read=0;
    thing_node_reader limited=[&](uintptr_t addr,void* dst,size_t n){
        largest_read=std::max(largest_read,n);
        return reader(addr,dst,n);
    };
    std::function<bool(gpu_3do_matrix34&)> large_refresh=[&](
        gpu_3do_matrix34 &out){
        put(matrices,large.data(),large.size()*sizeof(large[0]));
        out=large[0];
        return true;
    };
    assert(precise_type3_read_pose_batch(
        limited,rd,many,large_refresh,output));
    assert(output.size()==many && output.back()[9]==987.0f);
    assert(largest_read<=0x10000);
    uint32_t const invalid=0x7ffffff0U;
    put(rd+0x20,&invalid,4);
    assert(!precise_type3_read_pose_batch(
        reader,rd,3,refresh,output));
}
