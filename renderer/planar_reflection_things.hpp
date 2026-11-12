#pragma once
// Read-only, camera-independent SithThing discovery for future planar captures.
// OpenJKDF2: SithWorld owns ALL sectors and Things, whereas sithRender only
// traverses pFirstThingInSector in visible sectors. Traverse every sector
// without trusting an unverified SithThing allocation stride or using JK's
// render callbacks. Layout offsets match existing JKGM native readers.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace jkgm {
    struct planar_reflection_model_thing {
        uintptr_t thing=0;
        uint32_t index=0,signature=0;
        uint32_t model=0,sector=0;
        std::array<float,3> position{};
    };
    enum class planar_reflection_thing_error {
        none,invalid_world,invalid_sector,invalid_thing,invalid_link
    };
    struct planar_reflection_thing_scan {
        std::vector<planar_reflection_model_thing> models;
        uint32_t sectors=0,linked_things=0,excluded_flags=0,non_models=0;
        planar_reflection_thing_error error=planar_reflection_thing_error::none;
    };
    inline char const *planar_reflection_thing_error_name(
        planar_reflection_thing_error value)
    {
        switch(value) {
        case planar_reflection_thing_error::none:return "none";
        case planar_reflection_thing_error::invalid_world:return "invalid_world";
        case planar_reflection_thing_error::invalid_sector:return "invalid_sector";
        case planar_reflection_thing_error::invalid_thing:return "invalid_thing";
        case planar_reflection_thing_error::invalid_link:return "invalid_link";
        }
        return "unknown";
    }
    inline bool planar_reflection_valid_native_pointer(uintptr_t value)
    {
        return value>=0x10000U && value<0x7ff00000U;
    }
    // A malformed list fails CLOSED, clearing all partially discovered
    // Things. This avoids passing stale or unsafe geometry to a later pass.
    template<class Read>
    bool collect_planar_reflection_things(
        Read const &read,uintptr_t world,planar_reflection_thing_scan &out)
    {
        out={};
        auto fail=[&](planar_reflection_thing_error reason) {
            out={};out.error=reason;return false;
        };
        if(!planar_reflection_valid_native_pointer(world))
            return fail(planar_reflection_thing_error::invalid_world);
        auto read32=[&](uintptr_t at,uint32_t &value)->bool {
            return bool(read(at,&value,sizeof(value)));
        };
        uint32_t sector_count=0,sector_base=0;
        uint32_t thing_capacity=0,thing_base=0;
        int32_t highest_index=-1;
        // OpenJKDF2 SithWorld offsets for standard 32-bit JK. Sector
        // layout/stride also used by JKGM's sector-crossing renderer.
        if(!read32(world+0x4cU,sector_count) ||
           !read32(world+0x50U,sector_base) ||
           !read32(world+0xbcU,thing_capacity) ||
           !read(world+0xc0U,&highest_index,sizeof(highest_index)) ||
           !read32(world+0xc4U,thing_base) ||
           sector_count==0 || sector_count>65536U ||
           thing_capacity==0 || thing_capacity>65536U ||
           highest_index< -1 || highest_index>=int32_t(thing_capacity) ||
           !planar_reflection_valid_native_pointer(sector_base) ||
           !planar_reflection_valid_native_pointer(thing_base) ||
           uintptr_t(sector_base)+uintptr_t(sector_count)*0x98U>=0x7ff00000U)
            return fail(planar_reflection_thing_error::invalid_world);
        out.sectors=sector_count;
        std::unordered_set<uintptr_t> visited;
        visited.reserve(thing_capacity);
        std::unordered_set<uint32_t> indices;
        indices.reserve(thing_capacity);
        for(uint32_t sector_index=0;sector_index<sector_count;++sector_index) {
            uintptr_t const sector=uintptr_t(sector_base)+
                                   uintptr_t(sector_index)*0x98U;
            uint32_t current=0;
            if(!read32(sector+0x30U,current))
                return fail(planar_reflection_thing_error::invalid_sector);
            while(current!=0U) {
                if(!planar_reflection_valid_native_pointer(current) ||
                   visited.size()>=thing_capacity ||
                   !visited.insert(current).second)
                    return fail(planar_reflection_thing_error::invalid_link);
                uint32_t flags=0,index=0,type=0,owner=0,next=0;
                if(!read32(uintptr_t(current)+0x00U,flags) ||
                   !read32(uintptr_t(current)+0x04U,index) ||
                   !read32(uintptr_t(current)+0x0cU,type) ||
                   !read32(uintptr_t(current)+0x5cU,owner) ||
                   !read32(uintptr_t(current)+0x60U,next) ||
                   index>=thing_capacity || !indices.insert(index).second)
                    return fail(planar_reflection_thing_error::invalid_thing);
                ++out.linked_things;
                // A sector's linked list must contain its actual Things;
                // do not treat detached/malformed owner links as candidates.
                if(owner!=sector) {
                    ++out.non_models;
                    current=next;continue;
                }
                // OpenJKDF2: destroyed 0x2, LevelGeo 0x8, 0x10,
                // disabled 0x80000. Camera Things are HUD-only here.
                if(type==0U || type==1U ||
                   (flags&(0x2U|0x8U|0x10U|0x80000U))!=0U) {
                    ++out.excluded_flags;
                    current=next;continue;
                }
                uint32_t render_type=0,model=0;
                // SithThing.rdThing at +0xc4; rdThing.type == MODEL3 (1).
                if(!read32(uintptr_t(current)+0xc4U,render_type) ||
                   !read32(uintptr_t(current)+0xc8U,model))
                    return fail(planar_reflection_thing_error::invalid_thing);
                if(render_type!=1U || !planar_reflection_valid_native_pointer(model)) {
                    ++out.non_models;
                    current=next;continue;
                }
                planar_reflection_model_thing item{};
                item.thing=current;
                item.index=index;
                item.sector=sector;
                item.model=model;
                if(!read32(uintptr_t(current)+0x74U,item.signature) ||
                   !read(uintptr_t(current)+0xb8U,item.position.data(),
                         sizeof(item.position)))
                    return fail(planar_reflection_thing_error::invalid_thing);
                if(!std::isfinite(item.position[0]) ||
                   !std::isfinite(item.position[1]) ||
                   !std::isfinite(item.position[2]))
                    return fail(planar_reflection_thing_error::invalid_thing);
                out.models.push_back(item);
                current=next;
            }
        }
        return true;
    }
}
