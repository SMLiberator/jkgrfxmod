#include "renderer/cog_create_thing.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

int main() {
    std::array<unsigned char,0x100> thing{};
    std::array<unsigned char,0x40> templ{};
    auto read=[&](std::uintptr_t address,void *dest,std::size_t len)->bool {
        std::uintptr_t start=0;
        void const *src=nullptr;
        if(address>=0x20000U && address+len<=0x20100U) {
            start=0x20000U;src=thing.data();
        } else if(address>=0x30000U && address+len<=0x30040U) {
            start=0x30000U;src=templ.data();
        } else return false;
        std::memcpy(dest,static_cast<unsigned char const*>(src)+
                          (address-start),len);
        return true;
    };
    using jkgm::cog_get_create_thing_template_id;
    assert(cog_get_create_thing_template_id(read,0U)==-1);
    assert(cog_get_create_thing_template_id(read,0x20000U)==-1);
    std::uint32_t invalid=0x50000U;
    std::memcpy(thing.data()+0x7c,&invalid,4U);
    assert(cog_get_create_thing_template_id(read,0x20000U)==-1);
    std::uint32_t pointer=0x30000U;
    std::memcpy(thing.data()+0x7c,&pointer,4U);
    std::int32_t id=27;
    std::memcpy(templ.data()+4U,&id,4U);
    assert(cog_get_create_thing_template_id(read,0x20000U)==27);
    id=-1;
    std::memcpy(templ.data()+4U,&id,4U);
    assert(cog_get_create_thing_template_id(read,0x20000U)==-1);
}
