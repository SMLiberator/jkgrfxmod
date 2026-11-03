#include "renderer/jk_cmp_fullbright_mask.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

int main()
{
    using namespace jkgm;
    constexpr uint32_t cmp=0x30000U,light_table=0x40000U;
    constexpr uint32_t mip=0x50000U,pixels=0x60000U;
    std::vector<uint8_t> memory(0x80000U,0);
    std::array<uint8_t,768> palette{};
    for(unsigned i=0;i<256U;++i) {
        palette[3*i]=static_cast<uint8_t>(i);
        palette[3*i+1]=static_cast<uint8_t>(i/2U);
        palette[3*i+2]=static_cast<uint8_t>(i/3U);
    }
    // Index 11 alternates between different indices of identical RGB.
    for(int c=0;c<3;++c)palette[12U*3U+c]=palette[11U*3U+c];
    std::memcpy(memory.data()+cmp+jk_cmp_colors_offset,
                palette.data(),palette.size());
    jk_cmp_lighttable table{};
    for(unsigned level=0;level<64U;++level)
        for(unsigned index=0;index<256U;++index)
            table[level*256U+index]=level==0U ? 0U :
                static_cast<uint8_t>(index);
    for(unsigned level=0;level<64U;++level) {
        table[level*256U+9U]=9U;
        table[level*256U+10U]=10U;
        table[level*256U+11U]=static_cast<uint8_t>(11U+(level&1U));
    }
    std::memcpy(memory.data()+cmp+jk_cmp_lightlevel_offset,
                &light_table,sizeof(light_table));
    std::memcpy(memory.data()+light_table,table.data(),table.size());
    jk_indexed_mip_prefix source{};
    source.width=4;
    source.height=2;
    source.row_size=6; // padded native rows
    source.row_width=4;
    source.size=12;
    source.format[0]=0;
    source.format[1]=8;
    source.texels=pixels;
    std::memcpy(memory.data()+mip,&source,sizeof(source));
    uint8_t const source_pixels[]={9,2,0,11,255,255,2,10,11,0,255,255};
    std::memcpy(memory.data()+pixels,source_pixels,sizeof(source_pixels));
    auto read=[&](uintptr_t addr,void* dest,size_t count)->bool {
        if(addr>memory.size()||count>memory.size()-addr)return false;
        std::memcpy(dest,memory.data()+addr,count);
        return true;
    };
    std::vector<char> mask;
    assert(jk_cmp_build_fullbright_mask(
        read,cmp,mip,4U,2U,true,0U,mask));
    assert(mask.size()==4U*2U*4U);
    uint8_t const expected[]={255,0,0,255,0,255,255,0};
    for(size_t i=0;i<8U;++i) {
        assert(static_cast<uint8_t>(mask[i*4U])==expected[i]);
        assert(static_cast<uint8_t>(mask[i*4U+1U])==expected[i]);
        assert(static_cast<uint8_t>(mask[i*4U+2U])==expected[i]);
        assert(static_cast<uint8_t>(mask[i*4U+3U])==255U);
    }
    // Incompatible dimensions and invalid pointers/format fail closed.
    assert(!jk_cmp_build_fullbright_mask(
        read,cmp,mip,8U,2U,true,0U,mask));
    assert(mask.empty());
    source.format[0]=1;
    std::memcpy(memory.data()+mip,&source,sizeof(source));
    assert(!jk_cmp_build_fullbright_mask(
        read,cmp,mip,4U,2U,true,0U,mask));
    source.format[0]=0;
    source.row_size=3;
    std::memcpy(memory.data()+mip,&source,sizeof(source));
    assert(!jk_cmp_build_fullbright_mask(
        read,cmp,mip,4U,2U,true,0U,mask));
    source.row_size=6;
    source.texels=0x7fffffffU;
    std::memcpy(memory.data()+mip,&source,sizeof(source));
    assert(!jk_cmp_build_fullbright_mask(
        read,cmp,mip,4U,2U,true,0U,mask));
    assert(mask.empty());
    return 0;
}
