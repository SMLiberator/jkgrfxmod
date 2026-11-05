#pragma once
// Native JKGM savegame restore: signed 16-bit template IDs with bit 0x8000
// must resolve in Static.jkl, not through the current level's template array.
// Memory layout/stride verified against the user's jkgm(5).exe.
#include <cstddef>
#include <cstdint>

namespace jkgm { namespace saved_static_template {
constexpr uintptr_t restore_index_address=0x004F49F8U;
constexpr uintptr_t restore_success_address=0x004F4A2EU;
constexpr uintptr_t restore_failure_address=0x004F4E9BU;
constexpr uintptr_t native_copy_address=0x004CDB70U;
constexpr uintptr_t current_world_global=0x00833A10U;
constexpr uintptr_t static_world_global=0x00833A14U;
constexpr uintptr_t world_template_count_offset=0xC8U;
constexpr uintptr_t world_template_array_offset=0xD0U;
constexpr uintptr_t template_index_offset=4U;
constexpr uint64_t thing_stride=0x2B0U;
// Bytes are from the supplied modified binary, not OpenJKDF2. Fail closed
// if the loader is attached to a different executable or patch revision.
constexpr uint8_t restore_prologue[]{
    0x8b,0x44,0x24,0x04,0x53,0x8b,0x1d,0x10,0x3a,0x83,0x00};
constexpr uint8_t restore_index_bytes[]{
    0x0f,0xbf,0x0e,0x8b,0x83,0xc8,0x00,0x00,0x00,
    0x83,0xc4,0x04,0x83,0xc6,0x02,0x3b,0xc8};
constexpr uint8_t restore_copy_bytes[]{
    0x51,0x57,0xe8,0x42,0x91,0xfd,0xff,0x8b,0x16};
constexpr uint8_t restore_fail_bytes[]{
    0x5f,0x5e,0x5d,0x33,0xc0,0x5b,0xc3};

// Read returns whether [address,address+bytes) was safely copied to dest.
// The resolver never mutates a savegame packet or any native SithWorld.
enum class failure {
    none, no_world, no_array, index_out_of_range, unreadable_template,
    mismatched_template_id
};
struct result {
    uintptr_t address=0;
    uint16_t saved_index=0;
    uint16_t local_index=0;
    bool static_world=false;
    failure reason=failure::none;
    explicit operator bool() const {return address!=0;}
};
template<class Read>
result resolve(uint16_t serialized_index,Read const &read) {
    result out{};
    out.saved_index=serialized_index;
    out.local_index=uint16_t(serialized_index&0x7FFFU);
    out.static_world=(serialized_index&0x8000U)!=0U;
    uint32_t world=0U;
    if(!read(out.static_world?static_world_global:current_world_global,
             &world,sizeof(world)) || world<0x10000U) {
        out.reason=failure::no_world;return out;
    }
    int32_t count=0;
    uint32_t array=0U;
    if(!read(uintptr_t(world)+world_template_count_offset,
             &count,sizeof(count)) || count<=0 || count>0x8000) {
        out.reason=failure::index_out_of_range;return out;
    }
    if(out.local_index>=uint32_t(count)) {
        out.reason=failure::index_out_of_range;return out;
    }
    if(!read(uintptr_t(world)+world_template_array_offset,
             &array,sizeof(array)) || array<0x10000U) {
        out.reason=failure::no_array;return out;
    }
    uint64_t const address=uint64_t(array)+
                           uint64_t(out.local_index)*thing_stride;
    if(address>0xFFFFFFFFULL-template_index_offset-sizeof(uint32_t)) {
        out.reason=failure::unreadable_template;return out;
    }
    uint32_t actual=0U;
    if(!read(uintptr_t(address)+template_index_offset,
             &actual,sizeof(actual))) {
        out.reason=failure::unreadable_template;return out;
    }
    // Static and level templates carry different 0x8000 ownership bits.
    // Checking them also prevents using a recycled/stale template allocation.
    if(uint16_t(actual)!=serialized_index) {
        out.reason=failure::mismatched_template_id;return out;
    }
    out.address=uintptr_t(address);
    return out;
}
}} // namespace jkgm::saved_static_template
