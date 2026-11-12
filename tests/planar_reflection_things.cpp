#include "renderer/planar_reflection_things.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_map>

int main()
{
    using namespace jkgm;
    std::unordered_map<uintptr_t,uint8_t> bytes;
    auto put=[&](uintptr_t address,auto const &v) {
        uint8_t src[sizeof(v)];
        std::memcpy(src,&v,sizeof(v));
        for(size_t i=0;i<sizeof(v);++i)bytes[address+i]=src[i];
    };
    auto get=[&](uintptr_t address,void *dst,size_t n)->bool {
        auto *p=static_cast<uint8_t*>(dst);
        for(size_t i=0;i<n;++i) {
            auto it=bytes.find(address+i);
            if(it==bytes.end())return false;
            p[i]=it->second;
        }
        return true;
    };
    constexpr uintptr_t world=0x20000U,sector0=0x30000U;
    constexpr uintptr_t sector1=sector0+0x98U;
    constexpr uintptr_t a=0x40000U,b=0x50000U,c=0x60000U;
    put(world+0x4cU,uint32_t(2));
    put(world+0x50U,uint32_t(sector0));
    put(world+0xbcU,uint32_t(5));
    put(world+0xc0U,int32_t(3));
    put(world+0xc4U,uint32_t(a));
    put(sector0+0x30U,uint32_t(a));
    put(sector1+0x30U,uint32_t(c));
    // Two camera-independent models, one in each sector. A third Thing is
    // disabled; the normal main-camera sector walk may not see sector1.
    auto thing=[&](uintptr_t address,uint32_t idx,uint32_t sector,
                   uint32_t next,uint32_t flags,uint32_t kind) {
        put(address+0x00U,flags);
        put(address+0x04U,idx);
        put(address+0x0cU,kind);
        put(address+0x5cU,sector);
        put(address+0x60U,next);
        put(address+0x74U,uint32_t(idx+100));
        put(address+0xc4U,uint32_t(1));
        put(address+0xc8U,uint32_t(0x70000U+idx*0x100U));
        put(address+0xf8U,int32_t(4)); // rdThing.curGeoMode
        float p[3]={float(idx),float(idx+1U),2.5f};
        for(size_t i=0;i<3;++i)put(address+0xb8U+i*4U,p[i]);
    };
    thing(a,0,sector0,b,0,2);
    thing(b,1,sector0,0,0x80000U,2);
    thing(c,3,sector1,0,0,2);
    planar_reflection_thing_scan scan{};
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.error==planar_reflection_thing_error::none);
    assert(scan.sectors==2 && scan.linked_things==3);
    assert(scan.excluded_flags==1 && scan.models.size()==2);
    assert(scan.models[0].thing==a && scan.models[1].thing==c);
    assert(scan.models[1].index==3 && scan.models[1].signature==103);
    assert(scan.models[1].position[0]==3.0f);
    assert(scan.models[0].current_geometry_mode==4U);

    // Explicit GeoMode hiding is not main-camera frustum culling.
    // Offscreen reflection discovery must honor it independently.
    put(a+0xf8U,int32_t(0));
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.models.size()==1 && scan.excluded_geo_mode==1);
    assert(scan.models[0].thing==c);
    put(a+0xf8U,int32_t(2));
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.models.size()==1 && scan.excluded_geo_mode==1);
    put(a+0xf8U,int32_t(3));
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.models.size()==2 && scan.models[0].current_geometry_mode==3U);
    put(a+0xf8U,int32_t(7)); // JKGM clamps unsupported high modes to 4.
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.models.size()==2 && scan.models[0].current_geometry_mode==4U);
    put(a+0xf8U,int32_t(4));

    // Both explicit Thing hide bits must exclude models from reflections.
    put(a+0x00U,uint32_t(0x10U));
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.models.size()==1 && scan.excluded_flags==2U);
    put(a+0x00U,uint32_t(0x80000U));
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.models.size()==1 && scan.excluded_flags==2U);
    put(a+0x00U,uint32_t(0));

    // A cycle must fail closed and clear all partial output.
    put(c+0x60U,uint32_t(c));
    assert(!collect_planar_reflection_things(get,world,scan));
    assert(scan.models.empty());
    assert(scan.error==planar_reflection_thing_error::invalid_link);
    put(c+0x60U,uint32_t(0));

    // The malformed owner is not emitted as a renderable 3DO.
    put(c+0x5cU,uint32_t(sector0));
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.models.size()==1 && scan.non_models==1);
    put(c+0x5cU,uint32_t(sector1));

    // Inconsistent table count or invalid pointers cannot walk memory.
    put(world+0xbcU,uint32_t(2));
    assert(!collect_planar_reflection_things(get,world,scan));
    assert(scan.error==planar_reflection_thing_error::invalid_world ||
           scan.error==planar_reflection_thing_error::invalid_thing ||
           scan.error==planar_reflection_thing_error::invalid_link);
    put(world+0xbcU,uint32_t(5));
    put(sector0+0x30U,uint32_t(0x42));
    assert(!collect_planar_reflection_things(get,world,scan));
    assert(scan.error==planar_reflection_thing_error::invalid_link);
    put(sector0+0x30U,uint32_t(a));

    // Non-model rdThing (sprite), destroyed and camera Things are not 3DOs.
    put(a+0xc4U,uint32_t(2));
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.models.size()==1 && scan.non_models==1);
    put(a+0xc4U,uint32_t(1));
    put(a+0x0cU,uint32_t(1));
    assert(collect_planar_reflection_things(get,world,scan));
    assert(scan.models.size()==1 && scan.excluded_flags==2);
    return 0;
}
