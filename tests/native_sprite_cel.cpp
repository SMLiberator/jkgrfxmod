#include "renderer/native_sprite_cel.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
int main() {
    constexpr uintptr_t base=0x200000U;
    std::array<uint8_t,0x100U> thing{};
    auto put=[&](uintptr_t offset,int32_t value) {
        assert(offset+sizeof(value)<=thing.size());
        std::memcpy(thing.data()+offset,&value,sizeof(value));
    };
    const auto read=[&](uintptr_t addr,void *dest,size_t len) {
        if(addr<base || addr-base>thing.size() ||
           len>thing.size()-(addr-base))return false;
        std::memcpy(dest,thing.data()+(addr-base),len);
        return true;
    };
    int32_t cel=-42;
    put(0x2cU,0); put(0x30U,99);
    assert(jkgm::read_native_sprite_wall_cel(read,base,cel) && cel==0);
    put(0x2cU,3); put(0x30U,0); // the old wrong field stays zero
    assert(jkgm::read_native_sprite_wall_cel(read,base,cel) && cel==3);
    put(0x2cU,7);
    assert(jkgm::read_native_sprite_wall_cel(read,base,cel) && cel==7);
    put(0x2cU,-1);
    assert(jkgm::read_native_sprite_wall_cel(read,base,cel) && cel==-1);
    put(0x2cU,255);
    assert(jkgm::read_native_sprite_wall_cel(read,base,cel) && cel==255);
    put(0x2cU,256);
    assert(!jkgm::read_native_sprite_wall_cel(read,base,cel));
    put(0x2cU,-2);
    assert(!jkgm::read_native_sprite_wall_cel(read,base,cel));
    assert(!jkgm::read_native_sprite_wall_cel(read,0U,cel));
    const auto no_read=[](uintptr_t,void*,size_t) {return false;};
    assert(!jkgm::read_native_sprite_wall_cel(no_read,base,cel));
}
