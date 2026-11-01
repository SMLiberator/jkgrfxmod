#include "renderer/jk_palette_effects.hpp"
#include "renderer/thing_sector_tint.hpp"
#include "renderer/native_sector_palette_bridge.hpp"
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
    // The merged jkgm.exe keeps SithCamera objects in a verified array,
    // not at OpenJKDF2's legacy sithCamera_g_pCurCamera symbol.
    // Two different slots verify the camera index is used at runtime.
    int32_t camera_index=0;
    constexpr uintptr_t camera_array=0x008EC2C0U;
    constexpr uintptr_t camera_index_addr=0x008EE4E8U;
    constexpr uintptr_t camera_stride=0x4E0U;
    auto read=[&](uintptr_t addr,void *dst,size_t n) {
        if(addr==camera_index_addr && n==sizeof(camera_index)) {
            std::memcpy(dst,&camera_index,sizeof(camera_index));
            return true;
        }
        if(addr==camera_array+uintptr_t(camera_index)*camera_stride+0x18U &&
           n==4U) {
            auto const owning_sector=camera_index==0?sector_a:sector_b;
            std::memcpy(dst,&owning_sector,4);
            return true;
        }
        if(addr<base || addr-base+n>bytes.size())return false;
        std::memcpy(dst,bytes.data()+(addr-base),n);
        return true;
    };
    std::array<float,3> tint_a{},tint_b{},camera_tint{};
    assert(read_jk_camera_sector_tint(read,camera_tint));
    assert(camera_tint[0]==0.75f && camera_tint[1]==0.0f);
    assert(read_jk_thing_sector_tint(read,thing_a,tint_a));
    assert(read_jk_thing_sector_tint(read,thing_b,tint_b));
    auto delta_a=jk_thing_tint_delta(tint_a,camera_tint);
    auto delta_b=jk_thing_tint_delta(tint_b,camera_tint);
    assert(delta_a[0]==0.0f && delta_a[1]==0.0f);
    assert(delta_b[0]==-0.75f && delta_b[1]==0.5f);
    camera_index=1;
    assert(read_jk_camera_sector_tint(read,camera_tint));
    assert(camera_tint[0]==0.0f && camera_tint[1]==0.5f);
    camera_index=7; // out of bounds must fail closed
    assert(!read_jk_camera_sector_tint(read,camera_tint));
    camera_index=0;
    assert(read_jk_camera_sector_tint(read,camera_tint));
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

    // A native rdSetColorEffects pointer targets stdPalEffects_state.effect,
    // 0x14 into the state. The request pool follows at +0x60. Test
    // script effects and sector requests coexisting without hard-coded
    // absolute jk.exe addresses.
    constexpr uintptr_t native_state=base+0x700U;
    constexpr uintptr_t native_effect=native_state+0x14U;
    constexpr uintptr_t native_pool=native_state+0x60U;
    jk_palette_request dynamic{};
    dynamic.valid=1;
    dynamic.id=1;
    dynamic.tint[0]=0.20f;
    dynamic.add[2]=72;
    dynamic.fade=0.90f;
    jk_palette_request sector{};
    sector.valid=1;
    sector.id=2;
    sector.tint[0]=0.75f;
    sector.tint[1]=0.10f;
    jk_palette_request script{};
    script.valid=1;
    script.id=1;
    script.filter[1]=1;
    script.tint[0]=0.15f;
    script.add[0]=17;
    script.fade=0.80f;
    put(native_pool,&dynamic,sizeof(dynamic));
    put(native_pool+sizeof(dynamic),&sector,sizeof(sector));
    put(native_pool+2*sizeof(dynamic),&script,sizeof(script));
    auto snap=probe_native_sector_palette(read,native_effect);
    assert(snap.layout_valid && snap.sector_request_active);
    assert(snap.sector_slot==1U);
    std::array<jk_palette_request,32> full{};
    full[0]=dynamic;full[1]=sector;full[2]=script;
    auto combined=accumulate_jk_palette_effects(
        full,3U,std::array<int32_t,4>{{1,1,1,1}});
    auto remainder=without_native_sector_tint(combined,snap);
    assert(std::fabs(remainder.tint[0]-0.35f)<0.00001f);
    assert(std::fabs(remainder.tint[1])<0.00001f);
    assert(remainder.filter[1] && remainder.add[0]==17);
    assert(remainder.add[2]==72 && remainder.fade==0.80f);
    // A COG FreeColorEffect(1) invalidates exactly the allocated sector
    // request; the engine must not apply local sectors while it is free.
    sector.valid=0;
    put(native_pool+sizeof(dynamic),&sector,sizeof(sector));
    snap=probe_native_sector_palette(read,native_effect);
    assert(snap.layout_valid && !snap.sector_request_active);
    // Reusing the slot for an ordinary scripted request is NOT sector tint.
    sector.valid=1;sector.id=1;
    put(native_pool+sizeof(dynamic),&sector,sizeof(sector));
    snap=probe_native_sector_palette(read,native_effect);
    assert(snap.layout_valid && !snap.sector_request_active);
    return 0;
}
