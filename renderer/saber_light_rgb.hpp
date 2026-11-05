#pragma once
// Color ONLY the native rdCamera light emitted by a saber. No new lights,
// no writes into rdLight/SithThing, and no COG verbs. Source material color
// is the SideMat rdTexinfoHeader::solidColor identity-CMP palette entry.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace jkgm {
namespace saber_light_rgb {
constexpr unsigned max_camera_lights=64;
using rgb=std::array<float,3>;
inline constexpr rgb white{{1.0f,1.0f,1.0f}};
struct camera_light {
    uint32_t id=0;
    std::array<float,3> position{};
    float intensity=0.0f;
};
struct sample {
    uint32_t id=0;
    rgb color=white;
};
inline rgb from_palette(uint8_t red,uint8_t green,uint8_t blue)
{
    float const maximum=float(std::max({red,green,blue}));
    if(maximum<8.0f)return white;
    // SolidColor supplies the hue; native intensity must stay untouched.
    return {{red/maximum,green/maximum,blue/maximum}};
}
inline bool colored(rgb const &v)
{
    for(float x:v)if(!std::isfinite(x) || x<0 || x>1)return false;
    return std::max({v[0],v[1],v[2]})-
           std::min({v[0],v[1],v[2]})>0.025f;
}
// Identify the SAME rdLight produced by sithRender for
// actorParams.saberBladePos/intensity. Do not tint a coincident ordinary
// dynamic light, or approximate based on a saber hilt/tip midpoint.
inline bool find_unique_native_light(
    camera_light const *lights,size_t count,
    std::array<float,3> const &saber_pos,float saber_intensity,
    uint32_t &id)
{
    if(!lights || !std::isfinite(saber_intensity) || saber_intensity<=0.f)
        return false;
    for(float x:saber_pos)if(!std::isfinite(x))return false;
    bool found=false;
    // The camera position is copied directly from saberBladePos.
    // Leave a small allowance for floating-point pipeline conversions.
    for(size_t i=0;i<count;++i) {
        auto const &l=lights[i];
        if(l.id>=max_camera_lights || !std::isfinite(l.intensity))continue;
        float const tolerance=std::max(0.0001f,0.001f*saber_intensity);
        if(std::fabs(l.intensity-saber_intensity)>tolerance)continue;
        double dist2=0;
        for(unsigned n=0;n<3;++n) {
            double const d=double(l.position[n])-saber_pos[n];
            dist2+=d*d;
        }
        if(dist2>1.e-6)continue;
        if(found)return false; // Ambiguous: preserve original white light.
        id=l.id;
        found=true;
    }
    return found;
}
struct snapshot {
    std::array<rgb,max_camera_lights> colors{};
    std::array<bool,max_camera_lights> valid{};
    snapshot(){for(auto &c:colors)c=white;}
    rgb get(unsigned id) const {
        return id<max_camera_lights && valid[id]?colors[id]:white;
    }
};
class registry {
    std::mutex mutex_;
    snapshot pending_;
public:
    void add(uint32_t id,rgb const &color) {
        if(id>=max_camera_lights || !colored(color))return;
        std::lock_guard<std::mutex> lock(mutex_);
        // A single native camera slot may be seen twice with dual sabers:
        // their material hue belongs to the same actor.
        pending_.colors[id]=color;
        pending_.valid[id]=true;
    }
    snapshot consume() {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot result=pending_;
        pending_=snapshot{};
        return result;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_=snapshot{};
    }
};
inline registry &get_registry() {
    static registry result;
    return result;
}
} // namespace saber_light_rgb
} // namespace jkgm
