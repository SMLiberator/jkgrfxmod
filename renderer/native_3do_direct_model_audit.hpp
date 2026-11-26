#pragma once
// Read-only prediction of JKGM's rdModel3_Draw / DrawHNode callback order.
// Layout verified against jkgm(5).exe SHA-256 afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f.
// 0x44623e-0x4462ba and 0x4462e0-0x44635d. No native draw globals.
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>
#include <unordered_map>
#include <cstring>

namespace jkgm {
struct direct_model_node {
    uint32_t mesh=0,node=0,depth=0;
};
struct direct_model_plan {
    uint32_t geoset=0,geoset_address=0,node_count=0;
    std::vector<direct_model_node> nodes;
};
inline bool direct_model_span(uint64_t at,uint64_t size) {
    return at>=0x10000U && size && at+size<=0x80000000ULL;
}
// Capture the structural identity separately so cache hits need no hierarchy walk.
struct direct_model_identity {
    uint32_t model=0,count=0,base=0,geo=0,meshes=0,mesh_count=0;
    bool operator==(direct_model_identity const &o) const {
        return model==o.model && count==o.count && base==o.base &&
               geo==o.geo && meshes==o.meshes && mesh_count==o.mesh_count;
    }
};
template<class Read>
bool read_direct_model_identity(Read const &read,uint32_t rd,
                                direct_model_identity &out) {
    out={};
    uint32_t model=0,count=0,base=0,geo_count=0,meshes=0,mesh_count=0;
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
       count==0 || count>1024 ||
       !direct_model_span(base,uint64_t(count)*0xb4) ||
       !u32(model+0x24+uint32_t(geo)*8,mesh_count) ||
       !u32(model+0x28+uint32_t(geo)*8,meshes) ||
       mesh_count>4096 ||
       (mesh_count && !direct_model_span(meshes,uint64_t(mesh_count)*0x70)))
        return false;
    out={model,count,base,uint32_t(geo),meshes,mesh_count};
    return true;
}
template<class Read>
bool build_direct_model_plan(Read const &read,direct_model_identity const &key,
                             uint32_t cuts,direct_model_plan &out,
                             bool full_topology=false) {
    out={};
    auto const count=key.count,base=key.base,meshes=key.meshes,
        mesh_count=key.mesh_count;
    auto u32=[&](uint64_t at,uint32_t &v) {
        return direct_model_span(at,4) && read(uintptr_t(at),&v,4);
    };
    direct_model_plan plan{};
    plan.geoset=key.geo;
    plan.geoset_address=key.model+0x24+key.geo*8;
    plan.node_count=count;
    std::vector<uint8_t> seen(count,0),seen_indices(count,0);
    // Explicit stack avoids recursion on malformed hierarchy pointers.
    std::vector<std::pair<uint32_t,uint32_t>> pending{{base,0}};
    auto index_of=[&](uint32_t ptr,uint32_t &index) {
        if(ptr<base || (ptr-base)%0xb4!=0)return false;
        index=(ptr-base)/0xb4;
        return index<count;
    };
    while(!pending.empty()) {
        auto const entry=pending.back();
        uint32_t const ptr=entry.first,depth=entry.second;
        pending.pop_back();
        uint32_t slot=0,id=0,mesh=0,n=0,child=0;
        if(!index_of(ptr,slot) || seen[slot] ||
           !u32(ptr+0x44,id) || id>=count || seen_indices[id] ||
           !u32(ptr+0x4c,mesh) || !u32(ptr+0x54,n) || n>count ||
           (n && !u32(ptr+0x58,child)))return false;
        seen[slot]=1;seen_indices[id]=1;
        if(mesh!=0xffffffffU) {
            if(mesh>=mesh_count)return false;
            plan.nodes.push_back({meshes+mesh*0x70,id,depth});
        } else if(full_topology)plan.nodes.push_back({0,id,depth});
        // Native root is unconditional. Only child edges consult amputations.
        std::vector<uint32_t> children;
        for(uint32_t i=0;i<n;++i) {
            uint32_t child_slot=0,child_id=0,cut=0,next=0;
            if(!index_of(child,child_slot) || !u32(child+0x44,child_id) ||
               child_id>=count ||
               (!full_topology && (!direct_model_span(cuts,uint64_t(count)*4) ||
                !u32(uint64_t(cuts)+child_id*4,cut))) ||
               !u32(child+0x5c,next))return false;
            if(!cut)children.push_back(child);
            child=next;
        }
        if(pending.size()+children.size()>count)return false;
        for(auto it=children.rbegin();it!=children.rend();++it)
            pending.emplace_back(*it,depth+1);
    }
    out=std::move(plan);
    return true;
}
template<class Read>
bool predict_direct_model_nodes(Read const &read,uint32_t rd,direct_model_plan &out) {
    out={};
    direct_model_identity key{};
    uint32_t cuts=0;
    return read_direct_model_identity(read,rd,key) && read(rd+0x28,&cuts,4) &&
           build_direct_model_plan(read,key,cuts,out);
}
// Cache a complete DFS with depth, not the first Thing's amputated view.
// Every draw uses its own current amputation flags. Reuse only while native
// model revision tracking is reliable; otherwise restrict to one frame.
class direct_model_topology_cache {
    struct entry {direct_model_identity key;direct_model_plan plan;};
    std::unordered_map<uint32_t,entry> entries_;
    uint64_t revision_=0,frame_=0;
    bool reliable_=false;
public:
    void clear() {entries_.clear();revision_=frame_=0;reliable_=false;}
    void prepare(uint64_t revision,uint64_t frame,bool reliable) {
        if(revision_!=revision || reliable_!=reliable ||
           (!reliable && frame_!=frame))entries_.clear();
        revision_=revision;frame_=frame;reliable_=reliable;
    }
    template<class Read>
    direct_model_plan const *get(Read const &read,uint32_t rd,bool &hit) {
        hit=false;
        direct_model_identity key{};
        if(!read_direct_model_identity(read,rd,key))return nullptr;
        uint32_t const id=key.model+0x24+key.geo*8;
        auto it=entries_.find(id);
        if(it!=entries_.end() && it->second.key==key) {
            hit=true;return &it->second.plan;
        }
        direct_model_plan plan{};
        if(!build_direct_model_plan(read,key,0,plan,true))return nullptr;
        if(entries_.size()>=4096U)entries_.clear();
        auto &fresh=entries_[id];
        fresh={key,std::move(plan)};
        return &fresh.plan;
    }
};
// The root is always visited; skipping a cut node skips its entire subtree,
// including hierarchy nodes that have no mesh. Callback may fail a transaction.
template<class Visit>
bool visit_direct_model_meshes(direct_model_plan const &plan,
                               uint32_t const *cuts,Visit const &visit) {
    uint32_t cut_depth=0;
    for(auto const &node:plan.nodes) {
        if(cut_depth && node.depth>cut_depth)continue;
        cut_depth=0;
        if(node.depth && cuts[node.node]) {cut_depth=node.depth;continue;}
        if(node.mesh && !visit(node))return false;
    }
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
