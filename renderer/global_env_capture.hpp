#pragma once
// COG -> renderer on-demand global EnvMap capture requests. COG code does not
// touch OpenGL: it stores a validated world-space position and Thing identity.
// Replacing or clearing the request is atomic across game/render threads.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

namespace jkgm {
// Persistent on-demand captures include Cog 3DO scenery, including level
// geometry and PATH Things at their current pose. Verify native Thing identity
// without treating a shadow-caster's dynamic state or assigned puppet resources
// as evidence that the Cog is unsuitable for a one-time snapshot.
template<class Read>
bool global_env_capture_static_thing(Read const &read,uint64_t owner)
{
    uint32_t const address=uint32_t(owner>>32);
    uint32_t const expected_signature=uint32_t(owner);
    if(address<0x10000U || address>=0x7ff00000U ||
       expected_signature==0U)return false;
    uintptr_t const thing=address;
    uint32_t flags=0,type=0,move_type=0,attach_flags=0;
    uint32_t signature=0,render_type=0,linked_owner=0;
    if(!read(thing+0x00U,&flags,4) ||
       !read(thing+0x0cU,&type,4) ||
       !read(thing+0x10U,&move_type,4) ||
       !read(thing+0x34U,&attach_flags,4) ||
       !read(thing+0x74U,&signature,4) ||
       !read(thing+0xc4U,&render_type,4) ||
       !read(thing+0x108U,&linked_owner,4))return false;
    // Only Cog (type 7) MODEL3 scenery is baked into global EnvMaps.
    // SITH_TF_LEVELGEO (0x8) must be INCLUDED: it marks 3DO level geometry,
    // not an invisibility bit. Reject destroyed, hidden and HUD Things.
    if(type!=7U ||
       (flags&(0x2U|0x10U|0x20U|0x80000U))!=0U ||
       render_type!=1U || linked_owner!=address ||
       signature!=expected_signature)return false;
    // PATH (2) is valid for a one-time snapshot, even while moving.
    // Physics (1) and attached parts can be transient; keep those excluded.
    // Assigned animation/puppet resources do not imply an active animation.
    if((move_type!=0U && move_type!=2U) ||
       (attach_flags&(0x2U|0x4U|0x80U))!=0U)
        return false;
    return true;
}

// Seamless CaptureEnvMap replacement: when a new fade begins before the
// previous one finishes, its starting image must be what is visible now,
// not just the old fade's latest endpoint.
inline float global_env_fade_factor(float seconds,float elapsed)
{
    if(!(std::isfinite(seconds) && seconds>0.0f))return 1.0f;
    if(!std::isfinite(elapsed))return 0.0f;
    return std::clamp(elapsed/seconds,0.0f,1.0f);
}

enum class global_env_fade_origin {
    none,previous_capture,current_capture,composited_capture
};

inline global_env_fade_origin choose_global_env_fade_origin(
    bool has_current,bool has_previous,float new_fade,float previous_mix)
{
    if(!has_current || !(new_fade>0.0f))
        return global_env_fade_origin::none;
    if(!has_previous || previous_mix>=1.0f)
        return global_env_fade_origin::current_capture;
    if(previous_mix<=0.0f)
        return global_env_fade_origin::previous_capture;
    return global_env_fade_origin::composited_capture;
}

struct global_env_capture_request {
    std::array<float,3> position{};
    uint64_t exclude_owner=0U; // pointer/signature, never reused without validation
    uint32_t world=0U;
    float fade_time=0.0f;
    uint64_t sequence=0U;
};
class global_env_capture_controls {
    std::mutex mutex_;
    std::optional<global_env_capture_request> request_;
    uint64_t sequence_=0U;
    uint32_t world_=0U;
public:
    void prepare_world(uint32_t world) {
        if(!world)return;
        std::lock_guard<std::mutex> guard(mutex_);
        if(world_ && world_!=world) {
            request_.reset();
            ++sequence_;
        }
        world_=world;
    }
    bool request(std::array<float,3> const &position,
                 uint64_t exclude_owner,uint32_t world,float fade_time) {
        for(float v:position)if(!std::isfinite(v))return false;
        if(!std::isfinite(fade_time) || fade_time<0.0f ||
           fade_time>3600.0f || !world)return false;
        std::lock_guard<std::mutex> guard(mutex_);
        if(world_ && world_!=world)request_.reset();
        world_=world;
        request_=global_env_capture_request{
            position,exclude_owner,world,fade_time,++sequence_};
        return true;
    }
    std::optional<global_env_capture_request> snapshot() {
        std::lock_guard<std::mutex> guard(mutex_);
        return request_;
    }
    void clear() {
        std::lock_guard<std::mutex> guard(mutex_);
        request_.reset();
        ++sequence_;
    }
};
inline global_env_capture_controls &get_global_env_capture_controls() {
    static global_env_capture_controls state;
    return state;
}
} // namespace jkgm
