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
#include <limits>
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

// JKGM emits three SideMat triangles and two caps (five and four
// vertices). Do not decide which cap is the hilt by FACE INDEX: the actual
// saber origin is rdPolyline_Draw's orientation translation. Comparing both
// cap centers with that origin makes the result independent of cap order.
// With no origin supplied, keep the legacy face-3-base interpretation only
// for standalone callers that cannot access the native orientation.
inline bool gpu_polyline_cap_pose_camera(
    gpu_polyline_capture const &capture,
    std::array<float,3> &base,std::array<float,3> &tip,
    std::array<float,3> const *native_hilt_camera=nullptr,
    unsigned *tip_face_index=nullptr)
{
    if(capture.failed || capture.faces.size()!=5U ||
       capture.faces[3].count!=5U || capture.faces[4].count!=4U)
        return false;
    auto cap_center=[](gpu_polyline_face const &face,
                       std::array<float,3> &out) {
        if(face.count!=4U && face.count!=5U)return false;
        for(unsigned k=0;k<face.count;++k)
            for(float v:face.position[k])
                if(!std::isfinite(v))return false;
        out={{0,0,0}};
        if(face.count==4U) {
            for(unsigned k=0;k<4U;++k)
                for(int n=0;n<3;++n)
                    out[n]+=face.position[k][n]*0.25f;
        } else {
            // The five-point cap has one center plus four rim points.
            // Locate the central point geometrically, not by its index.
            unsigned central=0U;
            double best=std::numeric_limits<double>::infinity();
            for(unsigned skip=0;skip<5U;++skip) {
                std::array<double,3> rim{};
                for(unsigned k=0;k<5U;++k)
                    if(k!=skip)
                        for(int n=0;n<3;++n)
                            rim[n]+=double(face.position[k][n])*0.25;
                double distance=0.0;
                for(int n=0;n<3;++n) {
                    double const d=face.position[skip][n]-rim[n];
                    distance+=d*d;
                }
                if(distance<best){best=distance;central=skip;}
            }
            for(unsigned k=0;k<5U;++k)
                if(k!=central)
                    for(int n=0;n<3;++n)
                        out[n]+=face.position[k][n]*0.25f;
        }
        return true;
    };
    std::array<float,3> cap3{},cap4{};
    if(!cap_center(capture.faces[3],cap3) ||
       !cap_center(capture.faces[4],cap4))return false;

    bool tip_is_face3=false;
    if(native_hilt_camera) {
        double d3=0.0,d4=0.0;
        for(int n=0;n<3;++n) {
            if(!std::isfinite((*native_hilt_camera)[n]))return false;
            double const a=cap3[n]-(*native_hilt_camera)[n];
            double const b=cap4[n]-(*native_hilt_camera)[n];
            d3+=a*a;d4+=b*b;
        }
        // Anchor BOTH blade centerlines to JK's *actual* hilt origin.
        base=*native_hilt_camera;
        tip_is_face3=d3>d4;
        tip=tip_is_face3?cap3:cap4;
    } else {
        base=cap3;
        tip=cap4;
    }
    if(tip_face_index)*tip_face_index=tip_is_face3?3U:4U;
    double len2=0.0;
    for(int n=0;n<3;++n) {
        double const d=double(tip[n])-base[n];
        len2+=d*d;
    }
    return std::isfinite(len2) && len2>1.e-8 && len2<1.e10;
}

inline bool gpu_polyline_blade_pose(
    gpu_polyline_capture const &capture,gpu_3do_matrix34 const &inverse_view,
    std::array<float,3> &base_world,std::array<float,3> &tip_world,
    std::array<float,3> const *native_hilt_camera=nullptr)
{
    std::array<float,3> base{},tip{};
    if(!gpu_polyline_cap_pose_camera(
           capture,base,tip,native_hilt_camera))return false;
    base_world=gpu_3do_native_transform_point(inverse_view,base);
    tip_world=gpu_3do_native_transform_point(inverse_view,tip);
    return std::all_of(base_world.begin(),base_world.end(),
                       [](float x){return std::isfinite(x);}) &&
           std::all_of(tip_world.begin(),tip_world.end(),
                       [](float x){return std::isfinite(x);});
}

