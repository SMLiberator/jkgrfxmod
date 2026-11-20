#pragma once
// Compatibility-safe replacement for JK.dll GetThingCreateThingTemplate.
// The upstream handler dereferences (thing->createThing + 4) without first
// checking whether the createThing pointer is null.
#include <cstdint>
#include <cstring>
#include <limits>

namespace jkgm {
template<class Read>
int cog_get_create_thing_template_id(Read&& read,std::uintptr_t thing) {
    if(!thing || thing>std::numeric_limits<std::uintptr_t>::max()-0x80U)
        return -1;
    std::uint32_t template_ptr=0;
    if(!read(thing+0x7cU,&template_ptr,sizeof(template_ptr)) ||
       template_ptr<0x10000U)
        return -1;
    std::int32_t template_id=-1;
    if(!read(std::uintptr_t(template_ptr)+4U,
             &template_id,sizeof(template_id)))
        return -1;
    return template_id;
}
}
