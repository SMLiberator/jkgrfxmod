#include "renderer/sun_projected_geometry.hpp"
#include <cassert>
#include <cmath>
using namespace jkgm;
int main()
{
    projected_sun_triangle caster{{{{0,0,2},{1,0,2},{0,1,2}}},123U};
    std::array<decal_vec3,3> receiver{{{{-2,-2,0}},{{3,-2,0}},{{-2,3,0}}}};
    std::vector<projected_sun_triangle> output;
    project_sun_triangle_on_receiver(caster,receiver,{{0,0,1}},
                                     {{0,0,-1}},8.0f,output,30);
    assert(!output.empty());
    for(auto const &tri:output)for(auto const &p:tri.points) {
        assert(std::fabs(p[2])<0.0001f);
        assert(std::fabs(tri.receiver_normal[2]-1.0f)<0.0001f);
    }
    auto b=projected_sun_swept_bounds(caster,{{0,0,-1}},8.0f);
    assert(b.low[2]<0.0f && b.high[2]>1.99f);
    // The reverse ray is behind the receiver; no shadow.
    output.clear();
    project_sun_triangle_on_receiver(caster,receiver,{{0,0,1}},
                                     {{0,0,1}},8.0f,output,30);
    assert(output.empty());
    // Finite length: geometry cannot cast through an arbitrary level.
    project_sun_triangle_on_receiver(caster,receiver,{{0,0,1}},
                                     {{0,0,-1}},0.5f,output,30);
    assert(output.empty());
    // A receiver triangle cutting the projection crops the silhouette.
    std::array<decal_vec3,3> partial{{{{0.1f,0.1f,0}},
                                      {{0.5f,0.1f,0}},{{0.1f,0.5f,0}}}};
    project_sun_triangle_on_receiver(caster,partial,{{0,0,1}},
                                     {{0,0,-1}},8.0f,output,30);
    assert(!output.empty());
    for(auto const &tri:output)for(auto const &p:tri.points) {
        assert(p[0]>=0.099f && p[1]>=0.099f);
        assert(p[0]+p[1]<=0.601f);
    }
    // A tilted/nonplanar world polygon carries one SithSurface normal,
    // while individual triangles may have different plane normals. The
    // projected shadow MUST lie on the actual receiver triangle plane.
    std::array<decal_vec3,3> tilted{{{{-2,-2,-0.4f}},
                                     {{3,-2,0.6f}},{{-2,3,-0.4f}}}};
    output.clear();
    project_sun_triangle_on_receiver(caster,tilted,{{0,0,1}},
                                     {{0,0,-1}},8.0f,output,30);
    assert(!output.empty());
    for(auto const &tri:output)for(auto const &p:tri.points) {
        // x=-2 => z=-0.4, x=3 => z=0.6
        assert(std::fabs(p[2]-(0.2f*p[0]))<0.0002f);
        assert(std::fabs(decal_dot(tri.receiver_normal,
            decal_sub(p,tilted[0])))<0.0002f);
    }
    return 0;
}