// The live native blade is never modified. On a swing, move ONLY the
// historical tip vertices; every base vertex remains at the live hilt.
// The sole connecting triangle is genuinely three-dimensional.

inline std::shared_ptr<gpu_3do_geometry const>
make_gpu_polyline_geometry(
    gpu_polyline_capture const &capture,
    std::array<float,3> const *trailing_tip_camera=nullptr,
    std::array<float,3> const *native_hilt_camera=nullptr)
{
    if(capture.failed || capture.faces.size()!=5U)return {};
    std::array<float,3> base_camera{},tip_camera{},tip_delta{};
    unsigned tip_cap_face_index=4U;
    bool const have_trail=trailing_tip_camera &&
        gpu_polyline_cap_pose_camera(
            capture,base_camera,tip_camera,native_hilt_camera,
            &tip_cap_face_index) &&
        std::all_of(trailing_tip_camera->begin(),trailing_tip_camera->end(),
                    [](float v){return std::isfinite(v);});
    if(trailing_tip_camera && !have_trail)return {};
    // Classify each side vertex against the actual cap centers instead of
    // assuming a fixed triangle vertex order. Two tip rims + one base
    // center and two base rims + one tip center are both supported.
    std::array<std::array<bool,3>,3> is_tip{};
    if(have_trail) {
        for(unsigned fi=0;fi<3U;++fi) {
            auto const &face=capture.faces[fi];
            if(face.count!=3U || face.geo!=4U)return {};
            unsigned tips=0U;
            for(unsigned k=0;k<3U;++k) {
                double base2=0.0,tip2=0.0;
                for(int n=0;n<3;++n) {
                    double const x=face.position[k][n];
                    if(!std::isfinite(x))return {};
                    double const bd=x-base_camera[n];
                    double const td=x-tip_camera[n];
                    base2+=bd*bd;tip2+=td*td;
                }
                is_tip[fi][k]=tip2<base2;
                if(is_tip[fi][k])++tips;
            }
            // A malformed/unknown side layout must not produce a flying
            // blade. The original native faces are still renderable.
            if(tips==0U || tips==3U)return {};
        }
        for(int n=0;n<3;++n)
            tip_delta[n]=(*trailing_tip_camera)[n]-tip_camera[n];
    }

    auto result=std::make_shared<gpu_3do_geometry>();
    std::array<uint32_t,3> join_triangle_indices{};
    std::array<uint32_t,3> live_side_starts{};
    uint32_t join_vertex_start=0;
    bool have_join_triangle=false;
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
        if(face_index<3U)live_side_starts[face_index]=begin;
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
        // Keep the lagging blade and its tip cap behind the live blade.
        // The genuinely 3D connector is submitted as the final draw group,
        // AFTER both blades and caps (independent of SideMat/TipMat).
        std::vector<uint32_t> behind_indices;
        if(have_trail && face_index<3U) {
            uint32_t const delayed_begin=
                static_cast<uint32_t>(result->vertices.size());
            for(unsigned k=0;k<3U;++k) {
                gpu_3do_vertex v=result->vertices[begin+k];
                if(is_tip[face_index][k]) {
                    v.x+=tip_delta[0];
                    v.y+=tip_delta[1];
                    v.z+=tip_delta[2];
                }
                result->vertices.push_back(v);
                expand_bounds(v);
            }
            behind_indices={delayed_begin,delayed_begin+1U,
                            delayed_begin+2U};

            if(face_index==1U) {
                auto const &past=*trailing_tip_camera;
                // Use the actual source UV for the anchored base and the
                // average of the tip rim UVs. Tip and base must not swap:
                // the texture's bright core should travel along the blade,
                // not create an opaque wall at the hilt.
                std::array<float,2> base_uv{},tip_uv{};
                unsigned base_count=0U,tip_count=0U;
                for(unsigned k=0;k<3U;++k) {
                    auto &dst=is_tip[face_index][k]?tip_uv:base_uv;
                    dst[0]+=face.uv[k][0]+face.uv_offset[0];
                    dst[1]+=face.uv[k][1]+face.uv_offset[1];
                    if(is_tip[face_index][k])++tip_count;
                    else ++base_count;
                }
                if(!base_count || !tip_count)return {};
                for(int n=0;n<2;++n) {
                    base_uv[n]/=float(base_count);
                    tip_uv[n]/=float(tip_count);
                }
                uint32_t const join_begin=
                    static_cast<uint32_t>(result->vertices.size());
                unsigned const template_vertex=
                    !is_tip[face_index][0]?0U:
                    (!is_tip[face_index][1]?1U:2U);
                for(unsigned k=0;k<3U;++k) {
                    gpu_3do_vertex v=result->vertices[begin+template_vertex];
                    auto const &pt=k==0U?base_camera:
                                   (k==1U?tip_camera:past);
                    v.x=pt[0];v.y=pt[1];v.z=pt[2];
                    auto const &uv=k==0U?base_uv:tip_uv;
                    v.u=uv[0];v.v=uv[1];
                    v.fax=base_camera[0];
                    v.fay=base_camera[1];
                    v.faz=base_camera[2];
                    result->vertices.push_back(v);
                    expand_bounds(v);
                }
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
                join_triangle_indices={{join_begin,join_begin+1U,
                                        join_begin+2U}};
                join_vertex_start=join_begin;
                have_join_triangle=true;
            }
        }
        // The physical tip cap (4 or 5 vertices) gets its own delayed
        // copy. Select it from the native hilt, NEVER by a fixed face
        // index: copying the base cap would reintroduce the flying quad.
        // The copied cap uses the same camera-facing rim and original UVs.
        if(have_trail && face_index==tip_cap_face_index) {
            uint32_t const past_cap_begin=
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
                behind_indices.insert(behind_indices.end(),
                                      {past_cap_begin,past_cap_begin+k,
                                       past_cap_begin+k+1});
        }
        result->indices.insert(result->indices.end(),
                               behind_indices.begin(),behind_indices.end());
        for(uint32_t k=1;k+1<face.count;++k)
            result->indices.insert(result->indices.end(),
                                   {begin,begin+k,begin+k+1});
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
    // Draw each physical blade and its cap before the joining triangle.
    // Combining the SideMat ranges keeps their alpha blending in native
    // delayed -> live order instead of sorting each narrow strip separately.
    if(have_trail && have_join_triangle) {
        if(result->groups.size()==5U) {
            auto const &g0=result->groups[0];
            auto const &g1=result->groups[1];
            auto const &g2=result->groups[2];
            bool const common_side_material=
                g0.count==6U && g1.count==6U && g2.count==6U &&
                g0.offset==0U && g1.offset==6U && g2.offset==12U &&
                g0.material==g1.material && g1.material==g2.material &&
                g0.cel==g1.cel && g1.cel==g2.cel &&
                g0.face_type==g1.face_type &&
                g1.face_type==g2.face_type &&
                g0.solid_color==g1.solid_color &&
                g1.solid_color==g2.solid_color;
            if(common_side_material) {
                std::array<uint32_t,18> combined{};
                unsigned next=0U;
                auto copy_three=[&](size_t offset) {
                    for(unsigned k=0;k<3U;++k)
                        combined[next++]=result->indices[offset+k];
                };
                copy_three(0U);  // historical side 0
                copy_three(6U);  // historical side 1
                copy_three(12U); // historical side 2
                copy_three(3U);  // live side 0
                copy_three(9U);  // live side 1
                copy_three(15U); // live side 2
                std::copy(combined.begin(),combined.end(),
                          result->indices.begin());
                auto &group=result->groups[0];
                group.count=18U;
                group.centroid={{0,0,0}};
                for(unsigned i=0;i<21U;++i) {
                    auto const &v=result->vertices[i];
                    group.centroid[0]+=v.x/21.f;
                    group.centroid[1]+=v.y/21.f;
                    group.centroid[2]+=v.z/21.f;
                }
                for(unsigned gi=1U;gi<=2U;++gi)
                    for(int n=0;n<3;++n) {
                        group.bounds_min[n]=std::min(
                            group.bounds_min[n],
                            result->groups[gi].bounds_min[n]);
                        group.bounds_max[n]=std::max(
                            group.bounds_max[n],
                            result->groups[gi].bounds_max[n]);
                    }
                result->groups.erase(result->groups.begin()+1,
                                     result->groups.begin()+3);
            }
        }

        // Tip-bridge strip: SideMat's vertical UV axis runs from the
        // HISTORICAL tip (source blade BASE V) to the LIVE tip (source
        // blade TIP V). U is copied from native SideMat tip-rim vertices;
        // all UVs include the native live scrolling offset. TipMat's small
        // circular square is exclusively for the two separate tip caps.
        //
        // Select the widest projected pair of native tip-rim vertices from
        // ONE side face. The side of the blade can have multiple fan faces,
        // but all three use the same Thing's SideMat. Prefer a rim pair that
        // also maximizes the projected area of the connecting bridge.
        int best_face=-1;
        unsigned rim0=0U,rim1=0U,base_k=0U;
        float best_area=-1.f,best_width=-1.f;
        for(unsigned fi=0;fi<3U;++fi) {
            unsigned tip_count=0U;
            std::array<unsigned,2> tips{};
            unsigned base_index=0U;
            for(unsigned k=0;k<3U;++k) {
                if(is_tip[fi][k]) {
                    if(tip_count<2U)tips[tip_count]=k;
                    ++tip_count;
                } else base_index=k;
            }
            if(tip_count!=2U)continue;
            auto const &a=capture.faces[fi].position[tips[0]];
            auto const &b=capture.faces[fi].position[tips[1]];
            float const dx=a[0]-b[0],dz=a[2]-b[2];
            float const width2=dx*dx+dz*dz;
            float const area=std::abs(dx*tip_delta[2]-dz*tip_delta[0]);
            if(width2>1.e-10f &&
               (area>best_area+1.e-8f ||
                (std::abs(area-best_area)<=1.e-8f &&
                 width2>best_width))) {
                best_face=int(fi);
                rim0=tips[0];rim1=tips[1];base_k=base_index;
                best_area=area;best_width=width2;
            }
        }
        if(best_face>=0) {
            unsigned const source=unsigned(best_face);
            auto const &side=capture.faces[source];
            auto const &native0=result->vertices[
                live_side_starts[source]+rim0];
            auto const &native1=result->vertices[
                live_side_starts[source]+rim1];
            float const base_v=result->vertices[
                live_side_starts[source]+base_k].v;

            std::array<float,3> current0{{native0.x,native0.y,native0.z}};
            std::array<float,3> current1{{native1.x,native1.y,native1.z}};
            // If the native camera-facing rim aligns with the swing, the
            // bridge would have zero area. Use the SAME rim width rotated
            // perpendicular to the tip displacement in the camera X/Z
            // plane. Only the bridge is adjusted, never the live blades.
            float const projected_length2=
                tip_delta[0]*tip_delta[0]+tip_delta[2]*tip_delta[2];
            if(best_area<1.e-7f && projected_length2>1.e-10f) {
                float const half_width=std::sqrt(best_width)*0.5f;
                float const inv=1.f/std::sqrt(projected_length2);
                float const side_x=-tip_delta[2]*inv*half_width;
                float const side_z= tip_delta[0]*inv*half_width;
                current0={{tip_camera[0]-side_x,tip_camera[1],
                           tip_camera[2]-side_z}};
                current1={{tip_camera[0]+side_x,tip_camera[1],
                           tip_camera[2]+side_z}};
            }
            // Some native SideMat fan faces use a tip-rim pair whose
            // midpoint is slightly offset from the true cap center. Center
            // only the joining strip on the verified tip so its near/far
            // rims meet BOTH physical tip centers. Keep the rim width,
            // SideMat UVs, and the real/delayed blade vertices untouched.
            for(int n=0;n<3;++n) {
                float const rim_center=(current0[n]+current1[n])*0.5f;
                float const correction=tip_camera[n]-rim_center;
                current0[n]+=correction;
                current1[n]+=correction;
            }
            // Order: virtual rim 0/1 -> live rim 1/0. The bridge uses
            // exactly one rectangle worth of SideMat, with the native
            // U endpoints and full native BASE->TIP V progression.
            std::array<gpu_3do_vertex,4> strip{{
                native0,native1,native1,native0
            }};
            auto write_pos=[](gpu_3do_vertex &v,
                              std::array<float,3> const &p) {
                v.x=p[0];v.y=p[1];v.z=p[2];
            };
            std::array<float,3> past0=current0,past1=current1;
            for(int n=0;n<3;++n) {
                past0[n]+=tip_delta[n];past1[n]+=tip_delta[n];
            }
            write_pos(strip[0],past0);
            write_pos(strip[1],past1);
            write_pos(strip[2],current1);
            write_pos(strip[3],current0);
            strip[0].v=base_v;
            strip[1].v=base_v;
            // strip[2].v and strip[3].v are original native TIP V,
            // including the scrolling offset. No texture height assumed.
            float const ax=strip[1].x-strip[0].x;
            float const ay=strip[1].y-strip[0].y;
            float const az=strip[1].z-strip[0].z;
            float const bx=strip[2].x-strip[0].x;
            float const by=strip[2].y-strip[0].y;
            float const bz=strip[2].z-strip[0].z;
            float nx=ay*bz-az*by;
            float ny=az*bx-ax*bz;
            float nz=ax*by-ay*bx;
            float const normal_length=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(std::isfinite(normal_length) && normal_length>1.e-8f) {
                nx/=normal_length;ny/=normal_length;nz/=normal_length;
                gpu_3do_group bridge_group{};
                bridge_group.material=side.material;
                bridge_group.cel=side.cel;
                bridge_group.face_type=side.face_type|1U;
                bridge_group.solid_color=false;
                bridge_group.saber_trail_tip_bridge=true;
                bridge_group.offset=
                    static_cast<uint32_t>(result->indices.size());
                bridge_group.count=6U;
                bridge_group.bounds_min={{strip[0].x,strip[0].y,strip[0].z}};
                bridge_group.bounds_max=bridge_group.bounds_min;
                uint32_t const bridge_begin=
                    static_cast<uint32_t>(result->vertices.size());
                for(auto &v:strip) {
                    v.nx=v.lnx=nx;v.ny=v.lny=ny;v.nz=v.lnz=nz;
                    v.fax=past0[0];v.fay=past0[1];v.faz=past0[2];
                    result->vertices.push_back(v);
                    float const p[3]={v.x,v.y,v.z};
                    for(int n=0;n<3;++n) {
                        bridge_group.bounds_min[n]=std::min(
                            bridge_group.bounds_min[n],p[n]);
                        bridge_group.bounds_max[n]=std::max(
                            bridge_group.bounds_max[n],p[n]);
                        bridge_group.centroid[n]+=p[n]*0.25f;
                    }
                }
                result->indices.insert(result->indices.end(),{
                    bridge_begin,bridge_begin+1U,bridge_begin+2U,
                    bridge_begin,bridge_begin+2U,bridge_begin+3U
                });
                result->groups.push_back(bridge_group);
            }
        }

        // A dedicated SideMat group is essential here: a fan appended
        // inside the side range still renders before the TipMat cap when
        // the renderer separately sorts translucent material groups.
        auto join_group=result->groups[0];
        join_group.material=capture.faces[1].material;
        join_group.cel=capture.faces[1].cel;
        join_group.face_type=capture.faces[1].face_type|1U;
        join_group.solid_color=capture.faces[1].geo==3U;
        join_group.offset=static_cast<uint32_t>(result->indices.size());
        join_group.count=3U;
        join_group.saber_trail_join=true;
        join_group.bounds_min={{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()}};
        join_group.bounds_max={{
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max(),
            -std::numeric_limits<float>::max()}};
        join_group.centroid={{0,0,0}};
        for(uint32_t k=0;k<3U;++k) {
            auto const &v=result->vertices[join_vertex_start+k];
            float const p[3]={v.x,v.y,v.z};
            for(int n=0;n<3;++n) {
                join_group.centroid[n]+=p[n]/3.f;
                join_group.bounds_min[n]=std::min(join_group.bounds_min[n],p[n]);
                join_group.bounds_max[n]=std::max(join_group.bounds_max[n],p[n]);
            }
        }
        result->indices.insert(result->indices.end(),
                               join_triangle_indices.begin(),
                               join_triangle_indices.end());
        result->groups.push_back(join_group);
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
