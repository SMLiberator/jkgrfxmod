#include "renderer/hemi_reflection_camera.hpp"
#include <cassert>
#include <cmath>

int main() {
    // The fixed up/down paraboloid pair sees every nonzero world ray.
    // Both halves overlap by five degrees beyond the world Z=0 horizon.
    constexpr float eps=0.0002f;
    std::array<std::array<float,3>,8> const ray_samples{{
        {{0.0f,0.0f,1.0f}},{{0.0f,0.0f,-1.0f}},
        {{1.0f,0.0f,0.0f}},{{0.0f,1.0f,0.0f}},
        {{-1.0f,-2.0f,0.25f}},{{0.03f,1.0f,-0.5f}},
        {{-3.0f,2.0f,5.0f}},{{2.0f,1.0f,-5.0f}}
    }};
    for(auto const &ray:ray_samples) {
        bool any_covered=false;
        float const len=std::sqrt(ray[0]*ray[0]+ray[1]*ray[1]+
                                  ray[2]*ray[2]);
        for(unsigned hemisphere=0;hemisphere<2U;++hemisphere) {
            auto const sample=jkgm::project_dual_paraboloid_direction(
                ray,hemisphere);
            if(!sample.covered)continue;
            any_covered=true;
            assert(sample.uv[0]>=0.0f && sample.uv[0]<=1.0f);
            assert(sample.uv[1]>=0.0f && sample.uv[1]<=1.0f);
            auto const back=jkgm::unproject_dual_paraboloid_direction(
                sample.uv,hemisphere);
            for(unsigned i=0;i<3U;++i)
                assert(std::fabs(back[i]-ray[i]/len)<eps);
        }
        assert(any_covered);
    }
    auto up=jkgm::project_dual_paraboloid_direction(
        {{0.0f,0.0f,1.0f}},0U);
    auto down=jkgm::project_dual_paraboloid_direction(
        {{0.0f,0.0f,-1.0f}},1U);
    assert(up.covered && down.covered);
    assert(std::fabs(up.uv[0]-0.5f)<eps &&
           std::fabs(up.uv[1]-0.5f)<eps);
    assert(std::fabs(down.uv[0]-0.5f)<eps &&
           std::fabs(down.uv[1]-0.5f)<eps);
    auto horizon_top=jkgm::project_dual_paraboloid_direction(
        {{1.0f,0.0f,0.0f}},0U);
    auto horizon_bottom=jkgm::project_dual_paraboloid_direction(
        {{1.0f,0.0f,0.0f}},1U);
    assert(horizon_top.covered && horizon_bottom.covered);
    float const weight=jkgm::dual_paraboloid_upper_weight(0.0f);
    assert(std::fabs(weight-0.5f)<eps);
    assert(jkgm::dual_paraboloid_upper_weight(
        jkgm::dual_paraboloid_overlap_sine)==1.0f);
    assert(jkgm::dual_paraboloid_upper_weight(
        -jkgm::dual_paraboloid_overlap_sine)==0.0f);
    assert(!jkgm::project_dual_paraboloid_direction(
        {{0.0f,0.0f,0.0f}},0U).covered);
    assert(!jkgm::project_dual_paraboloid_direction(
        {{1.0f,0.0f,0.0f}},2U).covered);
    // Longitude reversal for the lower disk matches shader/converter.
    auto y_up=jkgm::project_dual_paraboloid_direction(
        {{0.0f,1.0f,0.0f}},0U);
    auto y_down=jkgm::project_dual_paraboloid_direction(
        {{0.0f,1.0f,0.0f}},1U);
    assert(y_up.uv[1]>0.5f && y_down.uv[1]<0.5f);
    // Two fixed upright/downward cameras, independent of player orientation.
    auto const top=jkgm::make_dual_paraboloid_camera(
        {{1.0f,2.0f,3.0f}},0U);
    auto const bottom=jkgm::make_dual_paraboloid_camera(
        {{1.0f,2.0f,3.0f}},1U);
    assert(top.valid && bottom.valid);
    assert(top.forward[2]==1.0f && bottom.forward[2]==-1.0f);
    assert(top.right==bottom.right);
    assert(top.up[1]==-1.0f && bottom.up[1]==1.0f);
    assert(!jkgm::make_dual_paraboloid_camera(
        {{0.0f,0.0f,0.0f}},2U).valid);
    // AABB radial test is conservative: no narrow perspective-frustum test.
    auto const origin=std::array<float,3>{{0.0f,0.0f,0.0f}};
    assert(!jkgm::dual_paraboloid_outside_distance(
        {{-1.0f,-1.0f,-1.0f}},{{1.0f,1.0f,1.0f}},origin,32.0f));
    assert(!jkgm::dual_paraboloid_outside_distance(
        {{32.0f,-1.0f,-1.0f}},{{40.0f,1.0f,1.0f}},origin,32.0f));
    assert(jkgm::dual_paraboloid_outside_distance(
        {{33.0f,-1.0f,-1.0f}},{{40.0f,1.0f,1.0f}},origin,32.0f));
    assert(!jkgm::dual_paraboloid_outside_distance(
        {{-100.0f,0.0f,0.0f}},{{100.0f,1.0f,1.0f}},origin,32.0f));
    // Invalid world bounds must fail open, never silently erase geometry.
    assert(!jkgm::dual_paraboloid_outside_distance(
        {{2.0f,0.0f,0.0f}},{{1.0f,1.0f,1.0f}},origin,32.0f));
    // Reject only boxes fully beyond the selected hemisphere's
    // five-degree overlap. Compare transformed bounds against both disks.
    auto const up_view=jkgm::planar_camera_native_view(
        jkgm::make_dual_paraboloid_camera(origin,0U));
    auto const down_view=jkgm::planar_camera_native_view(
        jkgm::make_dual_paraboloid_camera(origin,1U));
    assert(!jkgm::dual_paraboloid_bounds_outside(
        up_view,{{-1.0f,-1.0f,24.0f}},{{1.0f,1.0f,26.0f}},100.0f));
    assert(jkgm::dual_paraboloid_bounds_outside(
        down_view,{{-1.0f,-1.0f,24.0f}},{{1.0f,1.0f,26.0f}},100.0f));
    assert(jkgm::dual_paraboloid_bounds_outside(
        up_view,{{-1.0f,-1.0f,-26.0f}},{{1.0f,1.0f,-24.0f}},100.0f));
    // Near-horizon geometry belongs to both disks for seam blending.
    assert(!jkgm::dual_paraboloid_bounds_outside(
        up_view,{{20.0f,-1.0f,-1.0f}},{{24.0f,1.0f,-0.1f}},100.0f));
    assert(!jkgm::dual_paraboloid_bounds_outside(
        down_view,{{20.0f,-1.0f,0.1f}},{{24.0f,1.0f,1.0f}},100.0f));
    // Reject far geometry; retain large boxes crossing the dividing plane.
    assert(jkgm::dual_paraboloid_bounds_outside(
        up_view,{{200.0f,0.0f,0.0f}},{{205.0f,1.0f,1.0f}},100.0f));
    assert(!jkgm::dual_paraboloid_bounds_outside(
        up_view,{{-1.0f,-1.0f,-100.0f}},{{1.0f,1.0f,100.0f}},100.0f));
    // Missing/inverted bounds must preserve reflection geometry.
    assert(!jkgm::dual_paraboloid_bounds_outside(
        up_view,{{1.0f,0.0f,0.0f}},{{0.0f,1.0f,1.0f}},100.0f));
    assert(jkgm::dual_paraboloid_sphere_outside_distance(
        {{200.0f,0.0f,0.0f}},1.0f,origin,100.0f));
    assert(!jkgm::dual_paraboloid_sphere_outside_distance(
        {{100.0f,0.0f,0.0f}},2.0f,origin,100.0f));
    // Dedicated HEMI/CUBE distance limits never leak to general-purpose
    // shared, planar or one-time COG environment captures.
    assert(jkgm::reflection_probe_capture_distance_limit(
        5,false,false,64.0f)==64.0f);
    assert(jkgm::reflection_probe_capture_distance_limit(
        6,false,false,128.0f)==128.0f);
    assert(jkgm::reflection_probe_capture_distance_limit(
        7,false,true,64.0f)==0.0f);
    assert(jkgm::reflection_probe_capture_distance_limit(
        6,true,false,64.0f)==0.0f);
    assert(jkgm::reflection_probe_capture_distance_limit(
        4,false,false,64.0f)==0.0f);
    assert(jkgm::reflection_probe_capture_distance_limit(
        5,false,false,0.0f)==0.0f);
    // With radial culling disabled, disk orientation must still reject
    // fully opposite-side objects, while unlimited-distance geometry
    // on the selected side is retained.
    assert(jkgm::dual_paraboloid_bounds_outside(
        up_view,{{-1.0f,-1.0f,-26.0f}},
        {{1.0f,1.0f,-24.0f}},0.0f));
    assert(!jkgm::dual_paraboloid_bounds_outside(
        up_view,{{200.0f,0.0f,24.0f}},
        {{205.0f,1.0f,26.0f}},0.0f));
    assert(!jkgm::dual_paraboloid_bounds_outside(
        up_view,{{200.0f,0.0f,-0.5f}},
        {{205.0f,1.0f,0.5f}},0.0f));
    // Cube range culling uses radial transformed bounds too, and is disabled
    // entirely for shared/global environments (radius=0).
    assert(jkgm::reflection_probe_bounds_outside_range(
        up_view,{{200.0f,0.0f,0.0f}},{{202.0f,1.0f,1.0f}},64.0f));
    assert(!jkgm::reflection_probe_bounds_outside_range(
        up_view,{{61.0f,0.0f,0.0f}},{{65.0f,1.0f,1.0f}},64.0f));
    assert(!jkgm::reflection_probe_bounds_outside_range(
        up_view,{{200.0f,0.0f,0.0f}},{{202.0f,1.0f,1.0f}},0.0f));
    assert(!jkgm::reflection_probe_bounds_outside_range(
        up_view,{{5.0f,0.0f,0.0f}},{{4.0f,1.0f,1.0f}},64.0f));
    // Six orthogonal 90-degree cube views must cover every axis and map
    // the direction to its corresponding face center.
    std::array<std::array<float,3>,6> const axes{{
        {{ 1.0f, 0.0f, 0.0f}}, {{-1.0f, 0.0f, 0.0f}},
        {{ 0.0f, 1.0f, 0.0f}}, {{ 0.0f,-1.0f, 0.0f}},
        {{ 0.0f, 0.0f, 1.0f}}, {{ 0.0f, 0.0f,-1.0f}}
    }};
    for(unsigned face=0;face<6U;++face) {
        auto const cube=jkgm::make_cube_reflection_camera(
            {{0.0f,0.0f,0.0f}},face);
        assert(cube.valid && cube.fov==90.0f);
        assert(jkgm::engine_camera_detail::dot(cube.forward,axes[face])>0.9999f);
        assert(std::fabs(jkgm::engine_camera_detail::dot(
            cube.forward,cube.right))<0.00001f);
        assert(std::fabs(jkgm::engine_camera_detail::dot(
            cube.forward,cube.up))<0.00001f);
        auto const sample=jkgm::project_cube_reflection_direction(
            {{0.0f,0.0f,0.0f}},axes[face]);
        assert(sample.covered && sample.face==face);
        assert(std::fabs(sample.uv[0]-0.5f)<1.e-4f);
        assert(std::fabs(sample.uv[1]-0.5f)<1.e-4f);
    }
    // No arbitrary direction is dropped, including corner and rim rays.
    for(auto const &direction:std::array<std::array<float,3>,4>{{
        {{1.0f,1.0f,1.0f}}, {{-1.0f,1.0f,-1.0f}},
        {{0.01f,-4.0f,3.0f}}, {{100.0f,1.0f,0.001f}}
    }}) {
        auto const sample=jkgm::project_cube_reflection_direction(
            {{0.0f,0.0f,0.0f}},direction);
        assert(sample.covered && sample.face<6U);
        assert(sample.uv[0]>=0.0f && sample.uv[0]<=1.0f);
        assert(sample.uv[1]>=0.0f && sample.uv[1]<=1.0f);
    }
    assert(!jkgm::project_cube_reflection_direction(
        {{0.0f,0.0f,0.0f}},{{0.0f,0.0f,0.0f}}).covered);
    assert(!jkgm::make_cube_reflection_camera(
        {{0.0f,0.0f,0.0f}},6U).valid);
    return 0;
}
