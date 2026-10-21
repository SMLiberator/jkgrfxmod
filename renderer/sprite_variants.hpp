#pragma once
// One ordinary 11-field .SPR line. Extra lines describe independently
// selectable variants of the FIRST line; existing single-line SPRs are
// unchanged. The engine tokenizer supplies already-separated fields.
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>

namespace jkgm {
struct sprite_variant_spec {
    std::string material;
    uint32_t type=0;
    float width=0,height=0;
    int geometry=0,lighting=0,texture=0;
    float extra_light=0;
    std::array<float,3> offset{};
};
inline bool sprite_variant_int(std::string const &src,int &dst) {
    if(src.empty())return false;
    char *end=nullptr;
    errno=0;
    long const value=std::strtol(src.c_str(),&end,10);
    if(errno==ERANGE || end==src.c_str() || *end!='\0' ||
       value<std::numeric_limits<int>::min() ||
       value>std::numeric_limits<int>::max())return false;
    dst=static_cast<int>(value);
    return true;
}
inline bool sprite_variant_float(std::string const &src,float &dst) {
    if(src.empty())return false;
    char *end=nullptr;
    errno=0;
    float const value=std::strtof(src.c_str(),&end);
    if(errno==ERANGE || end==src.c_str() || *end!='\0' ||
       !std::isfinite(value))return false;
    dst=value;
    return true;
}
inline bool parse_sprite_variant(
    std::array<std::string,11> const &fields,sprite_variant_spec &out)
{
    sprite_variant_spec v{};
    // JK's rdSprite stores a maximum 31-byte material filename in the
    // temporary parser and uses an 0x20-byte resource name.
    if(fields[0].empty() || fields[0].size()>31)return false;
    v.material=fields[0];
    int type=0;
    if(!sprite_variant_int(fields[1],type) || type<0 || type>127 ||
       !sprite_variant_float(fields[2],v.width) ||
       !sprite_variant_float(fields[3],v.height) ||
       !sprite_variant_int(fields[4],v.geometry) ||
       !sprite_variant_int(fields[5],v.lighting) ||
       !sprite_variant_int(fields[6],v.texture) ||
       !sprite_variant_float(fields[7],v.extra_light) ||
       !sprite_variant_float(fields[8],v.offset[0]) ||
       !sprite_variant_float(fields[9],v.offset[1]) ||
       !sprite_variant_float(fields[10],v.offset[2]) ||
       v.width<=0.0f || v.height<=0.0f)
        return false;
    v.type=static_cast<uint32_t>(type);
    out=std::move(v);
    return true;
}
template<class RandomEngine>
inline size_t choose_sprite_variant(size_t count,RandomEngine &rng) {
    if(count<=1)return 0;
    return std::uniform_int_distribution<size_t>(0,count-1)(rng);
}
}
