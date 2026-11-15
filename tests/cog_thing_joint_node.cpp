#include "renderer/cog_thing_joint_node.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

struct region {
    std::uintptr_t base;
    std::vector<unsigned char> bytes;
};
int main() {
    std::vector<region> regions{
        {0x30000U,std::vector<unsigned char>(0x200)},  // Thing
        {0x40000U,std::vector<unsigned char>(0x40)},   // puppet animclass
        {0x10000U,std::vector<unsigned char>(0x200)},  // current world
        {0x20000U,std::vector<unsigned char>(0x200)},  // static world
        {0x50000U,std::vector<unsigned char>(3*0x1068)},
        {0x70000U,std::vector<unsigned char>(3*0x1068)},
        {0x833a10U,std::vector<unsigned char>(8)}
    };
    auto read=[&](std::uintptr_t address,void *out,std::size_t length) {
        for(auto const &r:regions) {
            if(address>=r.base && address-r.base<=r.bytes.size() &&
               length<=r.bytes.size()-(address-r.base)) {
                std::memcpy(out,r.bytes.data()+(address-r.base),length);
                return true;
            }
        }
        return false;
    };
    auto write=[&](std::uintptr_t address,auto value) {
        for(auto &r:regions) {
            if(address>=r.base && address-r.base<=r.bytes.size() &&
               sizeof(value)<=r.bytes.size()-(address-r.base)) {
                std::memcpy(r.bytes.data()+(address-r.base),&value,sizeof(value));
                return;
            }
        }
        assert(false && "invalid fixture write");
    };
    using jkgm::cog_get_thing_joint_node;
    assert(cog_get_thing_joint_node(read,0,0)==-1);
    assert(cog_get_thing_joint_node(read,0x30000U,0)==-1);
    write(0x30000U+0x128U,std::uint32_t{0x40000U});
    assert(cog_get_thing_joint_node(read,0x30000U,0)==-1);
    write(0x40000U+0x1eU,std::uint16_t{1});
    write(jkgm::cog_joint_current_world_global,std::uint32_t{0x10000U});
    write(0x10000U+0x174U,std::uint32_t{3});
    write(0x10000U+0x17cU,std::uint32_t{0x50000U});
    write(0x50000U+0x1068U+0x1040U+6U*4U,std::int32_t{42});
    write(0x50000U+0x1068U+0x1040U+9U*4U,std::int32_t{137});
    write(0x50000U+0x1068U+0x1040U,std::int32_t{0});
    assert(cog_get_thing_joint_node(read,0x30000U,6)==42);
    assert(cog_get_thing_joint_node(read,0x30000U,9)==137);
    assert(cog_get_thing_joint_node(read,0x30000U,0)==0);
    assert(cog_get_thing_joint_node(read,0x30000U,4)==0);
    assert(cog_get_thing_joint_node(read,0x30000U,-1)==-1);
    assert(cog_get_thing_joint_node(read,0x30000U,10)==-1);
    write(0x40000U+0x1eU,std::uint16_t{3});
    assert(cog_get_thing_joint_node(read,0x30000U,6)==-1); // class out of range
    write(0x40000U+0x1eU,std::uint16_t{0x8002U});
    write(jkgm::cog_joint_static_world_global,std::uint32_t{0x20000U});
    write(0x20000U+0x174U,std::uint32_t{3});
    write(0x20000U+0x17cU,std::uint32_t{0x70000U});
    write(0x70000U+2*0x1068U+0x1040U+6U*4U,std::int32_t{99});
    assert(cog_get_thing_joint_node(read,0x30000U,6)==99);
    write(jkgm::cog_joint_static_world_global,std::uint32_t{0});
    assert(cog_get_thing_joint_node(read,0x30000U,6)==-1);
    write(0x30000U+0x128U,std::uint32_t{0xdead0000U});
    assert(cog_get_thing_joint_node(read,0x30000U,6)==-1);
}
