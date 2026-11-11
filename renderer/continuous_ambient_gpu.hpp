#pragma once
// Stage B0: GL 3.3-compatible sparse voxel lookup transport.
// The CPU field owns topology; this immutable key table mirrors a
// usamplerBuffer(GL_RGBA32UI) and a samplerBuffer(GL_RG32F).
// No live OpenGL calls here. The renderer can upload these exact arrays
// and sample them by the GLSL 3.30 hash function in the next stage.
#include "continuous_ambient.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace jkgm {
class continuous_ambient_gpu_table {
public:
    using key=std::array<uint32_t,4>;
    using value=std::array<float,2>; // authored, shared-live or -1 if ordinary
    static constexpr uint32_t empty_sector=0xffffffffU;
    static constexpr uint32_t max_probe_budget=32U;
private:
    struct entry {
        key id{};
        float authored=0.0f;
        std::vector<uint32_t> shared_owners;
    };
    std::vector<key> keys_;
    std::vector<value> values_;
    std::vector<std::vector<uint32_t>> owners_;
    uint32_t mask_=0;
    uint32_t longest_probe_=0;
    size_t count_=0;
    float spacing_=1.0f;
    bool ready_=false;
    // Exact same wrapping 32-bit unsigned math is available in GLSL 330.
    static uint32_t hash(key const &k) {
        uint32_t h=2166136261U;
        for(uint32_t v:k)h=(h^v)*16777619U;
        h^=h>>16U;h*=0x7feb352dU;
        h^=h>>15U;h*=0x846ca68bU;
        h^=h>>16U;
        return h;
    }
    static uint32_t next_capacity(size_t count) {
        if(count>1000000U)return 0U;
        uint32_t capacity=16U;
        while(size_t(capacity)<count*2U) {
            if(capacity>=4194304U)return 0U;
            capacity*=2U;
        }
        return capacity;
    }
    bool fill(std::vector<entry> const &sorted,uint32_t capacity) {
        keys_.assign(capacity,key{{empty_sector,0U,0U,0U}});
        values_.assign(capacity,value{{0.0f,-1.0f}});
        owners_.clear();owners_.resize(capacity);
        mask_=capacity-1U;longest_probe_=0U;
        for(auto const &e:sorted) {
            uint32_t slot=hash(e.id)&mask_;
            uint32_t probes=0U;
            while(keys_[slot][0]!=empty_sector) {
                slot=(slot+1U)&mask_;
                if(++probes>=max_probe_budget)return false;
            }
            keys_[slot]=e.id;
            values_[slot][0]=e.authored;
            values_[slot][1]=e.shared_owners.empty()?-1.0f:e.authored;
            owners_[slot]=e.shared_owners;
            longest_probe_=std::max(longest_probe_,probes+1U);
        }
        return true;
    }
public:
    void reset() {
        keys_.clear();values_.clear();owners_.clear();
        mask_=longest_probe_=0U;count_=0U;spacing_=1.0f;ready_=false;
    }
    bool build(continuous_ambient_field const &field) {
        reset();
        if(!field.ready() || !std::isfinite(field.spacing()) ||
           field.spacing()<=0.0f)return false;
        spacing_=field.spacing();
        std::vector<entry> entries;
        entries.reserve(field.node_count());
        bool valid=true;
        field.for_each_node([&](uint32_t sector,
                continuous_ambient_field::coord const &pos,float light,
                std::vector<uint32_t> const *owners) {
            if(sector==empty_sector || !std::isfinite(light)) {
                valid=false;return;
            }
            entry e{};
            e.id={{sector,uint32_t(pos[0]),uint32_t(pos[1]),
                   uint32_t(pos[2])}};
            e.authored=light;
            if(owners)e.shared_owners=*owners;
            entries.push_back(std::move(e));
        });
        if(!valid || entries.size()!=field.node_count())return false;
        std::sort(entries.begin(),entries.end(),
            [](entry const &a,entry const &b){return a.id<b.id;});
        count_=entries.size();
        uint32_t capacity=next_capacity(count_);
        // A larger table is preferable to a potentially unbounded shader
        // loop. Never publish a table unless ALL lookups take <=32 probes.
        for(unsigned attempt=0;attempt<6U && capacity; ++attempt) {
            if(fill(entries,capacity)) {
                ready_=true;
                return true;
            }
            if(capacity>=4194304U)break;
            capacity*=2U;
        }
        reset();
        return false;
    }
    bool ready() const {return ready_;}
    uint32_t capacity() const {return static_cast<uint32_t>(keys_.size());}
    uint32_t mask() const {return mask_;}
    uint32_t max_probes() const {return longest_probe_;}
    size_t node_count() const {return count_;}
    float spacing() const {return spacing_;}
    size_t key_bytes() const {return keys_.size()*sizeof(key);}
    size_t value_bytes() const {return values_.size()*sizeof(value);}
    std::vector<key> const &keys() const {return keys_;}
    std::vector<value> const &values() const {return values_;}

    // Return an index compatible with GLSL texelFetch. Any missing
    // coordinate returns -1, never another sector's value.
    int32_t find(uint32_t sector,
            continuous_ambient_field::coord const &p) const {
        if(!ready_ || sector==empty_sector)return -1;
        key const id{{sector,uint32_t(p[0]),uint32_t(p[1]),
                      uint32_t(p[2])}};
        uint32_t slot=hash(id)&mask_;
        for(uint32_t probe=0;probe<max_probe_budget;++probe) {
            if(keys_[slot]==id)return int32_t(slot);
            if(keys_[slot][0]==empty_sector)return -1;
            slot=(slot+1U)&mask_;
        }
        return -1;
    }
    // The second channel is only used for shared nodes. Ordinary nodes
    // apply max(authored, the model's live sector floor) in the vertex
    // shader; shared nodes already include ALL connected sectors' floors.
    // The operation is atomic on invalid input: a failed native read does
    // not leave a partly updated buffer.
    template<class Floor>
    bool update_shared_floors(Floor const &floor,float authored_limit) {
        if(!ready_ || !std::isfinite(authored_limit) ||
           authored_limit<=0.0f)return false;
        std::vector<float> updates(values_.size(),-1.0f);
        for(size_t slot=0;slot<values_.size();++slot) {
            auto const &members=owners_[slot];
            if(members.empty())continue;
            float sum=0.0f;
            for(uint32_t sector:members) {
                float live=0.0f;
                if(!floor(sector,live) || !std::isfinite(live))return false;
                sum+=std::max(values_[slot][0],
                    std::clamp(live,0.0f,authored_limit));
            }
            updates[slot]=sum/float(members.size());
        }
        for(size_t i=0;i<values_.size();++i)
            values_[i][1]=updates[i];
        return true;
    }
};
static_assert(sizeof(continuous_ambient_gpu_table::key)==16,
              "GL_RGBA32UI transport must use 4 uint32 channels");
static_assert(sizeof(continuous_ambient_gpu_table::value)==8,
              "GL_RG32F transport must use 2 float channels");
} // namespace jkgm
