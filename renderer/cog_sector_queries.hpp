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
}
