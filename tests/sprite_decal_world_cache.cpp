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
    u32(world+0xa8,5);
    u32(world+0xac,uint32_t(surfaces));
    uintptr_t const uv_pool=base+0x2100;
    uintptr_t const uv_indices=base+0x2200;
    u32(world+0xa0,4);
    u32(world+0xa4,uint32_t(uv_pool));
    for(uint32_t n=0;n<4;++n) {
        std::array<float,2> uv{{float((n==1||n==2)?64:0),
                                 float((n>=2)?32:0)}};
        put(uv_pool+n*8,uv.data(),8);
    }
    for(int j=0;j<8;++j){
        int const corner=j%4;
        float const x=j<4?(corner<2?-0.25f:0.0f):
                              (corner<2?0.0f:0.25f);
        float const y=0.1f;
        float const z=(corner==0 || corner==3)?-0.25f:0.25f;
        vec(vertices+j*12,{{x,y,z}});
    }
    for(uint32_t i=0;i<5;++i){
        auto const addr=surfaces+i*0x5c;
        u32(addr+0x1c,4);
        u32(addr+0x28,4);
        u32(addr+0x2c,uint32_t(indices+i*16));
        u32(addr+0x34,uint32_t(base+0x1800));
        u32(addr+0x30,uint32_t(uv_indices+i*16));
        float const offset[2]={float(i)*4.0f,2.0f};
        put(addr+0x3c,offset,8);
        vec(addr+0x48,{{0,-1,0}});
        for(uint32_t j=0;j<4;++j) {
            u32(indices+i*16+j*4,(i%2)*4+j);
            u32(uv_indices+i*16+j*4,j);
        }
    }
    // Passable portal 0x7 must be excluded; solid grate 0x5 included.
    u32(surfaces+2*0x5c+0x0c,uint32_t(base+0x1900));
    u32(base+0x1900,0x7);
    u32(surfaces+3*0x5c+0x10,0x200);
    u32(surfaces+4*0x5c+0x0c,uint32_t(base+0x1910));
    u32(base+0x1910,0x5);
    decal_world_geometry index{};
    assert(index.load(reader,world));
    assert(index.matches(reader,world));
    assert(index.surface_count==5 && index.receivers.size()==3);
    assert(index.receivers[0].has_shadow_uv);
    assert(index.receivers[0].shadow_uv.size()==4);
    assert(index.receivers[0].material==base+0x1800);
    assert(std::fabs(index.receivers[0].shadow_uv[1][0]-64.0f)<0.0001f);
    assert(std::fabs(index.receivers[0].shadow_uv[0][1]-2.0f)<0.0001f);
    assert(index.receivers[2].solid_adjoin);

    assert(index.acceleration.node_count()>0);
    decal_projector p{};
    const int after_load=reads;
    auto result=project_cached_decal_world(index,p);
    assert(reads==after_load); // Cached queries do not read SithWorld.
    assert(result.valid_world && result.receivers==3);
    assert(result.broadphase_candidates==3);
    assert(result.triangles.size()==6);
    p.origin[0]=10;
    auto far=project_cached_decal_world(index,p);
    assert(reads==after_load);
    assert(far.valid_world && far.receivers==0);
    assert(far.broadphase_candidates==0);
    assert(far.triangles.empty());
    // In place vertex source edits no longer trigger silent repeated reads.
    vec(vertices,{{100,100,100}});
    p.origin[0]=0;
    assert(project_cached_decal_world(index,p).triangles.size()==6);
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
