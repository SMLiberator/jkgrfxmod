#include "renderer/cog_raycast_native_sphere.hpp"
#include <array>
#include <cassert>
#include <limits>

using jkgm::cog_raycast_native_sphere_candidate;
using vec=std::array<float,3>;
int main()
{
    vec const start{{0.0f,0.0f,0.0f}};
    vec const along_x{{1.0f,0.0f,0.0f}};
    auto sphere=[&](vec center,float radius,float length=10.0f) {
        return cog_raycast_native_sphere_candidate(
            start,along_x,length,center,radius);
    };
    // Ordinary native collideSize candidates; the triangle solver is run
    // only after passing this inexpensive test.
    assert(sphere({{5,0,0}},1));
    assert(sphere({{5,0.9f,0}},1));
    assert(sphere({{5,0,0}},0.1f));
    // A sphere hit does NOT prove a polygon hit: eliminating these false
    // positives is the entire purpose of RayCastEx's second stage.
    assert(!sphere({{5,1.1f,0}},1));
    assert(!sphere({{5,8,0}},1));
    // Finite segment: neither behind the origin nor past the end.
    assert(!sphere({{-5,0,0}},2));
    assert(!sphere({{14,0,0}},3));
    assert(sphere({{10.2f,0,0}},0.3f));
    // Starting within a sphere must still allow a face check.
    assert(sphere(start,2));
    // Respect the authored native radius, even when an actual 3DO polygon
    // lies outside it. False negatives are explicitly acceptable here.
    assert(!sphere({{5,2,0}},1));
    assert(sphere({{5,2,0}},3));
    // No native sphere means no candidate for a zero-radius ray.
    assert(!sphere({{5,0,0}},0));
    assert(!sphere({{5,0,0}},-1));
    assert(!sphere({{5,0,0}},1,0));
    constexpr float nan=std::numeric_limits<float>::quiet_NaN();
    assert(!sphere({{5,nan,0}},1));
    assert(!sphere({{5,0,0}},nan));
    assert(!cog_raycast_native_sphere_candidate(
        start,{{nan,0,0}},10,{{5,0,0}},2));
    // Symmetric behavior for rays pointing in the opposite direction.
    assert(cog_raycast_native_sphere_candidate(
        start,{{-1,0,0}},10,{{-4,0,0}},0.5f));
    assert(!cog_raycast_native_sphere_candidate(
        start,{{-1,0,0}},10,{{4,0,0}},0.5f));
}
