#pragma once
// Asynchronous opaque-depth visibility of the reflection RECEIVER, not the
// objects seen by the mirrored camera. GL 3.3 ANY_SAMPLES_PASSED is sampled
// after main-camera opaque 3DO depth is written; the previous completed result
// may skip a later mirrored capture without ever blocking the CPU.
#include "planar_reflection_camera.hpp"
#include "shaders.hpp"
#include "glad/glad.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {

class planar_receiver_visibility_shader : public opengl_shader {
    gl::uniform_location_id right_,forward_,up_,origin_,projection_;
public:
    explicit planar_receiver_visibility_shader(fs::path const &root)
        :opengl_shader("planar_receiver_visibility",
            root/"shaders/planar_receiver_visibility.vert",
            root/"shaders/planar_receiver_visibility.frag"),
         right_(gl::get_uniform_location(the_program,"view_right")),
         forward_(gl::get_uniform_location(the_program,"view_forward")),
         up_(gl::get_uniform_location(the_program,"view_up")),
         origin_(gl::get_uniform_location(the_program,"view_origin")),
         projection_(gl::get_uniform_location(the_program,"projection")){}
    void set_camera(gpu_3do_matrix34 const &v,float fov,
                    float aspect,float far_plane) {
        gl::set_uniform_vector(right_,make_point(v[0],v[1],v[2],0.0f));
        gl::set_uniform_vector(forward_,make_point(v[3],v[4],v[5],0.0f));
        gl::set_uniform_vector(up_,make_point(v[6],v[7],v[8],0.0f));
        gl::set_uniform_vector(origin_,make_point(v[9],v[10],v[11],1.0f));
        gl::set_uniform_vector(projection_,make_point(
            std::tan(fov*0.008726646259971648f),aspect,1.0f/far_plane,0.0f));
    }
};

// Camera motion between asynchronous query and capture invalidates the
// negative result if large enough to expose previously occluded water.
inline bool planar_receiver_camera_near(
    engine_camera const &a,engine_camera const &b)
{
    if(!a.valid || !b.valid || !std::isfinite(a.fov) ||
       !std::isfinite(b.fov) || std::fabs(a.fov-b.fov)>0.02f)
        return false;
    float distance_sq=0.0f,axes_sq=0.0f;
    for(unsigned i=0;i<3;++i) {
        if(!std::isfinite(a.position[i]) ||
           !std::isfinite(b.position[i]) ||
           !std::isfinite(a.forward[i]) ||
           !std::isfinite(b.forward[i]) ||
           !std::isfinite(a.up[i]) ||
           !std::isfinite(b.up[i]))return false;
        auto const d=a.position[i]-b.position[i];
        distance_sq+=d*d;
        auto const df=a.forward[i]-b.forward[i];
        auto const du=a.up[i]-b.up[i];
        axes_sq+=df*df+du*du;
    }
    // Small motion tolerance only: false negatives cause stale reflections.
    return distance_sq<=0.0625f && axes_sq<=0.001f;
}

class planar_receiver_visibility {
    struct slot {
        GLuint id=0;
        bool pending=false;
        uint64_t epoch=0,frame=0;
        engine_camera camera{};
    };
    std::array<slot,3> slots_{};
    uintptr_t world_=0;
    uint64_t revision_=0,epoch_=1,latest_frame_=0;
    planar_reflection_plane plane_{};
    engine_camera latest_camera_{};
    bool latest_valid_=false,latest_visible_=true;
    uint64_t submitted_=0,completed_=0,skipped_=0;
    static bool same_plane(planar_reflection_plane const &a,
                           planar_reflection_plane const &b) {
        if(!std::isfinite(a.offset)||!std::isfinite(b.offset) ||
           std::fabs(a.offset-b.offset)>0.002f)return false;
        for(int i=0;i<3;++i)
            if(!std::isfinite(a.normal[i]) ||
               !std::isfinite(b.normal[i]) ||
               std::fabs(a.normal[i]-b.normal[i])>0.0001f)return false;
        return true;
    }
public:
    planar_receiver_visibility()=default;
    planar_receiver_visibility(planar_receiver_visibility const &)=delete;
    planar_receiver_visibility& operator=(planar_receiver_visibility const &)=delete;
    ~planar_receiver_visibility(){release();}
    void release() {
        std::array<GLuint,3> ids{};
        for(size_t i=0;i<slots_.size();++i)ids[i]=slots_[i].id;
        if(ids[0])glDeleteQueries(3,ids.data());
        slots_={};
        invalidate();
    }
    void invalidate() {
        ++epoch_;
        latest_valid_=false;
        latest_visible_=true;
        latest_frame_=0;
    }
    void select(uintptr_t world,uint64_t revision,
                planar_reflection_plane const &plane) {
        if(world!=world_ || revision!=revision_ ||
           !same_plane(plane_,plane)) {
            world_=world;revision_=revision;plane_=plane;
            invalidate();
        }
    }
    void poll() {
        for(auto &s:slots_) {
            if(!s.pending)continue;
            GLuint available=0;
            glGetQueryObjectuiv(s.id,GL_QUERY_RESULT_AVAILABLE,&available);
            if(!available)continue; // NEVER wait for the GPU
            GLuint visible=1;
            glGetQueryObjectuiv(s.id,GL_QUERY_RESULT,&visible);
            s.pending=false;
            if(s.epoch==epoch_ &&
               (!latest_valid_ || s.frame>latest_frame_)) {
                latest_valid_=true;
                latest_visible_=visible!=0U;
                latest_frame_=s.frame;
                latest_camera_=s.camera;
                ++completed_;
            }
        }
    }
    bool hidden(engine_camera const &current,uint64_t frame) {
        poll();
        bool const skip=latest_valid_ && !latest_visible_ &&
            frame>=latest_frame_ && frame-latest_frame_<=3U &&
            planar_receiver_camera_near(current,latest_camera_);
        if(skip)++skipped_;
        return skip;
    }
    bool begin(engine_camera const &camera,uint64_t frame) {
        if(!slots_[0].id) {
            GLuint ids[3]{};
            glGenQueries(3,ids);
            if(!ids[0])return false;
            for(size_t i=0;i<3;++i)slots_[i].id=ids[i];
        }
        // If every query is pending, don't overwrite one or stall.
        for(auto &s:slots_) {
            if(s.pending)continue;
            s.pending=true;s.epoch=epoch_;s.frame=frame;s.camera=camera;
            glBeginQuery(GL_ANY_SAMPLES_PASSED,s.id);
            ++submitted_;
            return true;
        }
        return false;
    }
    void end(){glEndQuery(GL_ANY_SAMPLES_PASSED);}
    uint64_t submitted()const{return submitted_;}
    uint64_t completed()const{return completed_;}
    uint64_t skipped()const{return skipped_;}
    bool last_visible()const{return latest_visible_;}
};
} // namespace jkgm
