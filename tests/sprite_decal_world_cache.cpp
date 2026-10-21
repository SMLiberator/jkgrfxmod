#include "renderer/sprite_decal_world_cache.hpp"
#include "renderer/sprite_decal_pose_cache.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
using namespace jkgm;

int main(){
    constexpr uintptr_t base=0x10000000U;
    std::vector<unsigned char> mem(0x3000);
    auto put=[&](uintptr_t address,void const *src,size_t n){
        assert(address>=base && address-base+n<=mem.size());
        std::memcpy(mem.data()+address-base,src,n);
    };
    auto u32=[&](uintptr_t address,uint32_t value){
        put(address,&value,4);
    };
    auto vec=[&](uintptr_t address,decal_vec3 v){
        put(address,v.data(),12);
    };
    int reads=0;
    decal_native_read reader=[&](uintptr_t a,void *out,size_t n){
        ++reads;
        if(a<base || a-base>mem.size() || n>mem.size()-(a-base))
            return false;
        std::memcpy(out,mem.data()+a-base,n);
        return true;
    };
    uintptr_t const world=base+0x10;
    uintptr_t const vertices=base+0x200;
    uintptr_t const surfaces=base+0x800;
    uintptr_t const indices=base+0x1100;
    u32(world+0x88,8);
    u32(world+0x8c,uint32_t(vertices));
    u32(world+0xa8,4);
    u32(world+0xac,uint32_t(surfaces));
    for(int j=0;j<8;++j){
        float const x=j<4?-0.25f:0.25f;
        float const y=0.1f;
        float const z=j%4<2?-0.25f:0.25f;
        vec(vertices+j*12,{{x,y,z}});
    }
    for(uint32_t i=0;i<4;++i){
        auto const addr=surfaces+i*0x5c;
        u32(addr+0x1c,4);
        u32(addr+0x28,4);
        u32(addr+0x2c,uint32_t(indices+i*16));
        u32(addr+0x34,uint32_t(base+0x1800));
        vec(addr+0x48,{{0,-1,0}});
        for(uint32_t j=0;j<4;++j)
            u32(indices+i*16+j*4,(i%2)*4+j);
    }
    // A portal and sky should not be cached as receivers.
    u32(surfaces+2*0x5c+0x0c,uint32_t(base+0x1800));
    u32(surfaces+3*0x5c+0x10,0x200);
    decal_world_geometry index{};
    assert(index.load(reader,world));
    assert(index.matches(reader,world));
    assert(index.surface_count==4 && index.receivers.size()==2);
    decal_projector p{};
    const int after_load=reads;
    auto result=project_cached_decal_world(index,p);
    assert(reads==after_load); // Cached queries do not read SithWorld.
    assert(result.valid_world && result.receivers==2);
    assert(result.triangles.size()==4);
    p.origin[0]=10;
    auto far=project_cached_decal_world(index,p);
    assert(reads==after_load);
    assert(far.valid_world && far.receivers==0);
    assert(far.triangles.empty());
    // In place vertex source edits no longer trigger silent repeated reads.
    vec(vertices,{{100,100,100}});
    p.origin[0]=0;
    assert(project_cached_decal_world(index,p).triangles.size()==4);
    // New world surfaces invalidate the model cache.
    u32(world+0xa8,3);
    assert(!index.matches(reader,world));

    std::array<float,12> const identity{{1,0,0,0,1,0,0,0,1,0,0,0}};
    auto moved=identity;
    moved[9]=0.012f;
    moved[0]=0.7f; // rotational physics jitter irrelevant after impact.
    assert(sprite_decal_pose_stable(identity,moved,true));
    assert(!sprite_decal_pose_stable(identity,moved,false));
    moved[9]=0.1f;
    assert(!sprite_decal_pose_stable(identity,moved,true));
    std::array<float,3> const up{{0,0,1}};
    assert(sprite_decal_normals_stable(up,{{0.001f,0,1}}));
    assert(!sprite_decal_normals_stable(up,{{0.2f,0,1}}));
}
