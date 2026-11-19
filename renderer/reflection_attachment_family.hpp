#pragma once
// Per-probe self-exclusion for physically linked Thing assemblies. The flag
// lives on the CHILD's native attachment, not on the parent or ThingFlags.
// This intentionally does not use SithThing's unrelated COG parent pointer.
#include "thing_node_attachment.hpp"
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace jkgm {
inline bool reflection_qualifying_attachment(uint32_t attach_flags)
{
    return native_attach_has_thing_parent(attach_flags) &&
        (attach_flags&(native_attach_no_move|native_attach_node))!=0U;
}

inline bool reflection_attachment_pointer_valid(uintptr_t thing)
{
    return thing>=0x10000U && thing<0x7ff00000U;
}

template<class Read>
std::unordered_set<uint64_t> build_reflection_attachment_family(
    Read const &read,uint64_t source_owner)
{
    std::unordered_set<uint64_t> excluded;
    if(!source_owner)return excluded;
    // Even if the source has been recycled, its original identity still must
    // not appear in its own probe; do not follow a mismatched native link.
    excluded.insert(source_owner);
    uintptr_t const source=uintptr_t(uint32_t(source_owner>>32));
    uint32_t const expected_signature=uint32_t(source_owner);
    constexpr size_t max_members=1024U,max_siblings=4096U;
    auto read32=[&](uintptr_t address,uint32_t &out)->bool {
        return bool(read(address,&out,sizeof(out)));
    };
    auto identity=[&](uintptr_t thing,uint32_t &signature)->bool {
        uint32_t type=0,flags=0;
        return reflection_attachment_pointer_valid(thing) &&
            read32(thing+0x00U,flags) &&
            read32(thing+0x0cU,type) &&
            read32(thing+0x74U,signature) &&
            type>1U && type<=15U && signature!=0U &&
            (flags&0x2U)==0U;
    };
    uint32_t current_signature=0;
    if(!identity(source,current_signature) ||
       current_signature!=expected_signature)return excluded;

    struct member {uintptr_t thing;uint32_t signature;};
    std::vector<member> pending{{source,current_signature}};
    std::unordered_set<uintptr_t> visited{source};
    auto include=[&](uintptr_t thing) {
        if(visited.count(thing) || pending.size()>=max_members)return;
        uint32_t signature=0;
        if(!identity(thing,signature))return;
        visited.insert(thing);
        pending.push_back({thing,signature});
        excluded.insert((uint64_t(uint32_t(thing))<<32)|signature);
    };
    for(size_t i=0;i<pending.size();++i) {
        auto const node=pending[i];
        uint32_t signature=0;
        if(!identity(node.thing,signature) || signature!=node.signature)
            continue;
        uint32_t attach_flags=0,parent=0;
        if(read32(node.thing+0x34U,attach_flags) &&
           reflection_qualifying_attachment(attach_flags) &&
           read32(node.thing+0x58U,parent))
            include(uintptr_t(parent));

        // Native parents maintain an intrusive list at +0x68; each child's
        // next-sibling link is +0x6c. Only a child's own qualifying flags
        // allow passage along that edge. An ordinary attachment cannot
        // bridge two otherwise distinct reflection families.
        uint32_t child=0;
        if(!read32(node.thing+0x68U,child))continue;
        std::unordered_set<uintptr_t> siblings;
        for(size_t n=0;child && n<max_siblings;++n) {
            uintptr_t const at=uintptr_t(child);
            if(!reflection_attachment_pointer_valid(at) ||
               !siblings.insert(at).second)break;
            uint32_t next=0,child_flags=0,attached_parent=0;
            if(!read32(at+0x6cU,next) ||
               !read32(at+0x34U,child_flags) ||
               !read32(at+0x58U,attached_parent))break;
            if(attached_parent==node.thing &&
               reflection_qualifying_attachment(child_flags))
                include(at);
            child=next;
        }
    }
    return excluded;
}
} // namespace jkgm
