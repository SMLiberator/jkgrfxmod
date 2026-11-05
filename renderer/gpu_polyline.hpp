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

// JKGM rdPolyline_Draw 0x00448D60, verified from the uploaded EXE:
// first three faces are THREE-VERTEX SideMat triangles sharing a vertex
// pool: [tip rim, hilt center, next tip rim]. The five-vertex BASE cap
// at face 3 begins with the hilt center plus four surrounding BASE
// points. The last four-vertex face (4) is the TIP cap.
// Do not reverse these or the delay follows the hilt instead of the tip.
inline bool gpu_polyline_cap_pose_camera(
    gpu_polyline_capture const &capture,
    std::array<float,3> &base,std::array<float,3> &tip)
{
    if(capture.failed || capture.faces.size()!=5U)return false;
    auto center=[](gpu_polyline_face const &face,unsigned start,
                   std::array<float,3> &out) {
        if(face.count<3U || face.count>5U || start>=face.count)return false;
        out={{0,0,0}};
        for(unsigned i=start;i<face.count;++i)
            for(int n=0;n<3;++n) {
                if(!std::isfinite(face.position[i][n]))return false;
                out[n]+=face.position[i][n]/float(face.count-start);
            }
        return true;
    };
    if(!center(capture.faces[3],capture.faces[3].count==5U?1U:0U,base) ||
       !center(capture.faces[4],0U,tip))
        return false;
    double len2=0.0;
    for(int n=0;n<3;++n) {
        double d=double(tip[n])-base[n];
        len2+=d*d;
    }
    return std::isfinite(len2) && len2>1.e-8 && len2<1.e10;
}

inline bool gpu_polyline_blade_pose(
    gpu_polyline_capture const &capture,gpu_3do_matrix34 const &inverse_view,
    std::array<float,3> &base_world,std::array<float,3> &tip_world)
{
    std::array<float,3> base{},tip{};
    if(!gpu_polyline_cap_pose_camera(capture,base,tip))return false;
    base_world=gpu_3do_native_transform_point(inverse_view,base);
    tip_world=gpu_3do_native_transform_point(inverse_view,tip);
    return std::all_of(base_world.begin(),base_world.end(),
                       [](float x){return std::isfinite(x);}) &&
           std::all_of(tip_world.begin(),tip_world.end(),
                       [](float x){return std::isfinite(x);});
}

