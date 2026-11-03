#include "renderer/jk_cmp_gpu.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include <vector>
int main() {
    using namespace jkgm;
    static_assert(jk_cmp_lightlevel_offset==0x330U,"CMP memory layout changed");
    jk_cmp_lighttable levels{};
    for(size_t j=0;j<256U;j++)
        for(size_t light=0;light<64U;light++)
            levels[light*256U+j]=uint8_t((j+light)%256U);
    // Index 9 remains unaffected by all 64 software lighting values.
    for(size_t light=0;light<64U;light++)levels[light*256U+9U]=9;
    assert(jk_cmp_is_fullbright(levels,9U));
    assert(!jk_cmp_is_fullbright(levels,7U));
    // A constant remap is also software-unshaded.
    for(size_t light=0;light<64U;light++)levels[light*256U+18U]=19;
    assert(jk_cmp_is_fullbright(levels,18U));

    constexpr uint32_t sector=0x20000U,cmp=0x30000U,table=0x40000U;
    std::vector<uint8_t> bytes(0x50000U,0);
    std::memcpy(bytes.data()+sector+0x0c,&cmp,4);
    std::array<float,3> tint{{0.10f,0.55f,0.9f}};
    std::memcpy(bytes.data()+cmp+jk_cmp_tint_offset,tint.data(),12);
    std::memcpy(bytes.data()+cmp+jk_cmp_lightlevel_offset,&table,4);
    std::memcpy(bytes.data()+table,levels.data(),levels.size());
    auto read=[&](uintptr_t addr,void* out,size_t len) {
        if(addr>bytes.size()||len>bytes.size()-addr)return false;
        std::memcpy(out,bytes.data()+addr,len);
        return true;
    };
    std::array<float,3> output{};
    assert(jk_sector_cmp_tint(read,sector,output));
    assert(output==tint);
    jk_cmp_lighttable reread{};
    assert(jk_cmp_read_lighttable(read,cmp,reread));
    assert(reread==levels);
    // Null sector CMP is not mistaken for sector tint (sector +0x10).
    uint32_t zero=0;
    std::memcpy(bytes.data()+sector+0x0c,&zero,4);
    assert(!jk_sector_cmp_tint(read,sector,output));
    assert(!jk_cmp_read_tint(read,0U,output));
    float nan=std::nanf("");
    std::memcpy(bytes.data()+cmp+jk_cmp_tint_offset,&nan,4);
    assert(!jk_cmp_read_tint(read,cmp,output));
    return 0;
}
