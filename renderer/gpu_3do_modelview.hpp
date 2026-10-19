#pragma once
// JK native rdMatrix34: right/forward/up/translation stored as 4 vec3.
// Match rdMatrix_Multiply34(camera.orient, jointPlacement) *once per instance*
// rather than doing world-transform then camera subtraction for each GPU vertex.
// Each component follows the native ordering of float operations, which also
// avoids cancellation when both the camera and objects have large coordinates.
#include <array>
#include <cmath>
namespace jkgm {
    using gpu_3do_matrix34=std::array<float,12>;
    inline gpu_3do_matrix34 gpu_3do_native_modelview(
        gpu_3do_matrix34 const &a,gpu_3do_matrix34 const &b)
    {
        gpu_3do_matrix34 c{};
        // Same formula/evaluation order as JK's rdMatrix_Multiply34.
        c[0]=(a[6]*b[2])+(b[1]*a[3])+(b[0]*a[0]);
        c[1]=(a[1]*b[0])+(a[4]*b[1])+(a[7]*b[2]);
        c[2]=(a[2]*b[0])+(a[8]*b[2])+(a[5]*b[1]);

        c[3]=(b[3]*a[0])+(b[5]*a[6])+(b[4]*a[3]);
        c[4]=(b[5]*a[7])+(b[4]*a[4])+(b[3]*a[1]);
        c[5]=(b[5]*a[8])+(a[5]*b[4])+(b[3]*a[2]);

        c[6]=(b[6]*a[0])+(b[7]*a[3])+(b[8]*a[6]);
        c[7]=(b[6]*a[1])+(b[7]*a[4])+(b[8]*a[7]);
        c[8]=(b[8]*a[8])+(b[6]*a[2])+(b[7]*a[5]);

        c[9]=(b[9]*a[0])+(b[11]*a[6])+(b[10]*a[3])+a[9];
        c[10]=(b[9]*a[1])+(b[10]*a[4])+(b[11]*a[7])+a[10];
        c[11]=(b[10]*a[5])+(b[9]*a[2])+(b[11]*a[8])+a[11];
        return c;
    }
    inline bool gpu_3do_matrix_finite(gpu_3do_matrix34 const &m)
    {
        for(float v:m) if(!std::isfinite(v)) return false;
        return true;
    }
    inline std::array<float,3> gpu_3do_native_transform_point(
        gpu_3do_matrix34 const &m,std::array<float,3> const &v)
    {
        return {{
            (m[3]*v[1]+m[6]*v[2]+m[0]*v[0])+m[9],
            (m[4]*v[1]+m[7]*v[2]+m[1]*v[0])+m[10],
            (m[8]*v[2]+m[5]*v[1]+m[2]*v[0])+m[11]
        }};
    }
}