// The source SideMat is split across two moving blade strips. Its center
// samples the bright core on a triangle between their *tip* positions.
// When the trailing tip reaches the current one the triangle collapses,
// with no fake opacity fade or persistent world-space smear.
// Preserve all five native faces exactly. When a swing is active, render a
// second, camera-facing blade around the historical *world-space* direction,
// then connect both centerlines with one truly three-dimensional triangle.
// Never move half of a native side face or rotate the historical tip toward
// the camera: doing either twists the trail and makes it behave like a billboard.
inline std::shared_ptr<gpu_3do_geometry const>
make_gpu_polyline_geometry(
    gpu_polyline_capture const &capture,
    std::array<float,3> const *trailing_tip_camera=nullptr)
{
    if(capture.failed || capture.faces.size()!=5U)return {};
    std::array<float,3> base_camera{},tip_camera{},tip_delta{};
    bool const have_trail=trailing_tip_camera &&
        gpu_polyline_cap_pose_camera(capture,base_camera,tip_camera) &&
        std::all_of(trailing_tip_camera->begin(),trailing_tip_camera->end(),
                    [](float v){return std::isfinite(v);});
    if(trailing_tip_camera && !have_trail)return {};
    if(have_trail) {
        // These are the verified JKGM five-face polyline shapes.
        // Unknown layouts are still drawn normally, without a trail.
        if(capture.faces[3].count!=5U || capture.faces[4].count!=4U)
            return {};
        for(unsigned i=0;i<3U;++i)
            if(capture.faces[i].count!=3U || capture.faces[i].geo!=4U)
                return {};
        for(int n=0;n<3;++n)
            tip_delta[n]=(*trailing_tip_camera)[n]-tip_camera[n];
    }

    auto result=std::make_shared<gpu_3do_geometry>();
    for(size_t face_index=0;face_index<capture.faces.size();++face_index) {
        auto const &face=capture.faces[face_index];
        if(face.count<3U || face.count>5U || !face.material ||
           (face.geo!=3U && face.geo!=4U))return {};
        gpu_3do_group group{};
        group.offset=static_cast<uint32_t>(result->indices.size());
        group.material=face.material;
        group.cel=face.cel;
        // A lightsaber has no stable outward-facing side.
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
        auto expand_bounds=[&](gpu_3do_vertex const &v) {
            float const p[3]={v.x,v.y,v.z};
            for(unsigned n=0;n<3U;++n) {
                group.bounds_min[n]=std::min(group.bounds_min[n],p[n]);
                group.bounds_max[n]=std::max(group.bounds_max[n],p[n]);
            }
        };
        uint32_t const begin=static_cast<uint32_t>(result->vertices.size());
        for(uint32_t k=0;k<face.count;++k) {
            auto const &p=face.position[k];
            auto const &uv=face.uv[k];
            for(float v:p)if(!std::isfinite(v))return {};
            for(float v:uv)if(!std::isfinite(v))return {};
            gpu_3do_vertex v{};
            v.x=p[0];v.y=p[1];v.z=p[2];
            // Preserve both native vertex positions and native MAT-pixel
            // UVs (including the live per-face animation offset).
            v.u=uv[0]+face.uv_offset[0];
            v.v=uv[1]+face.uv_offset[1];
            if(!std::isfinite(v.u) || !std::isfinite(v.v))return {};
            v.nx=v.lnx=nx;v.ny=v.lny=ny;v.nz=v.lnz=nz;
            v.fax=face.position[0][0];
            v.fay=face.position[0][1];
            v.faz=face.position[0][2];
            v.light_mode=0.0f;
            v.cr=v.cg=v.cb=1.0f;
            result->vertices.push_back(v);
            expand_bounds(v);
        }
        for(uint32_t k=1;k+1<face.count;++k)
            result->indices.insert(result->indices.end(),
                                   {begin,begin+k,begin+k+1});

        if(have_trail && face_index<3U) {
            // Each native SideMat triangle is
            // [tip_rim_0, hilt_center, tip_rim_1].
            // Copy the COMPLETE camera-facing blade, moving only its
            // two tip-rim vertices to the real delayed centerline. The
            // camera-facing rim offsets and native UVs remain unchanged.
            uint32_t const delayed_begin=
                static_cast<uint32_t>(result->vertices.size());
            for(unsigned k=0;k<3U;++k) {
                gpu_3do_vertex v=result->vertices[begin+k];
                if(k!=1U) {
                    v.x+=tip_delta[0];
                    v.y+=tip_delta[1];
                    v.z+=tip_delta[2];
                }
                result->vertices.push_back(v);
                expand_bounds(v);
            }
            result->indices.insert(result->indices.end(),
                                   {delayed_begin,delayed_begin+1U,
                                    delayed_begin+2U});

            if(face_index==1U) {
                // This is the only connecting triangle. It joins actual
                // hilt/current-tip/delayed-tip positions in camera space
                // (which are inverse-transformed back into world space by
                // the ordinary procedural GPU instance transform).
                // The SideMat center column retains the core color and
                // the native base-to-tip UV progression.
                float const center_u=
                    (face.uv[0][0]+face.uv[2][0])*0.5f+
                    face.uv_offset[0];
                float const center_v=
                    (face.uv[0][1]+face.uv[2][1])*0.5f+
                    face.uv_offset[1];
                auto const &past=*trailing_tip_camera;
                uint32_t const join_begin=
                    static_cast<uint32_t>(result->vertices.size());
                for(unsigned k=0;k<3U;++k) {
                    gpu_3do_vertex v=result->vertices[begin+1U];
                    auto const &p=k==0U?base_camera:
                                  (k==1U?tip_camera:past);
                    v.x=p[0];v.y=p[1];v.z=p[2];
                    if(k!=0U) {v.u=center_u;v.v=center_v;}
                    v.fax=base_camera[0];
                    v.fay=base_camera[1];
                    v.faz=base_camera[2];
                    result->vertices.push_back(v);
                    expand_bounds(v);
                }
                // Compute the geometric normal in its TRUE 3D plane,
                // not the camera-facing side plane.
                float const ax=tip_camera[0]-base_camera[0];
                float const ay=tip_camera[1]-base_camera[1];
                float const az=tip_camera[2]-base_camera[2];
                float const bx=past[0]-base_camera[0];
                float const by=past[1]-base_camera[1];
                float const bz=past[2]-base_camera[2];
                float jx=ay*bz-az*by;
                float jy=az*bx-ax*bz;
                float jz=ax*by-ay*bx;
                float const jmag=std::sqrt(jx*jx+jy*jy+jz*jz);
                if(std::isfinite(jmag) && jmag>1.e-8f) {
                    jx/=jmag;jy/=jmag;jz/=jmag;
                    for(unsigned k=0;k<3U;++k) {
                        auto &v=result->vertices[join_begin+k];
                        v.nx=v.lnx=jx;
                        v.ny=v.lny=jy;
                        v.nz=v.lnz=jz;
                    }
                }
                result->indices.insert(result->indices.end(),
                                       {join_begin,join_begin+1U,
                                        join_begin+2U});
            }
        } else if(have_trail && face_index==4U) {
            // The hilt cap is shared, but the trailing tip gets its own
            // camera-facing tip cap, retaining the native shape and UVs.
            uint32_t const delayed_begin=
                static_cast<uint32_t>(result->vertices.size());
            for(unsigned k=0;k<face.count;++k) {
                gpu_3do_vertex v=result->vertices[begin+k];
                v.x+=tip_delta[0];
                v.y+=tip_delta[1];
                v.z+=tip_delta[2];
                result->vertices.push_back(v);
                expand_bounds(v);
            }
            for(uint32_t k=1;k+1<face.count;++k)
                result->indices.insert(result->indices.end(),
                                       {delayed_begin,delayed_begin+k,
                                        delayed_begin+k+1});
        }
        // Include the delayed blade and fan in group sorting/culling bounds.
        group.centroid={{0,0,0}};
        float const inv_count=
            1.f/float(result->vertices.size()-size_t(begin));
        for(size_t i=begin;i<result->vertices.size();++i) {
            auto const &v=result->vertices[i];
            group.centroid[0]+=v.x*inv_count;
            group.centroid[1]+=v.y*inv_count;
            group.centroid[2]+=v.z*inv_count;
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
