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
    // JK's rdFace.texVertOffset, in the same source MAT-pixel UV units.
    // The native polyline generator advances this independently per face.
    std::array<float,2> uv_offset{};
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

// Cap centers come from JKGM's captured generator output, not guessed
// gameplay joint offsets. Both caps are in the same native camera space.
inline bool gpu_polyline_blade_pose(
    gpu_polyline_capture const &capture,gpu_3do_matrix34 const &inverse_view,
    std::array<float,3> &base_world,std::array<float,3> &tip_world)
{
    if(capture.failed || capture.faces.size()!=5U)return false;
    auto center=[](gpu_polyline_face const &face,std::array<float,3> &out) {
        if(face.count<3 || face.count>5)return false;
        out={{0,0,0}};
        for(uint32_t i=0;i<face.count;++i)
            for(int n=0;n<3;++n) {
                if(!std::isfinite(face.position[i][n]))return false;
                out[n]+=face.position[i][n]/float(face.count);
            }
        return true;
    };
    std::array<float,3> base{},tip{};
    if(!center(capture.faces[4],base) ||
       !center(capture.faces[3],tip))return false;
    base_world=gpu_3do_native_transform_point(inverse_view,base);
    tip_world=gpu_3do_native_transform_point(inverse_view,tip);
    double len2=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(base_world[i]) || !std::isfinite(tip_world[i]))
            return false;
        len2+=double(tip_world[i]-base_world[i])*
              double(tip_world[i]-base_world[i]);
    }
    return len2>1.e-8 && len2<1.e10;
}

// The source SideMat is split across two moving blade strips. Its center
// samples the bright core on a triangle between their *tip* positions.
// When the trailing tip reaches the current one the triangle collapses,
// with no fake opacity fade or persistent world-space smear.
inline std::shared_ptr<gpu_3do_geometry const>
make_gpu_polyline_geometry(
    gpu_polyline_capture const &capture,
    std::array<float,3> const *trailing_tip_camera=nullptr)
{
    if(capture.failed || capture.faces.size()!=5U)return {};
    auto result=std::make_shared<gpu_3do_geometry>();
    for(size_t face_index=0;face_index<capture.faces.size();++face_index) {
        auto const &face=capture.faces[face_index];
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
        for(float offset:face.uv_offset)
            if(!std::isfinite(offset))return {};
        bool const split=trailing_tip_camera && face_index<3 &&
            face.count==4U && face.geo==4U;
        uint32_t const begin=static_cast<uint32_t>(result->vertices.size());
        for(uint32_t k=0;k<face.count;++k) {
            auto const &p=face.position[k];
            auto const &uv=face.uv[k];
            for(float v:p)if(!std::isfinite(v))return {};
            for(float v:uv)if(!std::isfinite(v))return {};
            gpu_3do_vertex v{};
            v.x=p[0];v.y=p[1];v.z=p[2];
            // Native rdPrimit3_ClipFace does this addition before clipping.
            // Keep its sign and units (no extra normalization or random UV
            // animation), so the ordinary GPU MAT path handles the rest.
            // Current blade samples SideMat's first half, leaving the
            // other half for the delayed edge. End caps stay untouched.
            float u=uv[0];
            if(split && k==1)u=(face.uv[0][0]+face.uv[1][0])*0.5f;
            if(split && k==2)u=(face.uv[2][0]+face.uv[3][0])*0.5f;
            v.u=u+face.uv_offset[0];
            v.v=uv[1]+face.uv_offset[1];
            if(!std::isfinite(v.u) || !std::isfinite(v.v))return {};
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
        if(split) {
            std::array<float,3> current_tip{},base{},offset{};
            for(int n=0;n<3;++n) {
                current_tip[n]=(face.position[0][n]+face.position[1][n])*0.5f;
                base[n]=(face.position[2][n]+face.position[3][n])*0.5f;
                offset[n]=(*trailing_tip_camera)[n]-current_tip[n];
            }
            // One delayed blade quad per original side face. Its base
            // remains attached to the current hilt: only its tip lags.
            uint32_t const trailing_begin=
                static_cast<uint32_t>(result->vertices.size());
            for(uint32_t k=0;k<4U;++k) {
                gpu_3do_vertex v=result->vertices[begin+k];
                if(k<2U) {
                    v.x+=offset[0];v.y+=offset[1];v.z+=offset[2];
                }
                if(k==0U) v.u=(face.uv[0][0]+face.uv[1][0])*0.5f+
                                 face.uv_offset[0];
                if(k==1U) v.u=face.uv[1][0]+face.uv_offset[0];
                if(k==2U) v.u=face.uv[2][0]+face.uv_offset[0];
                if(k==3U) v.u=(face.uv[2][0]+face.uv[3][0])*0.5f+
                                 face.uv_offset[0];
                result->vertices.push_back(v);
                for(int n=0;n<3;++n) {
                    float const p=n==0?v.x:n==1?v.y:v.z;
                    group.bounds_min[n]=std::min(group.bounds_min[n],p);
                    group.bounds_max[n]=std::max(group.bounds_max[n],p);
                }
            }
            result->indices.insert(result->indices.end(),{
                trailing_begin,trailing_begin+1U,trailing_begin+2U,
                trailing_begin,trailing_begin+2U,trailing_begin+3U});
            // JKGM has THREE sides around ONE blade centerline. Their fan
            // centers are identical, so draw the shared body just once:
            // otherwise three coplanar triangles compete for depth.
            if(face_index==0U) {
            // Fan core: U stays at the SideMat center (white); V follows
            // the same base-to-tip mapping as the native side quad.
            uint32_t const fan_begin=
                static_cast<uint32_t>(result->vertices.size());
            for(int k=0;k<3;++k) {
                gpu_3do_vertex v=result->vertices[begin+(k==0?2U:0U)];
                auto const &p=k==0?base:
                    (k==1?current_tip:*trailing_tip_camera);
                v.x=p[0];v.y=p[1];v.z=p[2];
                v.u=(face.uv[k==0?2U:0U][0]+
                     face.uv[k==0?3U:1U][0])*0.5f+face.uv_offset[0];
                result->vertices.push_back(v);
                for(int n=0;n<3;++n) {
                    group.bounds_min[n]=std::min(group.bounds_min[n],p[n]);
                    group.bounds_max[n]=std::max(group.bounds_max[n],p[n]);
                }
            }
            result->indices.insert(result->indices.end(),{
                fan_begin,fan_begin+1U,fan_begin+2U});
            }
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
