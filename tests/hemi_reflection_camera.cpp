#include "renderer/hemi_reflection_camera.hpp"
#include <cassert>
#include <cmath>

int main() {
    jkgm::engine_camera main{};
    main.valid=true;
    main.position={{10.0f,0.0f,3.0f}};
    main.right={{0.0f,1.0f,0.0f}};
    main.forward={{-1.0f,0.0f,0.0f}};
    main.up={{0.0f,0.0f,1.0f}};
    auto camera=jkgm::make_hemi_reflection_camera(
        {{0.0f,0.0f,0.0f}},main,160.0f);
    assert(camera.valid);
    assert(camera.fov==160.0f);
    assert(std::fabs(jkgm::engine_camera_detail::dot(
        camera.right,camera.forward))<0.0001f);
    assert(std::fabs(jkgm::engine_camera_detail::dot(
        camera.up,camera.forward))<0.0001f);
    auto center=jkgm::project_hemi_reflection_direction(
        camera,camera.forward);
    assert(center.covered);
    assert(std::fabs(center.uv[0]-0.5f)<1.e-4f);
    assert(std::fabs(center.uv[1]-0.5f)<1.e-4f);
    auto behind=jkgm::project_hemi_reflection_direction(
        camera,{{-camera.forward[0],-camera.forward[1],-camera.forward[2]}});
    assert(!behind.covered);
    // A nearly 90-degree surface reflection is not representable by an
    // ordinary 160-degree perspective camera and must use EnvMap fallback.
    assert(!jkgm::project_hemi_reflection_direction(
        camera,camera.right).covered);
    // Handle the special case of the viewer directly above the probe.
    main.position={{0.0f,0.0f,2.0f}};
    camera=jkgm::make_hemi_reflection_camera(
        {{0.0f,0.0f,0.0f}},main,160.0f);
    assert(camera.valid);
    assert(jkgm::engine_camera_detail::finite(camera.right));
    assert(jkgm::engine_camera_detail::finite(camera.up));
    assert(!jkgm::make_hemi_reflection_camera(
        {{0.0f,0.0f,0.0f}},main,180.0f).valid);
    return 0;
}
