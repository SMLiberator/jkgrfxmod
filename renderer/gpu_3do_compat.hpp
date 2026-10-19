#pragma once
// Narrow native-equivalence checks for the experimental GPU 3DO path.
// Never GPU-draw a mesh while Sith's effective GeoMode requests invisible,
// vertices, wireframe or untextured solid rendering. Those go through JK.
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
    inline bool gpu_3do_supports_geo_mode(int effective_thing_mode,
                                           int mesh_mode)
    {
        constexpr int RD_GEOMETRY_FULL=4;
        return effective_thing_mode==RD_GEOMETRY_FULL &&
               mesh_mode==RD_GEOMETRY_FULL;
    }
    inline bool gpu_3do_supports_face_mode(int face_mode)
    {
        constexpr int RD_GEOMETRY_FULL=4;
        return face_mode==RD_GEOMETRY_FULL;
    }
    // The cached shader uses R/F/U columns. Flipping an odd number of axes
    // reverses the handedness of the placement matrix and OpenGL winding.
    // The same mirrored mesh must cull opposite GL winding so its outward
    // surfaces match the nonmirrored/JK rendering behavior.
    inline float gpu_3do_transform_determinant(std::array<float,12> const &m)
    {
        float const cx=m[4]*m[8]-m[5]*m[7];
        float const cy=m[5]*m[6]-m[3]*m[8];
        float const cz=m[3]*m[7]-m[4]*m[6];
        return m[0]*cx+m[1]*cy+m[2]*cz;
    }
    inline bool gpu_3do_reversed_winding(std::array<float,12> const &m)
    {
        float const det=gpu_3do_transform_determinant(m);
        return std::isfinite(det) && det<0.0f;
    }
}
