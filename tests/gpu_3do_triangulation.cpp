#include "renderer/gpu_3do_triangulation.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

using point3=std::array<float,3>;
using triangle3=std::array<uint32_t,3>;

static double triangle_area_xy(point3 a,point3 b,point3 c) {
    return std::abs(double(b[0]-a[0])*(c[1]-a[1])-
                    double(b[1]-a[1])*(c[0]-a[0]))*0.5;
}
static double triangles_area(std::vector<point3> const &p,
                             std::vector<triangle3> const &tri) {
    double result=0;
    for(auto const &t:tri) {
        assert(t[0]<p.size() && t[1]<p.size() && t[2]<p.size());
        result+=triangle_area_xy(p[t[0]],p[t[1]],p[t[2]]);
    }
    return result;
}
int main() {
    using jkgm::gpu_3do_triangulate_face;
    std::vector<triangle3> triangles;
    bool nonplanar=false;
    auto const normal=point3{{0,0,1}};
    std::vector<point3> const triangle{{
        {{0,0,0}},{{3,0,0}},{{0,3,0}}
    }};
    assert(gpu_3do_triangulate_face(triangle,normal,triangles,nonplanar));
    assert(triangles.size()==1 && !nonplanar);
    assert(std::abs(triangles_area(triangle,triangles)-4.5)<1e-6);

    // Native convex quads already work with the original diagonal; keep
    // their winding, triangulation and UV interpolation unchanged.
    std::vector<point3> const square{{
        {{0,0,0}},{{4,0,0}},{{4,4,0}},{{0,4,0}}
    }};
    assert(gpu_3do_triangulate_face(square,normal,triangles,nonplanar));
    assert(!nonplanar && triangles.size()==2);
    assert((triangles[0]==triangle3{{0,1,2}}));
    assert((triangles[1]==triangle3{{0,2,3}}));

    // A fan from corner zero crosses the concavity: it is not a correct
    // tessellation, whereas the new ear clipping preserves area.
    std::vector<point3> const concave{{
        {{0,0,0}},{{4,0,0}},{{4,4,0}},{{2,1,0}},{{0,4,0}}
    }};
    assert(gpu_3do_triangulate_face(concave,normal,triangles,nonplanar));
    assert(!nonplanar && triangles.size()==3);
    assert(std::abs(triangles_area(concave,triangles)-10.0)<1e-6);
    for(auto t:triangles) {
        auto a=concave[t[0]],b=concave[t[1]],c=concave[t[2]];
        assert((b[0]-a[0])*(c[1]-a[1])-
               (b[1]-a[1])*(c[0]-a[0])>0);
    }
    // Reversed face indexing and stored normal must be handled as well.
    auto reversed=concave;
    std::reverse(reversed.begin(),reversed.end());
    assert(gpu_3do_triangulate_face(reversed,{{0,0,-1}},triangles,nonplanar));
    assert(triangles.size()==3);
    assert(std::abs(triangles_area(reversed,triangles)-10.0)<1e-6);

    // Preserve warped faces with original per-face native backface tests.
    std::vector<point3> const warped{{
        {{0,0,0}},{{2,0,0}},{{2,2,0.25f}},{{0,2,0}}
    }};
    assert(gpu_3do_triangulate_face(warped,normal,triangles,nonplanar));
    assert(nonplanar && triangles.size()==2);

    // No accidental GPU triangles for non-simple or degenerate polygons:
    // the caller must fall back to JK's native mesh draw.
    std::vector<point3> const bowtie{{
        {{0,0,0}},{{3,3,0}},{{0,3,0}},{{3,0,0}}
    }};
    assert(!gpu_3do_triangulate_face(bowtie,normal,triangles,nonplanar));
    assert(triangles.empty());
    std::vector<point3> const line{{
        {{0,0,0}},{{1,0,0}},{{2,0,0}},{{3,0,0}}
    }};
    assert(!gpu_3do_triangulate_face(line,normal,triangles,nonplanar));
    assert(triangles.empty());

    // Collinear extra perimeter corners should not cause missing geometry.
    std::vector<point3> const collinear{{
        {{0,0,0}},{{2,0,0}},{{4,0,0}},{{4,4,0}},{{0,4,0}}
    }};
    assert(gpu_3do_triangulate_face(collinear,normal,triangles,nonplanar));
    assert(std::abs(triangles_area(collinear,triangles)-16.0)<1e-6);
}
