#pragma once
// Per-Thing SPR size/opacity animation. Never changes a shared rdSprite.
// A Thing's pointer + engine signature identify an instance across slot reuse.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace jkgm {
    struct sprite_instance_info {
        float width=1.0f;
        float height=1.0f;
        float opacity=1.0f;
    };

    inline bool valid_sprite_instance_info(sprite_instance_info const &s) {
        return std::isfinite(s.width) && s.width>=0.0f && s.width<=128.0f &&
               std::isfinite(s.height) && s.height>=0.0f && s.height<=128.0f &&
               std::isfinite(s.opacity) && s.opacity>=0.0f && s.opacity<=1.0f;
    }

    inline sprite_instance_info interpolate_sprite_instance_info(
        sprite_instance_info a,sprite_instance_info b,float t) {
        t=std::max(0.0f,std::min(1.0f,t));
        return {a.width+(b.width-a.width)*t,
                a.height+(b.height-a.height)*t,
                a.opacity+(b.opacity-a.opacity)*t};
    }

    class sprite_instance_registry {
    public:
        bool set(void const *thing,uint32_t signature,
                 sprite_instance_info value,uint32_t now_ms,float seconds) {
            if(!thing || !valid_sprite_instance_info(value) ||
               !std::isfinite(seconds) || seconds<0.0f || seconds>3600.0f)
                return false;
            auto &entry=entries_[thing];
            sprite_instance_info const start=entry.signature==signature ?
                evaluate(entry,now_ms) : sprite_instance_info{};
            entry.signature=signature;
            entry.start=start;
            entry.target=value;
            entry.started=now_ms;
            entry.duration=static_cast<uint32_t>(seconds*1000.0f);
            if(entry.duration==0) entry.start=value;
            return true;
        }

        sprite_instance_info sample(void const *thing,uint32_t signature,
                                    uint32_t now_ms) const {
            auto const it=entries_.find(thing);
            if(it==entries_.end() || it->second.signature!=signature)
                return {};
            return evaluate(it->second,now_ms);
        }

        // Roll is an independent channel: a 360-degree transition must spin
        // a complete turn, rather than collapsing to a shortest-arc zero turn.
        bool set_roll(void const *thing,uint32_t signature,float degrees,
                      uint32_t now_ms,float seconds) {
            if(!thing || !std::isfinite(degrees) ||
               std::fabs(degrees)>36000.0f ||
               !std::isfinite(seconds) || seconds<0.0f || seconds>3600.0f)
                return false;
            auto &roll=rolls_[thing];
            float const start=roll.signature==signature ?
                evaluate_roll(roll,now_ms) : 0.0f;
            roll.signature=signature;
            roll.start=start;
            roll.target=degrees;
            roll.started=now_ms;
            roll.duration=static_cast<uint32_t>(seconds*1000.0f);
            if(roll.duration==0) roll.start=degrees;
            return true;
        }

        float sample_roll(void const *thing,uint32_t signature,
                          uint32_t now_ms) const {
            auto const it=rolls_.find(thing);
            if(it==rolls_.end() || it->second.signature!=signature)
                return 0.0f;
            return evaluate_roll(it->second,now_ms);
        }

        // Shadow casting is deliberately opt-in per SithThing. A separate
        // signature prevents recycled Thing slots inheriting the setting.
        bool set_cast_shadows(void const *thing,uint32_t signature,bool cast) {
            if(!thing || !signature)return false;
            if(cast)shadow_casters_[thing]=signature;
            else shadow_casters_.erase(thing);
            return true;
        }
        bool casts_shadows(void const *thing,uint32_t signature) const {
            if(!thing || !signature)return false;
            auto const it=shadow_casters_.find(thing);
            return it!=shadow_casters_.end() && it->second==signature;
        }

        void forget(void const *thing) {
            entries_.erase(thing);
            rolls_.erase(thing);
            shadow_casters_.erase(thing);
        }
        void clear() {
            entries_.clear();
            rolls_.clear();
            shadow_casters_.clear();
        }
        size_t size() const {
            return entries_.size()+rolls_.size()+shadow_casters_.size();
        }
    private:
        struct animation {
            uint32_t signature=0;
            sprite_instance_info start{},target{};
            uint32_t started=0,duration=0;
        };
        struct roll_animation {
            uint32_t signature=0;
            float start=0.0f,target=0.0f;
            uint32_t started=0,duration=0;
        };
        static sprite_instance_info evaluate(animation const &a,uint32_t now_ms) {
            if(a.duration==0) return a.target;
            // Unsigned time subtraction supports GetTickCount's wraparound.
            float const t=std::min(1.0f,
                static_cast<float>(now_ms-a.started)/static_cast<float>(a.duration));
            return interpolate_sprite_instance_info(a.start,a.target,t);
        }
        static float evaluate_roll(roll_animation const &a,uint32_t now_ms) {
            if(a.duration==0) return a.target;
            float const t=std::min(1.0f,
                static_cast<float>(now_ms-a.started)/static_cast<float>(a.duration));
            return a.start+(a.target-a.start)*t;
        }
        std::unordered_map<void const *,animation> entries_;
        std::unordered_map<void const *,roll_animation> rolls_;
        std::unordered_map<void const *,uint32_t> shadow_casters_;
    };
}
