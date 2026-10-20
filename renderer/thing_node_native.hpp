#pragma once
// Address-independent validation of JKGM SithThing -> rdThing -> rdModel3.
// The real engine accessor (identified at 0x445FD0 for the tested JKGM
// executable) refreshes the pose and must be invoked only AFTER these checks.
#include "thing_node_math.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>

namespace jkgm {
    using thing_node_reader=std::function<bool(uintptr_t,void*,size_t)>;
    struct thing_node_native_input {
        uintptr_t rd_thing=0;
        gpu_3do_matrix34 world_placement{};
    };
    constexpr uintptr_t jkgm_node_matrix_address=0x00445FD0U;
    constexpr uintptr_t jkgm_node_angles_address=0x00440950U;
    constexpr std::array<uint8_t,16> jkgm_node_angles_prologue{{
        0x83,0xec,0x20,0x8b,0x44,0x24,0x24,0x56,
        0x8b,0x48,0x0c,0x8b,0x50,0x10,0x89,0x4c
    }};
    constexpr std::array<uint8_t,16> jkgm_node_matrix_prologue{{
        0x56,0x8b,0x74,0x24,0x08,0x57,0x8b,0x7c,
        0x24,0x14,0x8b,0x46,0x04,0x3b,0x78,0x54
    }};

    inline bool thing_node_valid_address(uintptr_t ptr,size_t size) {
        return ptr>=0x10000U && ptr<=0x7fffffffU &&
               size<=0x10000U && ptr+size>=ptr &&
               ptr+size<=0x80000000U;
    }
    inline bool resolve_native_thing_node(
        thing_node_reader const &read,uintptr_t thing,int node,
        thing_node_native_input &out)
    {
        if(!read || !thing_node_valid_address(thing,0xe8) ||
           node<0 || node>=4096)return false;
        uint32_t kind=0,rd_type=0,model=0,count=0;
        uintptr_t const rd=thing+0xc4U;
        if(!read(thing+0x0c,&kind,4) || kind==0 || kind>15 ||
           !read(rd,&rd_type,4) || rd_type!=1U ||
           !read(rd+4,&model,4) ||
           !thing_node_valid_address(model,0x58) ||
           !read(uintptr_t(model)+0x54,&count,4) ||
           count==0 || count>4096 || unsigned(node)>=count)
            return false;
        gpu_3do_matrix34 placement{};
        if(!read(thing+0x88,placement.data(),sizeof(placement)) ||
           !read(thing+0xb8,placement.data()+9,3*sizeof(float)) ||
           !gpu_3do_matrix_finite(placement))
            return false;
        out={rd,placement};
        return true;
    }

    inline bool validate_native_thing_node_accessor(
        thing_node_reader const &read)
    {
        std::array<uint8_t,16> bytes{};
        return read && read(jkgm_node_matrix_address,
                            bytes.data(),bytes.size()) &&
               bytes==jkgm_node_matrix_prologue;
    }
    inline bool validate_native_thing_node_angles(
        thing_node_reader const &read)
    {
        std::array<uint8_t,16> bytes{};
        return read && read(jkgm_node_angles_address,
                            bytes.data(),bytes.size()) &&
               bytes==jkgm_node_angles_prologue;
    }
}
