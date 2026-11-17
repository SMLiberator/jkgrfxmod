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
