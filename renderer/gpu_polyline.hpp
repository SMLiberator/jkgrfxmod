#pragma once
// GPU-owned polyline rendering: capture JKGM's five generated procedural
// faces before rdCache submission. Geometry and UVs are taken from the
// executable, not approximated from OpenJKDF2's three-face saber.
#include "gpu_3do_preview.hpp"
#include "gpu_3do_modelview.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace jkgm {
struct gpu_polyline_face {
    uint32_t count=0,geo=4,face_type=0;
    uintptr_t material=0;
    int32_t cel=-1;
    std::array<std::array<float,3>,5> position{};
    std::array<std::array<float,2>,5> uv{};
};
struct gpu_polyline_capture {
    std::vector<gpu_polyline_face> faces;
    bool failed=false;
};
// Native rdMatrix34 column-major R/L/U+position inverse. The captured
// vertices are already in JK's camera-space XYZ; the inverse world matrix
// cancels the camera*instance composition in the existing GPU vertex stage.
inline bool gpu_polyline_inverse_view(
    gpu_3do_matrix34 const &v,gpu_3do_matrix34 &out)
{
    if(!gpu_3do_matrix_finite(v))return false;
    double const a=v[0],b=v[3],c=v[6];
    double const d=v[1],e=v[4],f=v[7];
    double const g=v[2],h=v[5],i=v[8];
    double const det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!std::isfinite(det) || std::abs(det)<1.e-9)return false;
    double const inv=1.0/det;
    double const m[9]={
        (e*i-f*h)*inv,(c*h-b*i)*inv,(b*f-c*e)*inv,
        (f*g-d*i)*inv,(a*i-c*g)*inv,(c*d-a*f)*inv,
        (d*h-e*g)*inv,(b*g-a*h)*inv,(a*e-b*d)*inv
    };
    // m[] is row-major; rdMatrix34 stores successive column vectors.
    out={{float(m[0]),float(m[3]),float(m[6]),
          float(m[1]),float(m[4]),float(m[7]),
          float(m[2]),float(m[5]),float(m[8]),0,0,0}};
    for(int k=0;k<3;++k)
        out[9+k]=float(-(m[k*3+0]*v[9]+
                         m[k*3+1]*v[10]+m[k*3+2]*v[11]));
    return gpu_3do_matrix_finite(out);
}

inline std::shared_ptr<gpu_3do_geometry const>
make_gpu_polyline_geometry(gpu_polyline_capture const &capture)
{
    if(capture.failed || capture.faces.size()!=5U)return {};
    auto result=std::make_shared<gpu_3do_geometry>();
    for(auto const &face:capture.faces) {
        if(face.count<3U || face.count>5U || !face.material ||
           (face.geo!=3U && face.geo!=4U))return {};
        gpu_3do_group group{};
        group.offset=static_cast<uint32_t>(result->indices.size());
        group.material=face.material;
        group.cel=face.cel;
        // Native polyline sides and caps can face the camera from either
        // winding; this is not a closed 3DO mesh with a fixed outward face.
        group.face_type=face.face_type|1U;
        group.solid_color=face.geo==3U;
        group.bounds_min=face.position[0];
        group.bounds_max=face.position[0];
        float nx=0,ny=-1,nz=0;
        {
            auto const &a=face.position[0];
            auto const &b=face.position[1];
            auto const &c=face.position[2];
            float const ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2];
            float const vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
            nx=uy*vz-uz*vy;ny=uz*vx-ux*vz;nz=ux*vy-uy*vx;
            float const mag=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(std::isfinite(mag) && mag>1.e-8f) {
                nx/=mag;ny/=mag;nz/=mag;
            } else {nx=0;ny=-1;nz=0;}
        }
        uint32_t const begin=static_cast<uint32_t>(result->vertices.size());
        for(uint32_t k=0;k<face.count;++k) {
            auto const &p=face.position[k];
            auto const &uv=face.uv[k];
            for(float v:p)if(!std::isfinite(v))return {};
            for(float v:uv)if(!std::isfinite(v))return {};
            gpu_3do_vertex v{};
            v.x=p[0];v.y=p[1];v.z=p[2];
            v.u=uv[0];v.v=uv[1];
            v.nx=v.lnx=nx;v.ny=v.lny=ny;v.nz=v.lnz=nz;
            v.fax=face.position[0][0];
            v.fay=face.position[0][1];
            v.faz=face.position[0][2];
            v.light_mode=0.0f; // lightsaber is fully lit
            v.cr=v.cg=v.cb=1.0f;
            result->vertices.push_back(v);
            for(unsigned n=0;n<3;++n) {
                group.centroid[n]+=p[n]/float(face.count);
            }
            for(unsigned n=0;n<3;++n) {
                group.bounds_min[n]=std::min(group.bounds_min[n],p[n]);
                group.bounds_max[n]=std::max(group.bounds_max[n],p[n]);
            }
        }
        for(uint32_t k=1;k+1<face.count;++k) {
            result->indices.push_back(begin);
            result->indices.push_back(begin+k);
            result->indices.push_back(begin+k+1);
        }
        group.count=static_cast<uint32_t>(result->indices.size())-group.offset;
        result->groups.push_back(group);
        auto const ref=std::make_pair(group.material,group.cel);
        if(group.solid_color)result->solid_material_refs.push_back(ref);
        else result->material_refs.push_back(ref);
    }
    auto deduplicate=[](auto &refs) {
        std::sort(refs.begin(),refs.end());
        refs.erase(std::unique(refs.begin(),refs.end()),refs.end());
    };
    deduplicate(result->material_refs);
    deduplicate(result->solid_material_refs);
    return result;
}
} // namespace jkgm
