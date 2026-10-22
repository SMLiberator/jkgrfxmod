#pragma once

// Shared COG-to-renderer material override requests. A MAT ID is the stable
// Sith material index (bit 0x8000 denotes a static material), not a recycled
// Direct3D texture handle. COG verbs never touch OpenGL or engine memory.

#include "animated.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

namespace jkgm {

struct runtime_image_key {
    uint32_t source_mat = 0;
    int image_id = -1;
};

// An absent image key means "use the material's ordinary base image".
// The renderer resolves image keys through the pack belonging to source_mat.
struct runtime_image_transition {
    std::optional<runtime_image_key> from;
    std::optional<runtime_image_key> to;
    double start = 0.0;
    float duration = 0.0f;

    void set(std::optional<runtime_image_key> target, float fade)
    {
        // A new command during an existing fade uses the previous target as
        // its starting point; a later shader revision can preserve three-way
        // blends if interrupted transitions become a practical requirement.
        from = to;
        to = target;
        start = effect_clock_now();
        duration = (std::isfinite(fade) && fade > 0.0f)
                       ? std::min(fade, 3600.0f) : 0.0f;
    }

    float progress(double now = effect_clock_now()) const
    {
        if(duration <= 0.0f) {
            return 1.0f;
        }
        return std::clamp(static_cast<float>((now - start) / duration), 0.0f, 1.0f);
    }
};

// Resolve a COG image crossfade without losing existing materials when an
// image pack hasn't been mapped/uploaded yet. An unresolved new target leaves
// the previous valid image in place; an unresolved old source falls back to
// the material's own base image rather than overriding it with null.
template <typename Image,typename Resolver>
bool resolve_runtime_transition_images(
    runtime_image_transition const &transition,
    std::optional<Image> base,
    Resolver const &resolve,
    std::optional<Image> &from,
    std::optional<Image> &to,
    float &weight)
{
    auto previous=base;
    if(transition.from) {
        auto const found=resolve(*transition.from);
        if(found) previous=found;
    }
    auto target=base;
    if(transition.to) {
        auto const found=resolve(*transition.to);
        if(!found) {
            // Keep the last resolved image while the source pack or requested
            // ID is unavailable. In particular, never clear EVERY EnvMap
            // while waiting for one SetGlobalEnvMap source MAT to load.
            from=previous;
            to=previous;
            weight=0.0f;
            return false;
        }
        target=found;
    }
    weight=transition.progress();
    if(weight>=1.0f) {
        previous=target;
        weight=0.0f;
    }
    from=previous;
    to=target;
    return true;
}

struct runtime_mat_override {
    std::optional<runtime_image_transition> env;
    std::optional<runtime_image_transition> albedo;
    std::optional<animated_floats<3>> color;
};

struct runtime_material_snapshot {
    std::optional<runtime_image_transition> global_env;
    std::map<uint32_t, runtime_mat_override> per_mat;
    uint64_t revision = 0; // increments on COG changes, independent of scan cadence

    bool empty() const { return !global_env && per_mat.empty(); }
};

class runtime_material_controls {
    mutable std::mutex lock_;
    runtime_material_snapshot state_;
    uint32_t observed_world_ = 0;
    uint64_t revision_ = 0;

public:
    void prepare_world(uint32_t current_world)
    {
        if(!current_world) return;
        std::lock_guard<std::mutex> guard(lock_);
        if(observed_world_ && observed_world_ != current_world) {
            state_ = {};
            ++revision_;
        }
        observed_world_ = current_world;
    }
    runtime_material_snapshot snapshot()
    {
        std::lock_guard<std::mutex> guard(lock_);
        double const now = effect_clock_now();
        // Apply completed resets to the authoritative state, not just a
        // temporary copy. Otherwise a later setter revives stale transitions.
        if(state_.global_env && !state_.global_env->to &&
           state_.global_env->progress(now) >= 1.0f) {
            state_.global_env.reset();
        }
        for(auto it = state_.per_mat.begin(); it != state_.per_mat.end();) {
            auto &em = it->second;
            if(em.env && !em.env->to && em.env->progress(now) >= 1.0f) {
                em.env.reset();
            }
            if(em.albedo && !em.albedo->to && em.albedo->progress(now) >= 1.0f) {
                em.albedo.reset();
            }
            if(em.color && !em.color->is_fading(now) &&
               em.color->target() == std::array<float, 3>{{1.0f, 1.0f, 1.0f}}) {
                em.color.reset();
            }
            if(!em.env && !em.albedo && !em.color) it = state_.per_mat.erase(it);
            else ++it;
        }
        auto result = state_;
        result.revision = revision_;
        return result;
    }

    void reset_all()
    {
        std::lock_guard<std::mutex> guard(lock_);
        state_ = {};
        ++revision_;
    }

    void set_global_env(uint32_t source_mat, int image_id, float fade)
    {
        std::lock_guard<std::mutex> guard(lock_);
        if(!state_.global_env) state_.global_env.emplace();
        state_.global_env->set(runtime_image_key{source_mat, image_id}, fade);
        ++revision_;
    }

    void reset_global_env(float fade)
    {
        std::lock_guard<std::mutex> guard(lock_);
        if(state_.global_env) {
            state_.global_env->set(std::nullopt, fade);
            ++revision_;
        }
    }

    void set_mat_env(uint32_t mat, uint32_t source_mat, int image_id, float fade)
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto &em = state_.per_mat[mat].env;
        if(!em) em.emplace();
        em->set(runtime_image_key{source_mat, image_id}, fade);
        ++revision_;
    }

    void reset_mat_env(uint32_t mat, float fade)
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto it = state_.per_mat.find(mat);
        if(it != state_.per_mat.end() && it->second.env) {
            it->second.env->set(std::nullopt, fade);
            ++revision_;
        }
    }

    void set_mat_albedo(uint32_t mat, int image_id, float fade)
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto &em = state_.per_mat[mat].albedo;
        if(!em) em.emplace();
        em->set(runtime_image_key{mat, image_id}, fade);
        ++revision_;
    }

    void reset_mat_albedo(uint32_t mat, float fade)
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto it = state_.per_mat.find(mat);
        if(it != state_.per_mat.end() && it->second.albedo) {
            it->second.albedo->set(std::nullopt, fade);
            ++revision_;
        }
    }

    void set_mat_color(uint32_t mat, std::array<float, 3> rgb, float fade)
    {
        std::lock_guard<std::mutex> guard(lock_);
        auto &em = state_.per_mat[mat].color;
        if(!em) em.emplace(std::array<float, 3>{{1.0f, 1.0f, 1.0f}});
        em->fade_to(rgb, std::isfinite(fade) ? std::max(fade, 0.0f) : 0.0f);
        ++revision_;
    }

    void reset_mat_color(uint32_t mat, float fade)
    {
        set_mat_color(mat, {{1.0f, 1.0f, 1.0f}}, fade);
    }
};

inline runtime_material_controls &get_runtime_material_controls()
{
    static runtime_material_controls controls;
    return controls;
}

} // namespace jkgm
