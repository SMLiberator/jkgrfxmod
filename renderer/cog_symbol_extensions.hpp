#pragma once
// COG declaration extensions; no new VM value types or JKL syntax.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace jkgm::cog_symbol_extensions {
constexpr int sprite_ref=14, soundclass_ref=15, puppet_ref=16;
inline int resource_ref_type(char const *name) {
    if(!name)return 0;
    if(std::strcmp(name,"sprite")==0)return sprite_ref;
    if(std::strcmp(name,"soundclass")==0)return soundclass_ref;
    if(std::strcmp(name,"puppet")==0)return puppet_ref;
    return 0;
}
// Strict COG (x/y/z) syntax. Reject malformed, nonfinite, or partial input.
inline bool parse_initial_vector(char const *p,std::array<float,3>&out) {
    if(!p)return false;
    auto space=[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    while(space(*p))++p;
    if(*p++!='(')return false;
    std::array<float,3> values{};
    for(int i=0;i<3;++i) {
        while(space(*p))++p;
        char *end=nullptr;
        values[i]=std::strtof(p,&end);
        if(end==p || !std::isfinite(values[i]))return false;
        p=end;
        while(space(*p))++p;
        if(*p++!=(i==2?')':'/'))return false;
    }
    while(space(*p))++p;
    if(*p) return false;
    out=values;
    return true;
}
inline int resource_index(std::uintptr_t pointer,std::uintptr_t base,
                          std::uint32_t count,std::size_t stride) {
    if(!pointer||!base||!stride||pointer<base)return -1;
    auto const offset=pointer-base;
    if(offset%stride || offset/stride>=count || offset/stride>0x7fffU)
        return -1;
    return static_cast<int>(offset/stride);
}
}
