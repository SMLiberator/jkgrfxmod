#pragma once
// Read-only camera-oriented viewer of the actual two-/six-layer reflection
// capture texture. Does not modify native rdCamera or any capture projection.
#include "shaders.hpp"
#include "engine_camera_data.hpp"
#include <cmath>

namespace jkgm {
class reflection_debug_view_shader final : public opengl_shader {
    gl::uniform_location_id sampler_,right_,forward_,up_,fov_,aspect_,mode_;
public:
    explicit reflection_debug_view_shader(fs::path const &root)
        :opengl_shader("reflection_debug_view",
            root/"shaders/postprocess.vert",
            root/"shaders/reflection_debug_view.frag"),
         sampler_(gl::get_uniform_location(the_program,"capture_map")),
         right_(gl::get_uniform_location(the_program,"camera_right")),
         forward_(gl::get_uniform_location(the_program,"camera_forward")),
         up_(gl::get_uniform_location(the_program,"camera_up")),
         fov_(gl::get_uniform_location(the_program,"tan_half_fov")),
         aspect_(gl::get_uniform_location(the_program,"height_over_width")),
         mode_(gl::get_uniform_location(the_program,"hemi_projection")) {}
    void set_sampler(int slot) {
        gl::set_uniform_integer(sampler_,slot);
    }
    void set_projection(bool hemi) {
        gl::set_uniform_integer(mode_,hemi?1:0);
    }
    void set_camera(engine_camera const &camera,int width,int height) {
        gl::set_uniform_vector(right_,make_point(
            camera.right[0],camera.right[1],camera.right[2]));
        gl::set_uniform_vector(forward_,make_point(
            camera.forward[0],camera.forward[1],camera.forward[2]));
        gl::set_uniform_vector(up_,make_point(
            camera.up[0],camera.up[1],camera.up[2]));
        constexpr float radians=0.008726646259971648f;
        gl::set_uniform_float(fov_,std::tan(camera.fov*radians));
        gl::set_uniform_float(aspect_,width>0?float(height)/float(width):1.0f);
    }
};
} // namespace jkgm
