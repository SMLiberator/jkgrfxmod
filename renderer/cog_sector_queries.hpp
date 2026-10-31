#pragma once
// Per-COG position-to-sector iteration; IDs are not assumed to be indices.
#include <array>
#include <cstdint>
#include <unordered_map>

namespace jkgm {
class cog_sector_queries {
    struct cursor {
        std::array<float,3> position{};
        int last=-1;
        int next_index=0;
    };
    uintptr_t world_=0;
    std::unordered_map<uintptr_t,cursor> queries_;
    template<class Match> static int advance(cursor& q,int count,Match match) {
        for(int i=q.next_index;i<count;++i) {
            int id=match(i,q.position);
            if(id>=0) {q.last=id;q.next_index=i+1;return id;}
        }
        q.last=-1;q.next_index=count;return -1;
    }
public:
    void set_world(uintptr_t world) {
        if(world_!=world){world_=world;queries_.clear();}
    }
    void clear(){set_world(0);}
    template<class Match>
    int first(uintptr_t cog,std::array<float,3> pos,int count,Match match) {
        if(!world_||!cog||count<=0)return -1;
        auto& q=queries_[cog];
        q.position=pos;q.last=-1;q.next_index=0;
        return advance(q,count,match);
    }
    template<class Match>
    int next(uintptr_t cog,int previous,int count,Match match) {
        if(!world_||!cog||previous<0||count<=0)return -1;
        auto it=queries_.find(cog);
        if(it==queries_.end()||it->second.last!=previous)return -1;
        return advance(it->second,count,match);
    }
};

// Lookup by native sector ID. The common ID==index case is O(1);
// sparse IDs use a cached mapping built once per level.
class cog_sector_id_lookup {
    uintptr_t world_=0, base_=0;
    int count_=0;
    bool built_=false;
    std::unordered_map<int,int> ids_;
public:
    template<class IdAt>
    int find(uintptr_t world,uintptr_t base,int count,int id,IdAt id_at) {
        if(!world || !base || count<=0 || id<0)return -1;
        if(world_!=world || base_!=base || count_!=count) {
            world_=world; base_=base; count_=count;
            ids_.clear(); built_=false;
        }
        if(id<count && id_at(id)==id)return id;
        if(!built_) {
            for(int i=0;i<count;++i) {
                int n=id_at(i);
                if(n>=0)ids_.emplace(n,i);
            }
            built_=true;
        }
        auto it=ids_.find(id);
        return it==ids_.end()?-1:it->second;
    }
};
}
