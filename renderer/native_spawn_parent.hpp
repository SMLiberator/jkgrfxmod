#pragma once
// Native JKGM automatic template-spawn parent policy and rel32 CALL encoding.
// Portable so the regression harness can exercise the rules without Windows.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace jkgm::native_spawn_parent {
template<class Thing>
Thing *choose_parent(Thing *original_parent,Thing *source) {
    return original_parent ? original_parent : source;
}
template<class Thing,class Root>
Thing *root_or_source(Thing *source,Root&& root) {
    if(!source)return nullptr;
    Thing *const result=root(source);
    return result ? result : source;
}
inline bool make_call(std::uintptr_t site,std::uintptr_t destination,
                      std::array<std::uint8_t,5>&result) {
    if(site>std::numeric_limits<std::uintptr_t>::max()-5U)return false;
    auto const difference=static_cast<std::int64_t>(destination)-
                          static_cast<std::int64_t>(site+5U);
    if(difference<std::numeric_limits<std::int32_t>::min() ||
       difference>std::numeric_limits<std::int32_t>::max())return false;
    result[0]=0xe8U;
    std::uint32_t const bits=static_cast<std::uint32_t>(
        static_cast<std::int32_t>(difference));
    for(unsigned i=0;i<4U;++i)
        result[i+1U]=static_cast<std::uint8_t>(bits>>(i*8U));
    return true;
}
}
