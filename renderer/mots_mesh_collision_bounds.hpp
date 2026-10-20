#pragma once
// Optional, purely geometric collision sphere built from a mesh's LOCAL
// vertices. This does not change the mesh's rendering, hierarchy pivot,
// vertex coordinates, or KEY animation. It is separate from the legacy
// mesh RADIUS authored in a 3DO, which is origin-centered.
#include "thing_node_math.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace jkgm {
    struct mots_local_mesh_sphere {
        thing_node_vector center{};
        float radius = 0.0f;
    };

    // AABB midpoint plus maximum distance to any vertex is conservative.
    // This is deliberately NOT the centroid: a vertex-averaged centroid
    // can move dramatically with nonuniform triangulation density.
    // A future tighter enclosing-sphere algorithm can replace this without
    // changing the caller's interface.
    inline bool mots_center_mesh_sphere(
        std::vector<thing_node_vector> const &vertices,
        mots_local_mesh_sphere &out)
    {
        if(vertices.empty() || vertices.size() > 1048576U) return false;

        std::array<double,3> minimum{{
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()
        }};
        std::array<double,3> maximum{{
            -std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()
        }};
        for(auto const &v : vertices) {
            for(int k=0;k<3;++k) {
                if(!std::isfinite(v[k])) return false;
                double x=v[k];
                minimum[k]=std::min(minimum[k],x);
                maximum[k]=std::max(maximum[k],x);
            }
        }
        mots_local_mesh_sphere candidate{};
        std::array<double,3> center{};
        for(int k=0;k<3;++k) {
            center[k]=minimum[k]+(maximum[k]-minimum[k])*0.5;
            candidate.center[k]=static_cast<float>(center[k]);
        }
        double radius_squared=0.0;
        for(auto const &v : vertices) {
            double d2=0.0;
            for(int k=0;k<3;++k) {
                double d=double(v[k])-double(candidate.center[k]);
                d2+=d*d;
            }
            radius_squared=std::max(radius_squared,d2);
        }
        double const r=std::sqrt(radius_squared);
        if(!std::isfinite(r) ||
           r>=double(std::numeric_limits<float>::max()))
            return false;
        // Outward-round, rather than accidentally miss a vertex due to
        // the float conversion of a double-precision radius.
        candidate.radius=static_cast<float>(r);
        if(double(candidate.radius)<r)
            candidate.radius=std::nextafter(
                candidate.radius,std::numeric_limits<float>::infinity());
        out=candidate;
        return true;
    }
}
