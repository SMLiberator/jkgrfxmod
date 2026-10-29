#pragma once
// Per-COG rotating handles for the last five successful native raycasts.
// Pure container: does not depend on the game executable or Windows APIs.
#include <array>
#include <cstdint>
#include <unordered_map>

namespace jkgm {
struct cog_raycast_hit {
    std::array<float,3> position{};
    std::array<float,3> normal{};
    float distance=0.0f;
    int thing_index=-1;
    int surface_index=-1;
    int sector_index=-1;
    uintptr_t thing_address=0;
    uint32_t thing_signature=0;
};

class cog_raycast_results {
public:
    static constexpr int slot_count=5;
    void set_world(uintptr_t world) {
        if(world_!=world) {
            hits_.clear();
            world_=world;
        }
    }
    void clear() {
        hits_.clear();
        world_=0;
    }
    // Misses never call add(), so they do not advance the rotating handle.
    int add(uintptr_t cog,cog_raycast_hit const &hit) {
        if(!cog || !world_)return -1;
        auto &ring=hits_[cog];
        int const id=ring.next;
        ring.data[id]=hit;
        ring.valid[id]=true;
        ring.next=(id+1)%slot_count;
        return id;
    }
    cog_raycast_hit const* get(uintptr_t cog,int id) const {
        if(!cog || !world_ || id<0 || id>=slot_count)return nullptr;
        auto const it=hits_.find(cog);
        if(it==hits_.end() || !it->second.valid[id])return nullptr;
        return &it->second.data[id];
    }
private:
    struct ring {
        std::array<cog_raycast_hit,slot_count> data{};
        std::array<bool,slot_count> valid{};
        int next=0;
    };
    uintptr_t world_=0;
    std::unordered_map<uintptr_t,ring> hits_;
};
}
