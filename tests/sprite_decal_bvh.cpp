#include "renderer/sprite_decal_bvh.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
using namespace jkgm;

struct surface {
    decal_vec3 bounds_min{},bounds_max{};
};

int main() {
    std::vector<surface> world;
    // Deliberately insert surfaces in reverse spatial order: broadphase
    // traversal must still return the SAME native face order as a full scan.
    for(int z=31;z>=0;--z)
        for(int x=31;x>=0;--x)
            world.push_back({{{float(x),0.0f,float(z)}},
                             {{float(x)+0.75f,0.0f,float(z)+0.75f}}});
    decal_receiver_bvh<surface> bvh;
    bvh.build(world);
    assert(bvh.node_count()>1);
    std::vector<uint32_t> found,expected;
    for(int z=-1;z<=32;++z)
        for(int x=-1;x<=32;++x) {
            decal_vec3 const lo{{x+0.15f,-0.1f,z+0.15f}};
            decal_vec3 const hi{{x+0.45f,0.1f,z+0.45f}};
            bvh.query(world,lo,hi,found);
            expected.clear();
            for(uint32_t n=0;n<world.size();++n)
                if(decal_aabbs_overlap(lo,hi,
                       world[n].bounds_min,world[n].bounds_max))
                    expected.push_back(n);
            assert(found==expected);
        }
    // Touching a receiver bound must NOT be discarded by the BVH.
    bvh.query(world,{{0.75f,0,0.75f}},{{0.75f,0,0.75f}},found);
    assert(found.size()==1);
    // Manual and empty worlds remain valid fallbacks.
    bvh.clear();
    bvh.query(world,{{0.15f,-0.1f,0.15f}},{{0.45f,0.1f,0.45f}},found);
    assert(found.size()==1 && found.front()==world.size()-1);
    world.clear();
    bvh.build(world);
    bvh.query(world,{{0,0,0}},{{1,1,1}},found);
    assert(found.empty());
}
