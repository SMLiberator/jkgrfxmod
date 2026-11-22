#pragma once

#include "base/flag_set.hpp"
#include "gl_types.hpp"
#include "math/box.hpp"
#include "math/color.hpp"

namespace jkgm::gl {
    enum class blend_function : enum_type {
        zero = 0x0,
        one = 0x1,
        destination_color = 0x0306,
        one_minus_source_alpha = 0x0303
    };

    enum class capability : enum_type {
        blend = 0x0BE2,
        cull_face = 0x0B44,
        depth_test = 0x0B71,
        framebuffer_srgb = 0x8DB9,
        multisample = 0x809D,
        sample_shading = 0x8C36
    };

    enum class face_mode : enum_type { front = 0x0404, back = 0x0405, front_and_back = 0x0408 };

    enum class comparison_function : enum_type {
        never = 0x0200,
        less = 0x0201,
        equal = 0x0202,
        less_equal = 0x0203,
        greater = 0x0204,
        not_equal = 0x0205,
        greater_equal = 0x0206,
        always = 0x0207
    };

    enum class polygon_mode : enum_type { point = 0x1b00, line = 0x1b01, fill = 0x1b02 };

    enum class clear_flag : bitfield_type { color = 0x00004000, depth = 0x00000100 };
    using clear_flags = flag_set<clear_flag>;

    // OpenGL submission state shared by the main view and offscreen captures.
    // Any raw GL state manipulation/context change must invalidate this cache.
    struct state_cache_stats {
        uint64_t active_calls=0,active_skipped=0;
        uint64_t texture_calls=0,texture_skipped=0;
        uint64_t vao_calls=0,vao_skipped=0;
        uint64_t front_calls=0,front_skipped=0;
        uint64_t cull_calls=0,cull_skipped=0;
        uint64_t cull_mode_calls=0,cull_mode_skipped=0;
    };
    void invalidate_cached_state();
    state_cache_stats consume_state_cache_stats();
    void set_active_texture_unit_raw(int unit);
    void bind_texture_raw(enum_type target,uint_type texture);
    void bind_vertex_array_raw(uint_type vao);
    void set_front_face_raw(enum_type winding);
    void set_cull_face_enabled(bool enabled);

    void enable(capability cap);
    void disable(capability cap);

    void clear(clear_flags cf);

    void set_blend_function(blend_function sfactor, blend_function dfactor);
    void set_blend_function_separate(blend_function src_rgb,
                                     blend_function dst_rgb,
                                     blend_function src_alpha,
                                     blend_function dst_alpha);
    void set_clear_color(color c);
    void set_depth_function(comparison_function func);
    void set_depth_mask(bool enable);
    void set_face_cull_mode(face_mode mode);
    void set_min_sample_shading_factor(float f);
    void set_polygon_mode(face_mode fm, polygon_mode pm);
    void set_viewport(box<2, int> vp);

    void log_errors();
}
