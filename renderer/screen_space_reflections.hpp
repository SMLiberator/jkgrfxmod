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
    gl::uniform_location_id origin_,right_,forward_,up_,tan_,params_,opaque_mode_;
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
         params_(gl::get_uniform_location(the_program,"trace_params")),
         opaque_mode_(gl::get_uniform_location(the_program,"opaque_receiver_mode")){}
    void configure(engine_camera const &camera,float aspect,
                   int steps,float max_distance,float thickness,
                   bool opaque=false) {
        auto vec=[](std::array<float,3> const &v) {
            return make_point(v[0],v[1],v[2]);
        };
        gl::set_uniform_integer(opaque_mode_,opaque?1:0);
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

class ssr_opaque_receiver_shader : public opengl_shader {
    gl::uniform_location_id right_,forward_,up_,origin_,projection_;
    gl::uniform_location_id world_origin_,world_right_,world_forward_,world_up_;
    gl::uniform_location_id depth_,albedo_,normal_,normal2_,resolution_;
    gl::uniform_location_id uv_scale_,scroll_,scroll2_,params_,material_,opacity_,packed_;
public:
    explicit ssr_opaque_receiver_shader(fs::path const &root,bool model)
        :opengl_shader(model?"ssr_opaque_model":"ssr_opaque_world",
            root/(model?"shaders/ssr_opaque_model.vert":
                         "shaders/ssr_world_receiver.vert"),
            root/"shaders/ssr_opaque_receiver.frag"),
         right_(gl::get_uniform_location(the_program,"view_right")),
         forward_(gl::get_uniform_location(the_program,"view_forward")),
         up_(gl::get_uniform_location(the_program,"view_up")),
         origin_(gl::get_uniform_location(the_program,"view_origin")),
         projection_(gl::get_uniform_location(the_program,"projection")),
         world_origin_(gl::get_uniform_location(the_program,"camera_origin")),
         world_right_(gl::get_uniform_location(the_program,"camera_right")),
         world_forward_(gl::get_uniform_location(the_program,"camera_forward")),
         world_up_(gl::get_uniform_location(the_program,"camera_up")),
         depth_(gl::get_uniform_location(the_program,"opaque_depth")),
         albedo_(gl::get_uniform_location(the_program,"albedo_map")),
         normal_(gl::get_uniform_location(the_program,"normal_map")),
         normal2_(gl::get_uniform_location(the_program,"normal_map2")),
         resolution_(gl::get_uniform_location(the_program,"capture_resolution")),
         uv_scale_(gl::get_uniform_location(the_program,"uv_scale")),
         scroll_(gl::get_uniform_location(the_program,"normal_scroll")),
         scroll2_(gl::get_uniform_location(the_program,"normal_scroll2")),
         params_(gl::get_uniform_location(the_program,"normal_params")),
         material_(gl::get_uniform_location(the_program,"material_params")),
         opacity_(gl::get_uniform_location(the_program,"material_opacity")),
         packed_(gl::get_uniform_location(the_program,"packed_vertex_format")){}
    void camera(gpu_3do_matrix34 const &m,engine_camera const &camera,
                float aspect,float far_plane,int width,int height,
                int packed_format=0) {
        gl::set_uniform_vector(right_,make_point(m[0],m[1],m[2],0.0f));
        gl::set_uniform_vector(forward_,make_point(m[3],m[4],m[5],0.0f));
        gl::set_uniform_vector(up_,make_point(m[6],m[7],m[8],0.0f));
        gl::set_uniform_vector(origin_,make_point(m[9],m[10],m[11],1.0f));
        float const h=std::tan(camera.fov*0.008726646259971648f);
        gl::set_uniform_vector(projection_,make_point(
            h,aspect,1.0f/std::max(far_plane,0.01f),0.0f));
        auto xyz=[](std::array<float,3> const &p) {
            return make_point(p[0],p[1],p[2]);
        };
        gl::set_uniform_vector(world_origin_,xyz(camera.position));
        gl::set_uniform_vector(world_right_,xyz(camera.right));
        gl::set_uniform_vector(world_forward_,xyz(camera.forward));
        gl::set_uniform_vector(world_up_,xyz(camera.up));
        gl::set_uniform_vector(resolution_,make_point(float(width),float(height)));
        gl::set_uniform_integer(depth_,0);
        gl::set_uniform_integer(albedo_,1);
        gl::set_uniform_integer(normal_,2);
        gl::set_uniform_integer(normal2_,3);
        gl::set_uniform_integer(packed_,packed_format);
    }
    void material(float width,float height,
                  float strength,float roughness,int alpha_mode,float alpha_cutoff,
                  float alpha_multiplier,bool has_normal,float normal_strength,
                  point<2,float> first_scroll,bool has_normal2,
                  float normal_strength2,point<2,float> second_scroll) {
        gl::set_uniform_vector(uv_scale_,make_point(
            1.0f/std::max(width,1.0f),1.0f/std::max(height,1.0f)));
        gl::set_uniform_vector(scroll_,first_scroll);
        gl::set_uniform_vector(scroll2_,second_scroll);
        gl::set_uniform_vector(params_,make_point(
            has_normal?1.0f:0.0f,normal_strength,
            has_normal2?1.0f:0.0f,normal_strength2));
        gl::set_uniform_vector(material_,make_point(
            strength,roughness,alpha_cutoff,float(alpha_mode)));
        gl::set_uniform_float(opacity_,alpha_multiplier);
    }
};
class ssr_opaque_composite_shader : public opengl_shader {
    gl::uniform_location_id image_,receiver_;
public:
    explicit ssr_opaque_composite_shader(fs::path const &root)
        :opengl_shader("ssr_opaque_composite",
            root/"shaders/postprocess.vert",
            root/"shaders/ssr_opaque_composite.frag"),
         image_(gl::get_uniform_location(the_program,"traced_image")),
         receiver_(gl::get_uniform_location(the_program,"receiver_image")){}
    void configure() {
        gl::set_uniform_integer(image_,0);
        gl::set_uniform_integer(receiver_,1);
    }
};
} // namespace jkgm
