#pragma once
// Asynchronous per-Thing GL 3.3 occlusion queries: configurable budget,
// including an uncapped stress mode. Main-camera GPU submission and opt-in
// native rdModel3 early skips consume completed results independently.
// This does NOT disable engine simulation or physics.
#include "gpu_thing_occlusion_math.hpp"
#include "native_occlusion_bridge.hpp"
#include "gpu_3do_preview.hpp"
#include "gpu_3do_modelview.hpp"
#include "shaders.hpp"
#include "glad/glad.h"
#include <unordered_map>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jkgm {
class gpu_thing_occlusion_shader : public opengl_shader {
    gl::uniform_location_id right_,forward_,up_,origin_,projection_,bound_;
    gl::uniform_location_id proxy_mode_,rect_ndc_,rect_forward_,debug_color_;
public:
    explicit gpu_thing_occlusion_shader(fs::path const &root)
        :opengl_shader("gpu_thing_occlusion",
             root/"shaders/gpu_thing_occlusion.vert",
             root/"shaders/gpu_thing_occlusion.frag"),
         right_(gl::get_uniform_location(the_program,"view_right")),
         forward_(gl::get_uniform_location(the_program,"view_forward")),
         up_(gl::get_uniform_location(the_program,"view_up")),
         origin_(gl::get_uniform_location(the_program,"view_origin")),
         projection_(gl::get_uniform_location(the_program,"projection")),
         bound_(gl::get_uniform_location(the_program,"center_radius")),
         proxy_mode_(gl::get_uniform_location(the_program,"proxy_mode")),
         rect_ndc_(gl::get_uniform_location(the_program,"rect_ndc")),
         rect_forward_(gl::get_uniform_location(the_program,"rect_forward")),
         debug_color_(gl::get_uniform_location(the_program,"debug_color")) {}
    void set_camera(gpu_3do_matrix34 const &v,float half_horizontal_fov,
                    float aspect,float far_plane) {
        gl::set_uniform_vector(right_,make_point(v[0],v[1],v[2],0.0f));
        gl::set_uniform_vector(forward_,make_point(v[3],v[4],v[5],0.0f));
        gl::set_uniform_vector(up_,make_point(v[6],v[7],v[8],0.0f));
        gl::set_uniform_vector(origin_,make_point(v[9],v[10],v[11],1.0f));
        gl::set_uniform_vector(projection_,make_point(
            half_horizontal_fov,aspect,1.0f/far_plane,0.0f));
    }
    void set_proxy_mode(int mode) {
        gl::set_uniform_integer(proxy_mode_,mode);
    }
    void set_rect(gpu_thing_occlusion_screen_rect const &r) {
        gl::set_uniform_vector(rect_ndc_,make_point(
            r.left,r.right,r.bottom,r.top));
        gl::set_uniform_float(rect_forward_,r.center_forward);
    }
    void set_debug_color(float r,float g,float b,float a) {
        gl::set_uniform_vector(debug_color_,make_point(r,g,b,a));
    }
    void set_bound(gpu_thing_occlusion_bound const &b) {
        gl::set_uniform_vector(bound_,make_point(
            b.center[0],b.center[1],b.center[2],b.radius));
    }
};
class gpu_thing_occlusion_queries {
    struct record {
        GLuint query=0;
        bool pending=false,visible=true;
        uint32_t consecutive_hidden=0;
        uint64_t result_frame=0,issued_frame=0,last_seen=0;
        engine_camera result_camera{},issued_camera{};
        gpu_thing_occlusion_bound bound{},issued_bound{};
        uint32_t model=0,issued_model=0;
    };
    std::unordered_map<uint64_t,record> entries_;
    uintptr_t world_=0;
    unsigned proxy_mode_=0U;
    uint64_t submitted_=0,completed_=0,skipped_=0;
    bool invalidated_=false;
public:
    gpu_thing_occlusion_queries()=default;
    gpu_thing_occlusion_queries(gpu_thing_occlusion_queries const &)=delete;
    gpu_thing_occlusion_queries &operator=(
        gpu_thing_occlusion_queries const &)=delete;
    ~gpu_thing_occlusion_queries(){clear();}
    void clear() {
        for(auto &e:entries_)if(e.second.query)
            glDeleteQueries(1,&e.second.query);
        entries_.clear();world_=0;
    }
    void invalidate() { invalidated_=true; }
    void set_proxy_mode(unsigned mode) {
        if(proxy_mode_!=mode) {
            clear();
            proxy_mode_=mode;
        }
    }
    void set_world(uintptr_t world) {
        if(world_!=world || invalidated_) {
            clear();world_=world;invalidated_=false;
        }
    }
    void poll() {
        for(auto &entry:entries_) {
            auto &e=entry.second;
            if(!e.pending)continue;
            GLuint available=0;
            glGetQueryObjectuiv(e.query,GL_QUERY_RESULT_AVAILABLE,&available);
            if(!available)continue;
            GLuint visible=1;
            glGetQueryObjectuiv(e.query,GL_QUERY_RESULT,&visible);
            e.pending=false;
            if(e.issued_frame<e.result_frame)continue;
            e.visible=visible!=0;
            e.result_frame=e.issued_frame;
            e.result_camera=e.issued_camera;
            e.bound=e.issued_bound;
            e.model=e.issued_model;
            e.consecutive_hidden=e.visible?0U:
                std::min(3U,e.consecutive_hidden+1U);
            ++completed_;
        }
    }
    // Experimental coverage: one completed negative suffices and remains
    // reusable for up to 12 frames. Camera/bound checks still invalidate
    // stale evidence. Moving occluders may expose temporal errors to test.
    bool hidden(uint64_t owner,gpu_thing_occlusion_bound const &current,
                engine_camera const &camera,uint64_t frame) {
        if(invalidated_)return false;
        auto it=entries_.find(owner);
        if(it==entries_.end())return false;
        auto const &e=it->second;
        bool const hide=e.consecutive_hidden>=1U && !e.visible &&
            frame>=e.result_frame && frame-e.result_frame<=12U &&
            gpu_thing_occlusion_same_bound(current,e.bound) &&
            gpu_thing_occlusion_camera_compatible(camera,e.result_camera) &&
            !gpu_thing_occlusion_camera_inside(current,camera);
        if(hide)++skipped_;
        return hide;
    }
    bool hidden(uint64_t owner,std::array<float,3> const &position,
                engine_camera const &camera,uint64_t frame) {
        auto it=entries_.find(owner);
        if(it==entries_.end() || invalidated_)return false;
        auto bound=it->second.bound;
        bound.center=position;
        return hidden(owner,bound,camera,frame);
    }
    // Publish a single confirmed negative while it is fresh; the native hook rechecks
    // model identity, current bound, world and camera before early rejection.
    std::vector<native_occlusion_result> confirmed_hidden(
        engine_camera const &camera,uint64_t frame) const {
        std::vector<native_occlusion_result> out;
        if(invalidated_)return out;
        for(auto const &p:entries_) {
            auto const &e=p.second;
            if(!e.model || e.visible || e.consecutive_hidden<1U ||
               frame<e.result_frame || frame-e.result_frame>12U ||
               !gpu_thing_occlusion_bound_valid(e.bound) ||
               !gpu_thing_occlusion_camera_compatible(camera,e.result_camera))
                continue;
            native_occlusion_result record{};
            record.owner=p.first;
            record.model=e.model;
            record.bound=e.bound;
            record.camera=e.result_camera;
            out.push_back(record);
        }
        return out;
    }
    // Read-only previous visibility state for query-proxy visual diagnostics.
    // 0=unresolved, 1=visible, 2=hidden. Not a fresh synchronous GL read.
    unsigned debug_result_state(uint64_t owner) const {
        auto const it=entries_.find(owner);
        if(it==entries_.end() || !it->second.result_frame)return 0U;
        return it->second.visible?1U:2U;
    }
    // A previously negative result must be refreshed promptly: an opaque
    // door can move even when the Thing being tested stays stationary.
    bool needs_retest(uint64_t owner)const {
        auto it=entries_.find(owner);
        return it!=entries_.end() && !it->second.pending &&
               it->second.consecutive_hidden>0U;
    }
    bool begin(uint64_t owner,gpu_thing_occlusion_bound const &bound,
               engine_camera const &camera,uint64_t frame,
               uint32_t model=0) {
        if(!owner || !gpu_thing_occlusion_bound_valid(bound) ||
           gpu_thing_occlusion_camera_inside(bound,camera))return false;
        auto it=entries_.find(owner);
        if(it==entries_.end()) {
            // Do not silently cap active Things during unlimited-budget
            // stress tests; idle query objects are pruned periodically.
            it=entries_.emplace(owner,record{}).first;
        }
        auto &entry=it->second;
        entry.last_seen=frame;
        if(entry.pending || entry.issued_frame==frame)return false;
        if(!entry.query)glGenQueries(1,&entry.query);
        if(!entry.query)return false;
        // Invalidate accumulated negatives when Thing center/radius changes.
        if(entry.result_frame &&
           (!gpu_thing_occlusion_same_bound(bound,entry.bound) ||
            entry.model!=model))
            entry.consecutive_hidden=0;
        entry.pending=true;entry.issued_frame=frame;
        entry.issued_camera=camera;entry.issued_bound=bound;
        entry.issued_model=model;
        glBeginQuery(GL_ANY_SAMPLES_PASSED,entry.query);
        ++submitted_;
        return true;
    }
    void end(){glEndQuery(GL_ANY_SAMPLES_PASSED);}
    void prune(uint64_t frame) {
        // Invalidate idle Thing pointers; preserve bounded query memory.
        if((frame%120U)!=0U)return;
        for(auto it=entries_.begin();it!=entries_.end();) {
            auto &e=it->second;
            if(e.pending || frame<=e.last_seen+120U) {++it;continue;}
            if(e.query)glDeleteQueries(1,&e.query);
            it=entries_.erase(it);
        }
    }
    size_t entries()const{return entries_.size();}
    uint64_t submitted()const{return submitted_;}
    uint64_t completed()const{return completed_;}
    uint64_t skipped()const{return skipped_;}
};
} // namespace jkgm
