#pragma once
// Experimental half/eighth-resolution screen-space reflections for cached
// world forward-alpha surfaces. These are ordinary GL 3.3 fullscreen and
// receiver draws; neither reads a texture attached for drawing.
#include "shaders.hpp"
#include "engine_camera_data.hpp"
#include "gpu_3do_modelview.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace jkgm {
class ssr_world_receiver_shader : public opengl_shader {
    gl::uniform_location_id right_,forward_,up_,origin_,projection_;
    gl::uniform_location_id depth_,normal_,normal2_,resolution_,uv_scale_;
    gl::uniform_location_id scroll_,scroll2_,params_;
public:
    explicit ssr_world_receiver_shader(fs::path const &root)
        :opengl_shader("ssr_world_receiver",
            root/"shaders/ssr_world_receiver.vert",
            root/"shaders/ssr_world_receiver.frag"),
         right_(gl::get_uniform_location(the_program,"view_right")),
         forward_(gl::get_uniform_location(the_program,"view_forward")),
         up_(gl::get_uniform_location(the_program,"view_up")),
         origin_(gl::get_uniform_location(the_program,"view_origin")),
         projection_(gl::get_uniform_location(the_program,"projection")),
         depth_(gl::get_uniform_location(the_program,"opaque_depth")),
         normal_(gl::get_uniform_location(the_program,"normal_map")),
         normal2_(gl::get_uniform_location(the_program,"normal_map2")),
         resolution_(gl::get_uniform_location(the_program,"capture_resolution")),
         uv_scale_(gl::get_uniform_location(the_program,"uv_scale")),
         scroll_(gl::get_uniform_location(the_program,"normal_scroll")),
         scroll2_(gl::get_uniform_location(the_program,"normal_scroll2")),
         params_(gl::get_uniform_location(the_program,"normal_params")){}
    void camera(gpu_3do_matrix34 const &m,float horizontal_fov,
                float aspect,float far_plane,int width,int height) {
        gl::set_uniform_vector(right_,make_point(m[0],m[1],m[2],0.0f));
        gl::set_uniform_vector(forward_,make_point(m[3],m[4],m[5],0.0f));
        gl::set_uniform_vector(up_,make_point(m[6],m[7],m[8],0.0f));
        gl::set_uniform_vector(origin_,make_point(m[9],m[10],m[11],1.0f));
        gl::set_uniform_vector(projection_,make_point(
            std::tan(horizontal_fov*0.008726646259971648f),aspect,
            1.0f/far_plane,0.0f));
        gl::set_uniform_vector(resolution_,make_point(float(width),float(height)));
        gl::set_uniform_integer(depth_,0);
        gl::set_uniform_integer(normal_,1);
        gl::set_uniform_integer(normal2_,2);
    }
    void material(float width,float height,
                  bool first,float first_strength,point<2,float> first_scroll,
                  bool second,float second_strength,point<2,float> second_scroll) {
        gl::set_uniform_vector(uv_scale_,make_point(
            1.0f/std::max(width,1.0f),1.0f/std::max(height,1.0f)));
        gl::set_uniform_vector(scroll_,first_scroll);
        gl::set_uniform_vector(scroll2_,second_scroll);
        gl::set_uniform_vector(params_,make_point(
            first?1.0f:0.0f,first_strength,
            second?1.0f:0.0f,second_strength));
    }
};

class ssr_trace_shader : public opengl_shader {
    gl::uniform_location_id receiver_,depth_,color_;
    gl::uniform_location_id origin_,right_,forward_,up_,tan_,params_;
public:
    explicit ssr_trace_shader(fs::path const &root)
        :opengl_shader("ssr_trace",
            root/"shaders/postprocess.vert",
            root/"shaders/ssr_trace.frag"),
         receiver_(gl::get_uniform_location(the_program,"receiver_normal_depth")),
         depth_(gl::get_uniform_location(the_program,"opaque_depth")),
         color_(gl::get_uniform_location(the_program,"opaque_color")),
         origin_(gl::get_uniform_location(the_program,"camera_origin")),
         right_(gl::get_uniform_location(the_program,"camera_right")),
         forward_(gl::get_uniform_location(the_program,"camera_forward")),
         up_(gl::get_uniform_location(the_program,"camera_up")),
         tan_(gl::get_uniform_location(the_program,"tan_half_fov")),
         params_(gl::get_uniform_location(the_program,"trace_params")){}
    void configure(engine_camera const &camera,float aspect,
                   int steps,float max_distance,float thickness) {
        auto vec=[](std::array<float,3> const &v) {
            return make_point(v[0],v[1],v[2]);
        };
        gl::set_uniform_integer(receiver_,0);
        gl::set_uniform_integer(depth_,1);
        gl::set_uniform_integer(color_,2);
        gl::set_uniform_vector(origin_,vec(camera.position));
        gl::set_uniform_vector(right_,vec(camera.right));
        gl::set_uniform_vector(forward_,vec(camera.forward));
        gl::set_uniform_vector(up_,vec(camera.up));
        float const h=std::tan(camera.fov*0.008726646259971648f);
        gl::set_uniform_vector(tan_,make_point(h,h/aspect));
        gl::set_uniform_vector(params_,make_point(
            max_distance,thickness,float(steps),4.0f));
    }
};
} // namespace jkgm
