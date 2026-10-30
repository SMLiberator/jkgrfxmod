#include "../renderer/native_particle_texture.hpp"
#include <cassert>
#include <cmath>
#include <array>

static bool close(float a,float b) { return std::fabs(a-b)<0.0001f; }

int main()
{
    // JK's camera-space billboard: X is right, Z is up, Y is depth.
    std::array<jkgm::particle_clip_vertex,4> full{{
        {-1,4,-1},{1,4,-1},{1,4,1},{-1,4,1}
    }};
    jkgm::particle_clip_bounds bounds{};
    assert(jkgm::particle_find_quad_bounds(full.data(),4,bounds));
    std::array<jkgm::particle_texture_uv,16> uv{};
    assert(jkgm::particle_texture_clipped_uvs(
        bounds,full.data(),4,64,32,uv.data()));
    assert(close(uv[0].u,0.5f) && close(uv[0].v,31.5f));
    assert(close(uv[1].u,63.5f) && close(uv[1].v,31.5f));
    assert(close(uv[2].u,63.5f) && close(uv[2].v,0.5f));
    assert(close(uv[3].u,0.5f) && close(uv[3].v,0.5f));

    // A billboard clipped halfway through is NOT UV-stretched to 0..1:
    // both X endpoints cover only the middle half of the original texture.
    std::array<jkgm::particle_clip_vertex,4> cropped{{
        {-0.5f,4,-1},{0.5f,4,-1},{0.5f,4,1},{-0.5f,4,1}
    }};
    assert(jkgm::particle_texture_clipped_uvs(
        bounds,cropped.data(),4,64,32,uv.data()));
    assert(close(uv[0].u,16.25f));
    assert(close(uv[1].u,47.75f));
    assert(close(uv[2].v,0.5f));

    // Intersections created by near-plane clipping need UVs interpolated
    // at their camera-space position, regardless of their new depth.
    std::array<jkgm::particle_clip_vertex,3> near{{
        {-1,0.02f,-1},{0,0.02f,0},{1,3,1}
    }};
    assert(jkgm::particle_texture_clipped_uvs(
        bounds,near.data(),3,64,32,uv.data()));
    assert(close(uv[1].u,32.0f) && close(uv[1].v,16.0f));

    auto bad=full;
    bad[2].x=INFINITY;
    assert(!jkgm::particle_find_quad_bounds(bad.data(),4,bounds));
    assert(!jkgm::particle_texture_clipped_uvs(
        bounds,full.data(),4,0,32,uv.data()));
}
