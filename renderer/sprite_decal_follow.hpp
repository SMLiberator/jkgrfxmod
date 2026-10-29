#pragma once
// Convert the already-clipped world-space vertices of a type-3 SPR decal
// into the local space of its receiving 3DO mesh. The resulting geometry is
// immutable: on each frame the renderer submits it with the receiver mesh's
// current composed rdMatrix34. No per-frame reprojection or Sith attachment
// is needed.
//
// The 3x3 inverse is GENERAL, not a transpose: SetModelNodeRLUVecs may
// scale, shear or mirror the node axes.
#include "gpu_3do_modelview.hpp"
#include "gpu_3do_preview.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace jkgm {
struct sprite_decal_receiver_identity {
    uint64_t thing_key = 0;       // SithThing address + generation/signature
    uintptr_t model = 0;         // changes when SetThingModel replaces the 3DO
    uintptr_t mesh = 0;          // mesh in that model's hierarchy
    uint64_t mesh_revision = 0;  // changes on editable mesh replacement

    bool matches(sprite_decal_receiver_identity const &other) const {
        return thing_key != 0 && thing_key == other.thing_key &&
            model != 0 && model == other.model &&
            mesh != 0 && mesh == other.mesh &&
            mesh_revision == other.mesh_revision;
    }
};

namespace sprite_decal_follow_detail {
struct inverse_affine {
    std::array<std::array<double,3>,3> rows{};
    std::array<double,3> origin{};
    // Forward linear columns for converting a world normal into a local one.
    std::array<std::array<double,3>,3> cols{};

    bool make(gpu_3do_matrix34 const &m) {
        if(!gpu_3do_matrix_finite(m)) return false;
        for(int i=0;i<3;++i) {
            for(int j=0;j<3;++j) cols[i][j] = m[3*i+j];
            origin[i] = m[9+i];
        }
        auto cross=[](std::array<double,3> const &a,
                      std::array<double,3> const &b) {
            return std::array<double,3>{{
                a[1]*b[2]-a[2]*b[1],
                a[2]*b[0]-a[0]*b[2],
                a[0]*b[1]-a[1]*b[0]
            }};
        };
        auto dot=[](std::array<double,3> const &a,
                    std::array<double,3> const &b) {
            return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
        };
        auto len=[&](std::array<double,3> const &v){
            return std::sqrt(dot(v,v));
        };
        auto const bxc=cross(cols[1],cols[2]);
        auto const cxa=cross(cols[2],cols[0]);
        auto const axb=cross(cols[0],cols[1]);
        double const det=dot(cols[0],bxc);
        double const scale=len(cols[0])*len(cols[1])*len(cols[2]);
        // Relative threshold handles uniform scales while rejecting matrices
        // which are near-singular compared with their actual axis lengths.
        if(!std::isfinite(det) || !std::isfinite(scale) ||
           scale<1.e-20 || std::fabs(det)<=scale*1.e-8)
            return false;
        for(int j=0;j<3;++j) {
            rows[0][j]=bxc[j]/det;
            rows[1][j]=cxa[j]/det;
            rows[2][j]=axb[j]/det;
        }
        return true;
    }

    bool point(float &x,float &y,float &z) const {
        std::array<double,3> const w{{
            double(x)-origin[0],double(y)-origin[1],double(z)-origin[2]
        }};
        std::array<double,3> p{};
        for(int i=0;i<3;++i)
            p[i]=rows[i][0]*w[0]+rows[i][1]*w[1]+rows[i][2]*w[2];
        for(double v:p)
            if(!std::isfinite(v) || std::fabs(v)>1.e8) return false;
        x=float(p[0]);y=float(p[1]);z=float(p[2]);
        return true;
    }

    bool normal(float &x,float &y,float &z) const {
        std::array<double,3> const n{{x,y,z}};
        std::array<double,3> p{};
        // If the GPU uses inverse-transpose(M) on the local normal,
        // local = transpose(M) * world reproduces the original world normal.
        for(int i=0;i<3;++i)
            p[i]=cols[i][0]*n[0]+cols[i][1]*n[1]+cols[i][2]*n[2];
        double const length=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
        if(!std::isfinite(length)||length<1.e-20)return false;
        x=float(p[0]/length);y=float(p[1]/length);z=float(p[2]/length);
        return true;
    }
};
}

// Input geometry comes from make_sprite_decal_gpu_mesh(), which produces
// WORLD-space positions. The face-anchor attribute stores normalized projector
// UV/depth for fragment-level decal edge opacity, NOT a world-space position.
// On success only positions and normals become mesh-LOCAL; projector values
// and angle opacity stay unchanged. On invalid transforms, leave untouched.
inline bool sprite_decal_geometry_to_receiver_local(
    gpu_3do_geometry &geometry,gpu_3do_matrix34 const &receiver_world)
{
    sprite_decal_follow_detail::inverse_affine inverse;
    if(!inverse.make(receiver_world))return false;
    gpu_3do_geometry converted=geometry;
    for(auto &v:converted.vertices) {
        if(!inverse.point(v.x,v.y,v.z) ||
           !inverse.normal(v.nx,v.ny,v.nz) ||
           !inverse.normal(v.lnx,v.lny,v.lnz))return false;
    }
    for(auto &p:converted.face_planes) {
        if(!inverse.point(p.anchor[0],p.anchor[1],p.anchor[2]) ||
           !inverse.normal(p.normal[0],p.normal[1],p.normal[2]))return false;
    }
    for(auto &group:converted.groups) {
        if(group.offset>converted.indices.size() ||
           group.count>converted.indices.size()-group.offset ||
           group.count==0)return false;
        group.bounds_min.fill(std::numeric_limits<float>::max());
        group.bounds_max.fill(-std::numeric_limits<float>::max());
        std::array<double,3> center{};
        for(uint32_t n=0;n<group.count;++n) {
            auto const index=converted.indices[group.offset+n];
            if(index>=converted.vertices.size())return false;
            auto const &v=converted.vertices[index];
            float const xyz[3]={v.x,v.y,v.z};
            for(int axis=0;axis<3;++axis) {
                group.bounds_min[axis]=std::min(group.bounds_min[axis],xyz[axis]);
                group.bounds_max[axis]=std::max(group.bounds_max[axis],xyz[axis]);
                center[axis]+=xyz[axis];
            }
        }
        for(int axis=0;axis<3;++axis)
            group.centroid[axis]=float(center[axis]/group.count);
    }
    geometry=std::move(converted);
    return true;
}
}
