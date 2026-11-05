#include "renderer/saved_static_template.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
using namespace jkgm::saved_static_template;

struct fake_memory {
    std::map<uintptr_t,uint8_t> data;
    template<class T> void put(uintptr_t at,T const &value) {
        uint8_t bytes[sizeof(T)]{};
        std::memcpy(bytes,&value,sizeof(T));
        for(size_t i=0;i<sizeof(T);++i)data[at+i]=bytes[i];
    }
    bool operator()(uintptr_t at,void *out,size_t len) const {
        auto *p=static_cast<uint8_t *>(out);
        for(size_t i=0;i<len;++i) {
            auto it=data.find(at+i);
            if(it==data.end())return false;
            p[i]=it->second;
        }
        return true;
    }
};
int main() {
    fake_memory m;
    uint32_t level_world=0x180000U,static_world=0x190000U;
    uint32_t level_array=0x200000U,static_array=0x300000U;
    m.put(current_world_global,level_world);
    m.put(static_world_global,static_world);
    m.put(uintptr_t(level_world)+world_template_count_offset,int32_t(20));
    m.put(uintptr_t(static_world)+world_template_count_offset,int32_t(400));
    m.put(uintptr_t(level_world)+world_template_array_offset,level_array);
    m.put(uintptr_t(static_world)+world_template_array_offset,static_array);

    // Level template #7; original layout and index are unchanged.
    m.put(uintptr_t(level_array)+7*thing_stride+template_index_offset,
          uint32_t(7));
    auto const a=resolve(7,m);
    assert(a && !a.static_world && a.local_index==7);
    assert(a.address==uintptr_t(level_array)+7*thing_stride);

    // Static.jkl template #360: signed int16_t is negative, but its
    // serialized bit pattern is 0x8168 and MUST select staticWorld.
    constexpr uint16_t static_saved_id=0x8168U;
    static_assert(int16_t(static_saved_id)<0,"static index signed high bit");
    m.put(uintptr_t(static_array)+360*thing_stride+template_index_offset,
          uint32_t(static_saved_id));
    auto const b=resolve(static_saved_id,m);
    assert(b && b.static_world && b.local_index==360);
    assert(b.address==uintptr_t(static_array)+360*thing_stride);
    assert(b.address!=a.address);

    // Template zero is legal in the full-description load path.
    m.put(uintptr_t(level_array)+template_index_offset,uint32_t(0));
    assert(resolve(0,m));

    // An invalid static index may NOT spill into the level template table.
    auto const oob=resolve(uint16_t(0x8200U),m);
    assert(!oob && oob.reason==failure::index_out_of_range);

    // Missing Static.jkl or stale template array should fail closed.
    m.put(static_world_global,uint32_t(0));
    auto const absent=resolve(static_saved_id,m);
    assert(!absent && absent.reason==failure::no_world);
    m.put(static_world_global,static_world);
    m.put(uintptr_t(static_world)+world_template_array_offset,uint32_t(0));
    auto const no_array=resolve(static_saved_id,m);
    assert(!no_array && no_array.reason==failure::no_array);
    m.put(uintptr_t(static_world)+world_template_array_offset,static_array);

    // Reject stale or reordered template allocations.
    m.put(uintptr_t(static_array)+360*thing_stride+template_index_offset,
          uint32_t(0x8169U));
    auto const stale=resolve(static_saved_id,m);
    assert(!stale && stale.reason==failure::mismatched_template_id);

    // An unmapped template must not be handed to native SetThingBasedOn.
    m.put(uintptr_t(static_array)+360*thing_stride+template_index_offset,
          uint32_t(static_saved_id));
    m.put(uintptr_t(static_world)+world_template_array_offset,
          uint32_t(0x400000U));
    auto const unmapped=resolve(static_saved_id,m);
    assert(!unmapped && unmapped.reason==failure::unreadable_template);

    // Validate the actual jkgm(5).exe signature and x86 thing stride.
    static_assert(thing_stride==0x2B0U,"unexpected SithThing stride");
    static_assert(restore_index_address==0x004F49F8U,"unexpected patch gate");
    static_assert(restore_copy_bytes[2]==0xE8U,"missing native clone call");
    return 0;
}
