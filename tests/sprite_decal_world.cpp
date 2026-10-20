#include "renderer/sprite_decal_world.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
using namespace jkgm;
int main() {
    constexpr uintptr_t base=0x10000000U;
    std::vector<unsigned char> memory(0x2000);
    auto set32=[&](uintptr_t addr,uint32_t value) {
        std::memcpy(&memory[addr-base],&value,4);
    };
    auto set3=[&](uintptr_t addr,decal_vec3 const &value) {
        std::memcpy(&memory[addr-base],value.data(),12);
    };
    decal_native_read reader=[&](uintptr_t a,void *out,size_t count) {
        if(a<base || a-base>memory.size() ||
           count>memory.size()-(a-base))return false;
        std::memcpy(out,memory.data()+(a-base),count);
        return true;
    };
    uintptr_t const world=base+0x10;
    uintptr_t const vertices=base+0x200;
    uintptr_t const surfaces=base+0x800;
    uintptr_t const idx=base+0x1100;
    set32(world+0x88,8);
    set32(world+0x8c,uint32_t(vertices));
    set32(world+0xa8,4);
    set32(world+0xac,uint32_t(surfaces));
    // Two adjacent halves of an opaque wall, with shared edge x=0.
    for(int j=0;j<8;++j) {
        float const x=(j<4)?(j%4<2?-1.0f:0.0f)
                             :(j%4<2?0.0f:1.0f);
        float const z=((j%4)==0 || (j%4)==3)?-1.0f:1.0f;
        set3(vertices+uintptr_t(j)*12U,{{x,0.1f,z}});
    }
    for(int surf=0;surf<4;++surf) {
        uintptr_t const a=surfaces+uintptr_t(surf)*0x5c;
        set32(a+0x1c,4); // face geomode textured
        set32(a+0x28,4);
        set32(a+0x2c,uint32_t(idx+surf*16));
        set32(a+0x34,uint32_t(base+0x1800));
        set3(a+0x48,{{0,-1,0}});
        for(int k=0;k<4;++k)
            set32(idx+surf*16+k*4, uint32_t((surf%2)*4+k));
    }
    // Third wall is a portal: must not receive projected paint.
    set32(surfaces+2*0x5c+0x0c,uint32_t(base+0x1500));
    // Fourth wall is sky: must not receive projected paint.
    set32(surfaces+3*0x5c+0x10,0x200U);
    decal_projector p{};
    auto mesh=project_decal_world_surfaces(reader,world,p);
    assert(mesh.valid_world);
    assert(mesh.surfaces_seen==4);
    assert(mesh.receivers==2);
    assert(mesh.invalid_surfaces==0);
    assert(mesh.triangles.size()==4);
    for(auto const &tri:mesh.triangles) {
        assert(tri.surface_index<2);
        for(auto const &v:tri.vertices) {
            auto const uv=projected_decal_uv(v,p);
            assert(uv[0]>=-0.001f&&uv[0]<=1.001f);
            assert(uv[1]>=-0.001f&&uv[1]<=1.001f);
            assert(v.depth>=0&&v.depth<=p.depth);
        }
    }
    // Strict cap cannot drop half of an emitted surface's triangles.
    auto capped=project_decal_world_surfaces(reader,world,p,3);
    assert(capped.valid_world && capped.truncated);
    assert(capped.triangles.size()==2);
    // A malformed surface index never dereferences arbitrary game memory.
    set32(idx+0*16+0*4,9999999U);
    auto invalid=project_decal_world_surfaces(reader,world,p);
    assert(invalid.invalid_surfaces==1 && invalid.triangles.size()==2);
    // A nonsensical world count fails before scanning.
    set32(world+0xa8,9000000U);
    auto rejected=project_decal_world_surfaces(reader,world,p);
    assert(!rejected.valid_world && rejected.triangles.empty());
    // A failed address read is handled without crashing.
    assert(!project_decal_world_surfaces(reader,base+0x5000,p).valid_world);
}
