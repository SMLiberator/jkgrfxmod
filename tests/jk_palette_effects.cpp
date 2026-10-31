#include "renderer/jk_palette_effects.hpp"
#include "renderer/thing_sector_tint.hpp"
#include <cstring>
#include <cstdint>
#include <cmath>
#include <cassert>
#include <array>
using namespace jkgm;
int main()
{
    std::array<jk_palette_request,32> req{};
    std::array<int32_t,4> enabled{{1,1,1,1}};
    auto p=accumulate_jk_palette_effects(req,0,enabled);
    assert(!p.active && p.fade==1);
    req[0].valid=1; req[0].filter[0]=1;
    req[0].tint[0]=0.25f;
    req[0].add[1]=32; req[0].fade=0.8f;
    req[1].valid=1; req[1].filter[2]=1;
    req[1].tint[0]=0.2f; req[1].add[1]=13; req[1].fade=0.6f;
    p=accumulate_jk_palette_effects(req,2,enabled);
    assert(p.active && p.filter[0] && !p.filter[1] && p.filter[2]);
    assert(p.tint[0]>0.4499f && p.tint[0]<0.4501f);
    assert(p.add[1]==45 && p.fade==0.6f);
    assert(!accumulate_jk_palette_effects(req,3,enabled).active);
    enabled={{0,0,0,0}};
    assert(!accumulate_jk_palette_effects(req,2,enabled).active);
    req[1].valid=0;
    enabled={{1,1,1,1}};
    p=accumulate_jk_palette_effects(req,1,enabled);
    assert(p.active && p.fade==0.8f);
    // OpenJKDF2 native layouts: SithThing.sector +0x5c,
    // SithCamera.sector +0x18, SithSector.tint +0x10.
    // Two Things sharing an rdModel3 may use different sectors.
    constexpr uintptr_t base=0x400000U;
    std::array<uint8_t,0x1000> bytes{};
    constexpr uint32_t thing_a=0x400100U,thing_b=0x400200U;
    constexpr uint32_t sector_a=0x400400U,sector_b=0x400500U;
    constexpr uint32_t camera=0x400600U;
    auto put=[&](uintptr_t addr,void const *src,size_t n) {
        assert(addr>=base && addr-base+n<=bytes.size());
        std::memcpy(bytes.data()+(addr-base),src,n);
    };
    auto put32=[&](uintptr_t addr,uint32_t v){put(addr,&v,4);};
    auto putf=[&](uintptr_t addr,float v){put(addr,&v,4);};
    put32(thing_a+0x5cU,sector_a);
    put32(thing_b+0x5cU,sector_b);
    put32(camera+0x18U,sector_a);
    putf(sector_a+0x10U,0.75f);
    putf(sector_b+0x14U,0.50f);
    auto read=[&](uintptr_t addr,void *dst,size_t n) {
        if(addr==0x0082F104U && n==4U) {
            std::memcpy(dst,&camera,4);
            return true;
        }
        if(addr<base || addr-base+n>bytes.size())return false;
        std::memcpy(dst,bytes.data()+(addr-base),n);
        return true;
    };
    std::array<float,3> tint_a{},tint_b{},camera_tint{};
    assert(read_jk_camera_sector_tint(read,camera_tint));
    assert(read_jk_thing_sector_tint(read,thing_a,tint_a));
    assert(read_jk_thing_sector_tint(read,thing_b,tint_b));
    auto delta_a=jk_thing_tint_delta(tint_a,camera_tint);
    auto delta_b=jk_thing_tint_delta(tint_b,camera_tint);
    assert(delta_a[0]==0.0f && delta_a[1]==0.0f);
    assert(delta_b[0]==-0.75f && delta_b[1]==0.5f);
    // A scripted/global tint remains unchanged when replacing only the
    // camera-sector portion with the Thing's own sector tint.
    float global_scripted=0.20f;
    float global_combined=global_scripted+camera_tint[0];
    assert(std::fabs(global_combined+delta_b[0]-global_scripted)<1e-6f);
    putf(sector_b+0x10U,0.35f); // live COG Sector Tint update
    assert(read_jk_thing_sector_tint(read,thing_b,tint_b));
    assert(std::fabs(jk_thing_tint_delta(tint_b,camera_tint)[0]+0.40f)<
           1e-6f);
    put32(thing_b+0x5cU,0U);
    assert(!read_jk_thing_sector_tint(read,thing_b,tint_b));
    return 0;
}
