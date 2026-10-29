#pragma once
// Narrow native-equivalence checks for the experimental GPU 3DO path.
// Filled geometry modes 3 (solid color) and 4 (textured) are GPU drawable.
// Modes 0..2 still require JK's hidden/point/line path. Software TexMode
// (affine/perspective) does not affect GPU UV interpolation.
#include <array>
#include <cmath>
#include <cstdint>

namespace jkgm {
    inline bool gpu_3do_supports_geo_mode(int effective_thing_mode,
                                           int mesh_mode)
    {
        return effective_thing_mode>=3 && effective_thing_mode<=4 &&
               mesh_mode>=3 && mesh_mode<=4;
    }
    inline bool gpu_3do_supports_face_mode(int face_mode)
    {
        return face_mode==3 || face_mode==4;
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

    // Camera origin in the mesh's own coordinates. Solving the affine
    // view*placement matrix avoids a separate world-space camera transform
    // and matches JK's rdModel3_DrawMesh local-camera/face-normal test.
    // Works with nonuniform and negative (mirrored) mesh scales.
    inline bool gpu_3do_local_eye(std::array<float,12> const &m,
                                   std::array<float,3> &eye)
    {
        float const det=gpu_3do_transform_determinant(m);
        if(!std::isfinite(det) || std::fabs(det)<1e-8f) return false;
        auto const cross=[](std::array<float,3> a,std::array<float,3> b) {
            return std::array<float,3>{{a[1]*b[2]-a[2]*b[1],
                                         a[2]*b[0]-a[0]*b[2],
                                         a[0]*b[1]-a[1]*b[0]}};
        };
        auto const dot=[](std::array<float,3> a,std::array<float,3> b) {
            return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
        };
        std::array<float,3> const r{{m[0],m[1],m[2]}};
        std::array<float,3> const f{{m[3],m[4],m[5]}};
        std::array<float,3> const u{{m[6],m[7],m[8]}};
        std::array<float,3> const minus_t{{-m[9],-m[10],-m[11]}};
        eye={{dot(minus_t,cross(f,u))/det,
              dot(r,cross(minus_t,u))/det,
              dot(r,cross(f,minus_t))/det}};
        return std::isfinite(eye[0]) && std::isfinite(eye[1]) &&
               std::isfinite(eye[2]);
    }
    inline bool gpu_3do_face_facing_camera(
        std::array<float,3> const &eye,
        std::array<float,3> const &anchor,
        std::array<float,3> const &normal)
    {
        return (eye[0]-anchor[0])*normal[0] +
               (eye[1]-anchor[1])*normal[1] +
               (eye[2]-anchor[2])*normal[2] > 0.0f;
    }
}
