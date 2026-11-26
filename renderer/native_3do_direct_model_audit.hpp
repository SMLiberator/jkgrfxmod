#pragma once
// Read-only prediction of JKGM's rdModel3_Draw / DrawHNode callback order.
// Layout verified against jkgm(5).exe SHA-256 afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f.
// 0x44623e-0x4462ba and 0x4462e0-0x44635d. No native draw globals.
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>

namespace jkgm {
struct direct_model_node {
    uint32_t mesh=0,node=0;
};
struct direct_model_plan {
    uint32_t geoset=0,geoset_address=0;
    std::vector<direct_model_node> nodes;
};
inline bool direct_model_span(uint64_t at,uint64_t size) {
    return at>=0x10000U && size && at+size<=0x80000000ULL;
}
template<class Read>
bool predict_direct_model_nodes(Read const &read,uint32_t rd,
                                direct_model_plan &out) {
    out={};
    uint32_t model=0,count=0,base=0,cuts=0,geo_count=0,meshes=0,mesh_count=0;
    int32_t geo=-1;
    auto u32=[&](uint64_t at,uint32_t &v) {
        return direct_model_span(at,4) && read(uintptr_t(at),&v,4);
    };
    if(!direct_model_span(rd,0x44) ||
       !u32(uint64_t(rd)+4,model) || !direct_model_span(model,0x60) ||
       !u32(uint64_t(model)+0x44,geo_count) ||
       !read(rd+0x30,&geo,4) || geo_count==0 || geo_count>4 ||
       (geo==-1 && !read(model+0x50,&geo,4)) ||
       geo<0 || uint32_t(geo)>=geo_count ||
       !u32(model+0x54,count) || !u32(model+0x58,base) ||
       !u32(rd+0x28,cuts) || count==0 || count>1024 ||
       !direct_model_span(base,uint64_t(count)*0xb4) ||
       !u32(model+0x24+uint32_t(geo)*8,mesh_count) ||
       !u32(model+0x28+uint32_t(geo)*8,meshes) ||
       mesh_count>4096 ||
       (mesh_count && !direct_model_span(meshes,uint64_t(mesh_count)*0x70)))
        return false;
    direct_model_plan plan{};
    plan.geoset=uint32_t(geo);
    plan.geoset_address=model+0x24+uint32_t(geo)*8;
    std::vector<uint8_t> seen(count,0),seen_indices(count,0);
    // Explicit stack avoids recursion on malformed hierarchy pointers.
    std::vector<uint32_t> pending{base};
    auto index_of=[&](uint32_t ptr,uint32_t &index) {
        if(ptr<base || (ptr-base)%0xb4!=0)return false;
        index=(ptr-base)/0xb4;
        return index<count;
    };
    while(!pending.empty()) {
        uint32_t const ptr=pending.back();
        pending.pop_back();
        uint32_t slot=0,id=0,mesh=0,n=0,child=0;
        if(!index_of(ptr,slot) || seen[slot] ||
           !u32(ptr+0x44,id) || id>=count || seen_indices[id] ||
           !u32(ptr+0x4c,mesh) || !u32(ptr+0x54,n) || n>count ||
           (n && !u32(ptr+0x58,child)))return false;
        seen[slot]=1;seen_indices[id]=1;
        if(mesh!=0xffffffffU) {
            if(mesh>=mesh_count)return false;
            plan.nodes.push_back({meshes+mesh*0x70,id});
        }
        // Native root is unconditional. Only child edges consult amputations.
        std::vector<uint32_t> children;
        for(uint32_t i=0;i<n;++i) {
            uint32_t child_slot=0,child_id=0,cut=0,next=0;
            if(!index_of(child,child_slot) || !u32(child+0x44,child_id) ||
               child_id>=count || !direct_model_span(cuts,uint64_t(count)*4) ||
               !u32(uint64_t(cuts)+child_id*4,cut) ||
               !u32(child+0x5c,next))return false;
            if(!cut)children.push_back(child);
            child=next;
        }
        if(pending.size()+children.size()>count)return false;
        for(auto it=children.rbegin();it!=children.rend();++it)
            pending.push_back(*it);
    }
    out=std::move(plan);
    return true;
}
struct direct_model_modes {int32_t geo=0,light=0,tex=0;};
inline direct_model_modes predict_direct_model_modes(
    direct_model_modes thing,direct_model_modes global,
    uint32_t options,float ambient) {
    return {std::min(thing.geo,global.geo),
            (options&2U) && ambient>=1.0f ? 0 : std::min(thing.light,global.light),
            std::min(thing.tex,global.tex)};
}
}
