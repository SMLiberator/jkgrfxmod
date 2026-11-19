#pragma once
// Dedicated minimal shader for a planar reflected scene. This lives outside
// the main G-buffer and never alters the native JK camera or its draw queue.
#include "shaders.hpp"
#include "gpu_3do_modelview.hpp"
#include "gpu_3do_uv.hpp"
#include "offscreen_capture_projection.hpp"
#include "world_cache_sky.hpp"
#include <array>
#include <algorithm>

namespace jkgm {
class planar_reflection_capture_shader : public opengl_shader {
    gl::uniform_location_id vr_,vf_,vu_,origin_,projection_,clip_;
    gl::uniform_location_id clip_enabled_,ortho_mode_;
    gl::uniform_location_id model_,sampler_,uv_scale_,uv_scroll_;
    gl::uniform_location_id tint_,has_albedo_,alpha_;
    gl::uniform_location_id fog_params_,fog_color_,jk_filter_,jk_tint_,jk_fade_,jk_limit_;
    gl::uniform_location_id model_ambient_,model_mode_,model_cmp_,model_sector_;
    gl::uniform_location_id packed_vertex_format_;
    gl::uniform_location_id emission_sampler_,emission_factor_,emission_scroll_;
    gl::uniform_location_id emission_enabled_,emission_only_;
    gl::uniform_location_id thing_opacity_,thing_mode_;
    gl::uniform_location_id sky_mode_,sky_horizon_,sky_angles_;
    gl::uniform_location_id sky_origin_,sky_right_,sky_forward_,sky_up_;
    gl::uniform_location_id sky_ceiling_,sky_offset_,sky_face_,sky_viewport_;
    gl::uniform_location_id paraboloid_hemisphere_,paraboloid_inv_far_;
    bool model_uv_=false; // World/SKY UVs use full MAT size, 3DOs cap at 256.
public:
    explicit planar_reflection_capture_shader(fs::path const &root,
                                                bool paraboloid=false)
        :opengl_shader(paraboloid?"dual_paraboloid_capture":"planar_capture",
            root/(paraboloid?"shaders/dual_paraboloid_capture.vert":
                "shaders/planar_capture.vert"),
            root/(paraboloid?"shaders/dual_paraboloid_capture.frag":
                "shaders/planar_capture.frag"),
            paraboloid?root/"shaders/dual_paraboloid_capture.geom":
                fs::path()),
         vr_(gl::get_uniform_location(the_program,"view_right")),
         vf_(gl::get_uniform_location(the_program,"view_forward")),
         vu_(gl::get_uniform_location(the_program,"view_up")),
         origin_(gl::get_uniform_location(the_program,"view_origin")),
         projection_(gl::get_uniform_location(the_program,"projection")),
         clip_(gl::get_uniform_location(the_program,"clip_plane_view")),
         clip_enabled_(gl::get_uniform_location(the_program,"clip_enabled")),
         ortho_mode_(gl::get_uniform_location(the_program,"orthographic_mode")),
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
         model_sector_(gl::get_uniform_location(the_program,"model_sector_tint")),
         packed_vertex_format_(gl::get_uniform_location(the_program,"packed_vertex_format")),
         emission_sampler_(gl::get_uniform_location(the_program,"emissive_map")),
         emission_factor_(gl::get_uniform_location(the_program,"emissive_factor")),
         emission_scroll_(gl::get_uniform_location(the_program,"emissive_scroll")),
         emission_enabled_(gl::get_uniform_location(the_program,"has_emissive")),
         emission_only_(gl::get_uniform_location(the_program,"emissive_only")),
         thing_opacity_(gl::get_uniform_location(the_program,"capture_thing_opacity")),
         thing_mode_(gl::get_uniform_location(the_program,"capture_thing_mode")),
         sky_mode_(gl::get_uniform_location(the_program,"capture_sky_mode")),
         sky_horizon_(gl::get_uniform_location(the_program,"capture_sky_horizon")),
         sky_angles_(gl::get_uniform_location(the_program,"capture_sky_angles")),
         sky_origin_(gl::get_uniform_location(the_program,"capture_sky_camera_origin")),
         sky_right_(gl::get_uniform_location(the_program,"capture_sky_camera_right")),
         sky_forward_(gl::get_uniform_location(the_program,"capture_sky_camera_forward")),
         sky_up_(gl::get_uniform_location(the_program,"capture_sky_camera_up")),
         sky_ceiling_(gl::get_uniform_location(the_program,"capture_sky_ceiling_height")),
         sky_offset_(gl::get_uniform_location(the_program,"capture_sky_offset")),
         sky_face_(gl::get_uniform_location(the_program,"capture_sky_face_offset")),
         sky_viewport_(gl::get_uniform_location(the_program,"capture_sky_viewport")),
         paraboloid_hemisphere_(gl::get_uniform_location(the_program,
             "paraboloid_hemisphere")),
         paraboloid_inv_far_(gl::get_uniform_location(the_program,
             "paraboloid_inv_far"))
    {}
    void set_paraboloid(unsigned hemisphere,float far_plane) {
        gl::set_uniform_integer(paraboloid_hemisphere_,int(hemisphere));
        gl::set_uniform_float(paraboloid_inv_far_,
            far_plane>0.1f?1.0f/far_plane:0.0f);
    }
    void set_capture_sky(int mode,world_cache_sky_info const &info,
        std::array<float,3> const &angles,engine_camera const &camera,
        float half_fov,int width,int height,
        std::array<float,2> const &face_offset)
    {
        gl::set_uniform_integer(sky_mode_,mode);
        if(!mode)return;
        auto const &offset=mode==1
            ?info.horizon_offset:info.ceiling_offset;
        gl::set_uniform_vector(sky_horizon_,make_point(
            info.horizon_distance*half_fov,
            info.pixels_per_revolution,half_fov,0.0f));
        gl::set_uniform_vector(sky_angles_,make_point(
            angles[0],angles[1],angles[2]));
        gl::set_uniform_vector(sky_origin_,make_point(
            camera.position[0],camera.position[1],camera.position[2]));
        gl::set_uniform_vector(sky_right_,make_point(
            camera.right[0],camera.right[1],camera.right[2]));
        gl::set_uniform_vector(sky_forward_,make_point(
            camera.forward[0],camera.forward[1],camera.forward[2]));
        gl::set_uniform_vector(sky_up_,make_point(
            camera.up[0],camera.up[1],camera.up[2]));
        gl::set_uniform_float(sky_ceiling_,info.ceiling_height);
        gl::set_uniform_vector(sky_offset_,make_point(offset[0],offset[1]));
        gl::set_uniform_vector(sky_face_,make_point(
            face_offset[0],face_offset[1]));
        gl::set_uniform_vector(sky_viewport_,make_point(
            float(width),float(height)));
    }
    // 0=opaque/emissive, 1=premultiplied alpha blend, 2=depth dither.
    void set_thing_fade(float opacity,int mode) {
        gl::set_uniform_float(thing_opacity_,opacity);
        gl::set_uniform_integer(thing_mode_,mode);
    }
    void set_projection(offscreen_capture_projection const &p) {
        gl::set_uniform_vector(projection_,make_point(
            p.half_horizontal,p.aspect,p.reciprocal_far,
            p.orthographic?1.0f:0.0f));
        gl::set_uniform_integer(ortho_mode_,p.orthographic?1:0);
    }
    void set_modelview(gpu_3do_matrix34 const &m,bool model,bool packed=false) {
        model_uv_=model;
        gl::set_uniform_integer(model_,model?1:0);
        gl::set_uniform_integer(packed_vertex_format_,model && packed?1:0);
        gl::set_uniform_vector(vr_,make_point(m[0],m[1],m[2],0.0f));
        gl::set_uniform_vector(vf_,make_point(m[3],m[4],m[5],0.0f));
        gl::set_uniform_vector(vu_,make_point(m[6],m[7],m[8],0.0f));
        gl::set_uniform_vector(origin_,make_point(m[9],m[10],m[11],1.0f));
    }
    void set_clip(std::array<float,4> const &p,bool enabled=true) {
        gl::set_uniform_vector(clip_,make_point(p[0],p[1],p[2],p[3]));
        gl::set_uniform_integer(clip_enabled_,enabled?1:0);
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
    void set_emission(bool textured,point<4,float> strength,
                      point<2,float> scroll,bool only) {
        gl::set_uniform_integer(emission_sampler_,1);
        gl::set_uniform_integer(emission_enabled_,textured?1:0);
        gl::set_uniform_integer(emission_only_,only?1:0);
        gl::set_uniform_vector(emission_factor_,strength);
        gl::set_uniform_vector(emission_scroll_,scroll);
    }
    void set_material(float width,float height,
        point<4,float> tint,point<2,float> scroll,
        bool textured,float alpha_cutoff)
    {
        gl::set_uniform_integer(sampler_,0);
        gl::set_uniform_integer(has_albedo_,textured?1:0);
        float const reference_width=model_uv_
            ?gpu_3do_model_uv_extent(width):width;
        float const reference_height=model_uv_
            ?gpu_3do_model_uv_extent(height):height;
        gl::set_uniform_vector(uv_scale_,make_point(
            1.0f/std::max(reference_width,1.0f),
            1.0f/std::max(reference_height,1.0f)));
        gl::set_uniform_vector(uv_scroll_,scroll);
        gl::set_uniform_vector(tint_,tint);
        gl::set_uniform_float(alpha_,alpha_cutoff);
    }
};
} // namespace jkgm
