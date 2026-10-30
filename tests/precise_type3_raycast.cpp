#include "renderer/precise_type3_raycast.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <vector>
#include <limits>
using namespace jkgm::precise_type3;
static bool approx(double a,double b,double tol=0.00003) {
    return std::fabs(a-b)<tol;
}
static double cast(std::vector<vec3> const& poly,vec3 normal,
                   matrix34 const& matrix,vec3 origin,vec3 direction,
                   double max_length=100) {
    auto const inverse=invert(matrix);
    assert(inverse.valid);
    auto const ray=to_local(inverse,origin,direction);
    double t=-1;
    bool const hit=hit_face(
        ray,normal,poly.size(),[&](size_t i){return poly[i];},
        max_length,t);
    return hit?t:-1.0;
}
int main() {
    matrix34 const identity{{1,0,0,0,1,0,0,0,1,0,0,0}};
    std::vector<vec3> const square{{
        {{-1,-1,0}},{{1,-1,0}},{{1,1,0}},{{-1,1,0}}
    }};
    vec3 const normal{{0,0,1}},down{{0,0,-1}},up{{0,0,1}};
    assert(approx(cast(square,normal,identity,{{0,0,2}},down),2));
    assert(approx(cast(square,normal,identity,{{1,0,2}},down),2));
    // A tiny negative edge-side calculation must not turn a hit into a miss.
    assert(approx(cast(square,normal,identity,{{1.0000001f,0,2}},down),2));
    assert(cast(square,normal,identity,{{1.001f,0,2}},down)<0);
    assert(cast(square,normal,identity,{{0,0,-2}},up)<0);
    assert(cast(square,normal,identity,{{0,0,2}},down,1)<0);
    assert(cast(square,normal,identity,{{0,0,2}},{{1,0,0}})<0);
    // An irregular concave polygon: the recessed area is empty.
    std::vector<vec3> const concave{{
        {{0,0,0}},{{3,0,0}},{{3,3,0}},{{2,3,0}},
        {{2,1,0}},{{1,1,0}},{{1,3,0}},{{0,3,0}}
    }};
    assert(approx(cast(concave,normal,identity,{{0.5f,2,2}},down),2));
    assert(cast(concave,normal,identity,{{1.5f,2,2}},down)<0);
    // Non-unit axes, nonzero translation, and negative determinant.
    matrix34 const mirrored{{-2,0,0,0,3,0,0,0,0.5f,5,2,4}};
    auto inv=invert(mirrored);
    assert(inv.valid);
    vec3 world_n{};
    assert(world_normal(inv,normal,world_n));
    assert(approx(world_n[2],1));
    assert(approx(cast(square,normal,mirrored,{{5,2,8}},down),4));
    assert(cast(square,normal,mirrored,{{8,2,8}},down)<0);
    // A skewed matrix (not safely inverted by JK's InvertOrtho34).
    matrix34 const sheared{{1,0.25f,0,0.5f,1,0,0,0,1,10,20,30}};
    assert(approx(cast(square,normal,sheared,{{10,20,35}},down),5));
    assert(approx(cast(square,normal,sheared,{{10,20,35}},down,5),5));
    assert(cast(square,normal,sheared,{{10,20,35}},down,4.99)<0);
    // Zero-scale transforms and malformed inputs are rejected.
    matrix34 const collapsed{{0,0,0,0,1,0,0,0,1,0,0,0}};
    assert(!invert(collapsed).valid);
    matrix34 nan_mat=identity;
    nan_mat[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!invert(nan_mat).valid);
    return 0;
}
