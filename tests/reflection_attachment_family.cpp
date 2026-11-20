#include "renderer/reflection_attachment_family.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_map>

int main()
{
    using namespace jkgm;
    std::unordered_map<uintptr_t,uint8_t> memory;
    auto put=[&](uintptr_t address,auto const &value) {
        uint8_t bytes[sizeof(value)];
        std::memcpy(bytes,&value,sizeof(value));
        for(size_t i=0;i<sizeof(value);++i)memory[address+i]=bytes[i];
    };
    auto read=[&](uintptr_t address,void *dst,size_t size)->bool {
        auto *bytes=static_cast<uint8_t*>(dst);
        for(size_t i=0;i<size;++i) {
            auto it=memory.find(address+i);
            if(it==memory.end())return false;
            bytes[i]=it->second;
        }
        return true;
    };
    constexpr uintptr_t body=0x20000U, wheel=0x30000U;
    constexpr uintptr_t door=0x40000U, prop=0x50000U;
    constexpr uintptr_t small=0x60000U, small_child=0x70000U;
    constexpr uintptr_t other=0x80000U, other_part=0x90000U;
    constexpr uintptr_t ground=0xa0000U;
    auto make=[&](uintptr_t at,uint32_t signature,uint32_t attachment,
                  uintptr_t parent,uintptr_t first_child,uintptr_t next) {
        put(at+0x00U,uint32_t(0));        // ThingFlags
        put(at+0x0cU,uint32_t(7));        // Cog
        put(at+0x34U,attachment);         // ATTACH flags
        put(at+0x58U,uint32_t(parent));  // attachedThing
        put(at+0x68U,uint32_t(first_child));
        put(at+0x6cU,uint32_t(next));
        put(at+0x74U,signature);
    };
    auto key=[](uintptr_t at,uint32_t sig)->uint64_t {
        return (uint64_t(uint32_t(at))<<32)|sig;
    };
    // Body -> wheel(NODE+NOMOVE), door(THINGFACE+NOMOVE),
    //         prop(ordinary THING, no NOMOVE). Wheel -> small(NODE only).
    make(body,11,0,0,wheel,0);
    make(wheel,12,0x8cU,body,small,door);
    make(door,13,0x0aU,body,0,prop);
    make(prop,14,0x04U,body,small_child,0);
    make(small,15,0x84U,wheel,0,0);
    make(small_child,16,0x8cU,prop,0,0);
    make(other,17,0,0,other_part,0);
    make(other_part,18,0x8cU,other,0,0);
    make(ground,19,0x09U,0,0,0); // surface+NOMOVE isn't Thing-parent

    assert(reflection_qualifying_attachment(0x8cU));
    assert(reflection_qualifying_attachment(0x84U));
    assert(reflection_qualifying_attachment(0x0aU));
    assert(!reflection_qualifying_attachment(0x04U));
    assert(!reflection_qualifying_attachment(0x09U));
    assert(!reflection_qualifying_attachment(0x80U));

    for(auto source:{key(body,11),key(wheel,12),key(door,13),key(small,15)}) {
        auto family=build_reflection_attachment_family(read,source);
        assert(family.size()==4U);
        for(auto member:{key(body,11),key(wheel,12),key(door,13),key(small,15)})
            assert(family.count(member)!=0U);
        assert(family.count(key(prop,14))==0U);
        assert(family.count(key(small_child,16))==0U);
        assert(family.count(key(other,17))==0U);
        assert(family.count(key(other_part,18))==0U);
    }
    // A nonqualifying parent edge cuts the family: prop and its own
    // qualifying child exclude each other but not the vehicle.
    auto prop_family=build_reflection_attachment_family(read,key(prop,14));
    assert(prop_family.size()==2U && prop_family.count(key(prop,14)) &&
           prop_family.count(key(small_child,16)) &&
           !prop_family.count(key(body,11)));
    auto unrelated=build_reflection_attachment_family(read,key(other_part,18));
    assert(unrelated.size()==2U && unrelated.count(key(other,17)) &&
           !unrelated.count(key(wheel,12)));
    assert(build_reflection_attachment_family(read,key(ground,19)).size()==1U);

    // Recycled source Thing cannot accidentally inherit a new Thing's family.
    auto stale=build_reflection_attachment_family(read,key(body,99));
    assert(stale.size()==1U && stale.count(key(body,99)));
    assert(build_reflection_attachment_family(read,0U).empty());
    // Native list corruption must terminate without following invalid links.
    put(door+0x6cU,uint32_t(wheel)); // sibling loop
    auto cyclic=build_reflection_attachment_family(read,key(body,11));
    assert(cyclic.size()==4U);
    put(door+0x6cU,uint32_t(prop));
    put(prop+0x6cU,uint32_t(0x42)); // invalid next sibling
    auto malformed=build_reflection_attachment_family(read,key(body,11));
    assert(malformed.size()==4U);
    return 0;
}
