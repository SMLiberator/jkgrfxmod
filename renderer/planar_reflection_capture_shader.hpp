#pragma once
// Dedicated minimal shader for a planar reflected scene. This lives outside
// the main G-buffer and never alters the native JK camera or its draw queue.
#include "shaders.hpp"
#include "gpu_3do_modelview.hpp"
#include <array>
#include <algorithm>

namespace jkgm {
class planar_reflection_capture_shader : public opengl_shader {
    gl::uniform_location_id vr_,vf_,vu_,origin_,projection_,clip_;
    gl::uniform_location_id model_,sampler_,uv_scale_,uv_scroll_;
    gl::uniform_location_id tint_,has_albedo_,alpha_;
    gl::uniform_location_id fog_params_,fog_color_,jk_filter_,jk_tint_,jk_fade_,jk_limit_;
    gl::uniform_location_id model_ambient_,model_mode_,model_cmp_,model_sector_;
public:
    explicit planar_reflection_capture_shader(fs::path const &root)
        :opengl_shader("planar_capture",
            root/"shaders/planar_capture.vert",
            root/"shaders/planar_capture.frag"),
         vr_(gl::get_uniform_location(the_program,"view_right")),
         vf_(gl::get_uniform_location(the_program,"view_forward")),
         vu_(gl::get_uniform_location(the_program,"view_up")),
         origin_(gl::get_uniform_location(the_program,"view_origin")),
         projection_(gl::get_uniform_location(the_program,"projection")),
         clip_(gl::get_uniform_location(the_program,"clip_plane_view")),
         model_(gl::get_uniform_location(the_program,"is_model")),
         sampler_(gl::get_uniform_location(the_program,"albedo_map")),
         uv_scale_(gl::get_uniform_location(the_program,"uv_scale")),
         uv_scroll_(gl::get_uniform_location(the_program,"uv_scroll")),
         tint_(gl::get_uniform_location(the_program,"material_tint")),
         has_albedo_(gl::get_uniform_location(the_program,"use_albedo")),
         alpha_(gl::get_uniform_location(the_program,"alpha_cutoff")),
         fog_params_(gl::get_uniform_location(the_program,"fog_params")),
         fog_color_(gl::get_uniform_location(the_program,"fog_color")),
         jk_filter_(gl::get_uniform_location(the_program,"jk_color_filter")),
         jk_tint_(gl::get_uniform_location(the_program,"jk_color_tint")),
         jk_fade_(gl::get_uniform_location(the_program,"jk_color_add_fade")),
         jk_limit_(gl::get_uniform_location(the_program,"jk_light_limit")),
         model_ambient_(gl::get_uniform_location(the_program,"model_ambient")),
         model_mode_(gl::get_uniform_location(the_program,"model_effective_light_mode")),
         model_cmp_(gl::get_uniform_location(the_program,"model_cmp_tint")),
         model_sector_(gl::get_uniform_location(the_program,"model_sector_tint"))
    {}
    void set_projection(float half_fov,float aspect,float far_plane) {
        gl::set_uniform_vector(projection_,make_point(
            half_fov,aspect,1.0f/far_plane,0.0f));
    }
    void set_modelview(gpu_3do_matrix34 const &m,bool model) {
        gl::set_uniform_integer(model_,model?1:0);
        gl::set_uniform_vector(vr_,make_point(m[0],m[1],m[2],0.0f));
        gl::set_uniform_vector(vf_,make_point(m[3],m[4],m[5],0.0f));
        gl::set_uniform_vector(vu_,make_point(m[6],m[7],m[8],0.0f));
        gl::set_uniform_vector(origin_,make_point(m[9],m[10],m[11],1.0f));
    }
    void set_clip(std::array<float,4> const &p) {
        gl::set_uniform_vector(clip_,make_point(p[0],p[1],p[2],p[3]));
    }
    void set_fog(point<4,float> params,point<3,float> color) {
        gl::set_uniform_vector(fog_params_,params);
        gl::set_uniform_vector(fog_color_,color);
    }
    void set_color_effects(point<4,float> filter,point<4,float> tint,
                           point<4,float> fade,float light_limit) {
        gl::set_uniform_vector(jk_filter_,filter);
        gl::set_uniform_vector(jk_tint_,tint);
        gl::set_uniform_vector(jk_fade_,fade);
        gl::set_uniform_float(jk_limit_,light_limit);
    }
    void set_model_lighting(float ambient,float mode,
                            point<3,float> cmp,point<3,float> sector_tint) {
        gl::set_uniform_float(model_ambient_,ambient);
        gl::set_uniform_float(model_mode_,mode);
        gl::set_uniform_vector(model_cmp_,cmp);
        gl::set_uniform_vector(model_sector_,sector_tint);
    }
    void set_material(float width,float height,
        point<4,float> tint,point<2,float> scroll,
        bool textured,float alpha_cutoff)
    {
        gl::set_uniform_integer(sampler_,0);
        gl::set_uniform_integer(has_albedo_,textured?1:0);
        gl::set_uniform_vector(uv_scale_,make_point(
            1.0f/std::max(width,1.0f),1.0f/std::max(height,1.0f)));
        gl::set_uniform_vector(uv_scroll_,scroll);
        gl::set_uniform_vector(tint_,tint);
        gl::set_uniform_float(alpha_,alpha_cutoff);
    }
};
} // namespace jkgm
