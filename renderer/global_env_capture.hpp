#pragma once
// COG -> renderer on-demand global EnvMap capture requests. COG code does not
// touch OpenGL: it stores a validated world-space position and Thing identity.
// Replacing or clearing the request is atomic across game/render threads.
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

namespace jkgm {
// Like sun shadow's static-caster selection, verify the native SithThing
// identity and reject objects with movement, attachments or active animation.
// Unlike the shadow cache, an idle actor/weapon must NEVER become scenery:
// generated EnvMaps persist long after an individual capture frame.
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
    uint32_t animation_class=0,puppet=0,rd_puppet=0;
    if(!read(thing+0x00U,&flags,4) ||
       !read(thing+0x0cU,&type,4) ||
       !read(thing+0x10U,&move_type,4) ||
       !read(thing+0x34U,&attach_flags,4) ||
       !read(thing+0x74U,&signature,4) ||
       !read(thing+0xc4U,&render_type,4) ||
       !read(thing+0xd8U,&rd_puppet,4) ||
       !read(thing+0x108U,&linked_owner,4) ||
       !read(thing+0x128U,&animation_class,4) ||
       !read(thing+0x12cU,&puppet,4))return false;
    // SithThing types 7=Cog, 8=Ghost (stationary scenery). All other
    // kinds include actors, players, corpses, items, weapons and effects.
    // Invisible, level-geometry and camera/HUD things are not captured.
    if((type!=7U && type!=8U) ||
       (flags&(0x2U|0x8U|0x10U|0x20U|0x80000U))!=0U ||
       render_type!=1U || linked_owner!=address ||
       signature!=expected_signature)return false;
    // A parked physics object isn't a persistent part of the scene. Thing
    // and face/node attachments can follow moving parents even with move=0.
    // Reuse the native attach masks used by the sun/Thing attachment path.
    if(move_type!=0U ||
       (attach_flags&(0x2U|0x4U|0x80U))!=0U ||
       animation_class!=0U || puppet!=0U || rd_puppet!=0U)
        return false;
    return true;
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
