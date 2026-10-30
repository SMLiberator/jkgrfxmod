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

    // The patched JKGM particle routine supplies a diamond, not a square.
    // Only the textured path replaces its four camera-space vertices.
    std::array<jkgm::particle_clip_vertex,4> diamond{{
        {-1,4,0},{0,4,-1},{1,4,0},{0,4,1}
    }};
    auto const native_diamond=diamond;
    jkgm::particle_clip_bounds diamond_bounds{};
    assert(jkgm::particle_make_textured_square(
        diamond.data(),diamond.size(),diamond_bounds));
    assert(close(diamond[0].x,-1) && close(diamond[0].z,-1));
    assert(close(diamond[1].x,1) && close(diamond[1].z,-1));
    assert(close(diamond[2].x,1) && close(diamond[2].z,1));
    assert(close(diamond[3].x,-1) && close(diamond[3].z,1));
    for(auto const &p:diamond)assert(close(p.y,4));
    assert(close(diamond_bounds.min_x,-1) && close(diamond_bounds.max_z,1));
    assert(jkgm::particle_texture_clipped_uvs(
        diamond_bounds,diamond.data(),4,64,32,uv.data()));
    assert(close(uv[0].u,0.5f) && close(uv[0].v,31.5f));
    assert(close(uv[1].u,63.5f) && close(uv[1].v,31.5f));
    assert(close(uv[2].u,63.5f) && close(uv[2].v,0.5f));
    assert(close(uv[3].u,0.5f) && close(uv[3].v,0.5f));
    // The uploaded patched executable uses X radius sqrt(2)*R and
    // Z radius R for its diamond. GeoMode 4 must NOT inherit that 1.414:1
    // aspect ratio; the vanilla square has side length 2R in both axes.
    float const root2=std::sqrt(2.f);
    std::array<jkgm::particle_clip_vertex,4> stretched_diamond{{
        {3.f,7.f,9.f},{3.f+root2,7.f,10.f},
        {3.f,7.f,11.f},{3.f-root2,7.f,10.f}
    }};
    jkgm::particle_clip_bounds stretched_bounds{};
    assert(jkgm::particle_make_textured_square(
        stretched_diamond.data(),4,stretched_bounds));
    assert(close(stretched_bounds.min_x,2.f));
    assert(close(stretched_bounds.max_x,4.f));
    assert(close(stretched_bounds.min_z,9.f));
    assert(close(stretched_bounds.max_z,11.f));
    assert(close(stretched_diamond[0].x,2.f) &&
           close(stretched_diamond[0].z,9.f));
    assert(close(stretched_diamond[1].x,4.f) &&
           close(stretched_diamond[1].z,9.f));
    assert(close(stretched_diamond[2].x,4.f) &&
           close(stretched_diamond[2].z,11.f));
    assert(close(stretched_diamond[3].x,2.f) &&
           close(stretched_diamond[3].z,11.f));
    assert(jkgm::particle_texture_clipped_uvs(
        stretched_bounds,stretched_diamond.data(),4,64,32,uv.data()));
    assert(close(uv[0].u,.5f) && close(uv[0].v,31.5f));
    assert(close(uv[2].u,63.5f) && close(uv[2].v,.5f));

    // The square conversion changes only the supplied mutable copy:
    // untextured particles retain the engine's native diamond.
    assert(close(native_diamond[0].z,0));
    assert(close(native_diamond[1].x,0));

    auto invalid= native_diamond;
    invalid[2].y=6;
    auto const before_invalid=invalid;
    assert(!jkgm::particle_make_textured_square(
        invalid.data(),invalid.size(),diamond_bounds));
    for(size_t i=0;i<invalid.size();++i) {
        assert(close(invalid[i].x,before_invalid[i].x));
        assert(close(invalid[i].y,before_invalid[i].y));
        assert(close(invalid[i].z,before_invalid[i].z));
    }

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
