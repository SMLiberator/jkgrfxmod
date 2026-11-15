#include "renderer/cog_symbol_extensions.hpp"
#include <cassert>
#include <array>
#include <cmath>
#include <cstdint>

int main() {
    using namespace jkgm::cog_symbol_extensions;
    assert(resource_ref_type("sprite")==14);
    assert(resource_ref_type("soundclass")==15);
    assert(resource_ref_type("puppet")==16);
    assert(resource_ref_type("vector")==0);
    std::array<float,3> vec{};
    assert(parse_initial_vector("(0/1/1)",vec));
    assert(vec[0]==0.f && vec[1]==1.f && vec[2]==1.f);
    assert(parse_initial_vector(" (-1.5 / 2.25 / 0) ",vec));
    assert(vec[0]==-1.5f && vec[1]==2.25f && vec[2]==0.f);
    assert(!parse_initial_vector("0/1/1",vec));
    assert(!parse_initial_vector("(0/1)",vec));
    assert(!parse_initial_vector("(0/1/1) trailing",vec));
    assert(!parse_initial_vector("(0/1/1/2)",vec));
    assert(!parse_initial_vector("(0/nan/1)",vec));
    assert(!parse_initial_vector("",vec));
    assert(resource_index(0x1000+3*0xac,0x1000,7,0xac)==3);
    assert(resource_index(0x1000+0x41a*4,0x1000,4,0x41a*4)==1);
    assert(resource_index(0x1001,0x1000,7,0xac)==-1);
    assert(resource_index(0x1000+7*0xac,0x1000,7,0xac)==-1);
    assert(resource_index(0,0x1000,7,0xac)==-1);
}
