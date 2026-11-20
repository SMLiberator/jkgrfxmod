#include "renderer/planar_reflection_camera.hpp"
#include <cassert>
#include <cmath>
#include <limits>

static bool near(float x,float y) { return std::abs(x-y)<1e-4f; }

int main()
{
    using namespace jkgm;
    engine_camera source{};
    source.valid=true;
    source.position={{4.0f,8.0f,3.0f}};
    planar_reflection_plane water{};
    water.offset=1.0f;
    auto const reflected=make_planar_reflection_camera(source,water);
    assert(reflected.valid);
    assert(near(reflected.position[0],4.0f));
    assert(near(reflected.position[1],8.0f));
    assert(near(reflected.position[2],-1.0f));
    assert(near(reflected.right[0],1.0f));
    assert(near(reflected.forward[1],1.0f));
    assert(near(reflected.up[2],-1.0f));

    auto const m=planar_camera_native_view(reflected);
    auto const origin=gpu_3do_native_transform_point(m,reflected.position);
    assert(near(origin[0],0.0f));
    assert(near(origin[1],0.0f));
    assert(near(origin[2],0.0f));
    auto const above=gpu_3do_native_transform_point(
        m,{{4.0f,13.0f,3.0f}});
    assert(near(above[0],0.0f) && near(above[1],5.0f));
    assert(near(above[2],-4.0f));
    gpu_3do_matrix34 translation{{
        1,0,0,0,1,0,0,0,1,4,13,3
    }};
    auto const model_view=gpu_3do_native_modelview(m,translation);
    auto const mesh_origin=gpu_3do_native_transform_point(
        model_view,{{0.0f,0.0f,0.0f}});
    for(int i=0;i<3;++i)assert(near(mesh_origin[i],above[i]));

    // World and 3DO projections must agree for an arbitrarily tilted plane.
    planar_reflection_plane tilted{};
    tilted.normal={{0.0f,0.0f,2.0f}};
    tilted.offset=2.0f;
    auto const scaled=make_planar_reflection_camera(source,tilted);
    assert(near(scaled.position[2],reflected.position[2]));
    tilted.normal={{1.0f,0.0f,1.0f}};
    tilted.offset=0.0f;
    auto const oblique=make_planar_reflection_camera(source,tilted);
    assert(oblique.valid);
    auto const view=planar_camera_native_view(oblique);
    auto const eye=gpu_3do_native_transform_point(view,oblique.position);
    for(float v:eye)assert(near(v,0.0f));

    // Invalid plane or source must never produce a capture camera.
    water.normal={{0.0f,0.0f,0.0f}};
    assert(!make_planar_reflection_camera(source,water).valid);
    water.normal={{0.0f,0.0f,1.0f}};
    water.offset=std::numeric_limits<float>::quiet_NaN();
    assert(!make_planar_reflection_camera(source,water).valid);
    source.valid=false;
    assert(!make_planar_reflection_camera(source,{}).valid);
    return 0;
}
