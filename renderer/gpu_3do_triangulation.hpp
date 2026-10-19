#pragma once
// Triangulate arbitrary simple 3DO face outlines without assuming convexity.
// Geometry is cached only when a reliable, non-self-intersecting polygon can
// be triangulated. The caller must use JK's original face renderer otherwise.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace jkgm {
    struct gpu_3do_xy {
        double x=0.0, y=0.0;
    };

    inline double gpu_3do_cross(gpu_3do_xy a,gpu_3do_xy b,gpu_3do_xy c)
    {
        return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
    }

    inline bool gpu_3do_segments_intersect(
        gpu_3do_xy a,gpu_3do_xy b,gpu_3do_xy c,gpu_3do_xy d,
        double length_epsilon,double area_epsilon)
    {
        if(std::max(a.x,b.x)+length_epsilon<std::min(c.x,d.x) ||
           std::max(c.x,d.x)+length_epsilon<std::min(a.x,b.x) ||
           std::max(a.y,b.y)+length_epsilon<std::min(c.y,d.y) ||
           std::max(c.y,d.y)+length_epsilon<std::min(a.y,b.y))
            return false;
        double const ab_c=gpu_3do_cross(a,b,c);
        double const ab_d=gpu_3do_cross(a,b,d);
        double const cd_a=gpu_3do_cross(c,d,a);
        double const cd_b=gpu_3do_cross(c,d,b);
        return ((ab_c<=area_epsilon && ab_d>=-area_epsilon) ||
                (ab_c>=-area_epsilon && ab_d<=area_epsilon)) &&
               ((cd_a<=area_epsilon && cd_b>=-area_epsilon) ||
                (cd_a>=-area_epsilon && cd_b<=area_epsilon));
    }

    // Triangles contain indices into the ORIGINAL polygon's face corners,
    // keeping independent MAT UVs, vertex colors and smooth normals intact.
    // Nonplanar but otherwise simple faces remain supported. The caller must
    // disable per-triangle GL face culling for them while retaining JK's
    // original per-face camera/normal rejection.
    inline bool gpu_3do_triangulate_face(
        std::vector<std::array<float,3>> const &points,
        std::array<float,3> const &face_normal,
        std::vector<std::array<uint32_t,3>> &triangles,
        bool &nonplanar)
    {
        triangles.clear();
        nonplanar=false;
        if(points.size()<3 || points.size()>128) return false;
        for(auto const &p:points)
            for(float x:p) if(!std::isfinite(x)) return false;
        double normal_length2=0.0;
        for(float f:face_normal) {
            if(!std::isfinite(f)) return false;
            normal_length2+=double(f)*f;
        }
        if(normal_length2<1e-20) return false;

        std::array<double,3> newell{};
        std::array<double,3> lo{{points[0][0],points[0][1],points[0][2]}};
        auto hi=lo;
        for(size_t i=0;i<points.size();i++) {
            auto const &a=points[i],&b=points[(i+1)%points.size()];
            newell[0]+=(double(a[1])-b[1])*(double(a[2])+b[2]);
            newell[1]+=(double(a[2])-b[2])*(double(a[0])+b[0]);
            newell[2]+=(double(a[0])-b[0])*(double(a[1])+b[1]);
            for(int k=0;k<3;k++) {
                lo[k]=std::min(lo[k],double(a[k]));
                hi[k]=std::max(hi[k],double(a[k]));
            }
        }
        double const span=std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]});
        if(span<=1e-10) return false;
        double const nl=std::sqrt(newell[0]*newell[0]+
                                  newell[1]*newell[1]+newell[2]*newell[2]);
        if(nl<=span*span*1e-10) return false;
        int const drop=std::abs(newell[0])>std::abs(newell[1])
            ? (std::abs(newell[0])>std::abs(newell[2])?0:2)
            : (std::abs(newell[1])>std::abs(newell[2])?1:2);
        std::vector<gpu_3do_xy> projected;
        projected.reserve(points.size());
        for(auto const &p:points) {
            if(drop==0) projected.push_back({p[1],p[2]});
            else if(drop==1) projected.push_back({p[0],p[2]});
            else projected.push_back({p[0],p[1]});
            double const height=((double(p[0])-points[0][0])*newell[0]+
                                 (double(p[1])-points[0][1])*newell[1]+
                                 (double(p[2])-points[0][2])*newell[2])/nl;
            if(std::abs(height)>span*1e-4) nonplanar=true;
        }
        double px_min=projected[0].x,px_max=px_min;
        double py_min=projected[0].y,py_max=py_min;
        for(auto const &p:projected) {
            px_min=std::min(px_min,p.x);px_max=std::max(px_max,p.x);
            py_min=std::min(py_min,p.y);py_max=std::max(py_max,p.y);
        }
        double const planar_span=std::max(px_max-px_min,py_max-py_min);
        double const eps=std::max(1e-24,planar_span*planar_span*1e-10);
        double const length_eps=std::max(1e-12,planar_span*1e-9);
        std::vector<uint32_t> contour;
        contour.reserve(points.size());
        for(uint32_t i=0;i<points.size();i++) {
            if(!contour.empty()) {
                auto a=projected[contour.back()],b=projected[i];
                if(std::abs(a.x-b.x)<=length_eps &&
                   std::abs(a.y-b.y)<=length_eps) continue;
            }
            contour.push_back(i);
        }
        if(contour.size()>2) {
            auto a=projected[contour.front()],b=projected[contour.back()];
            if(std::abs(a.x-b.x)<=length_eps &&
               std::abs(a.y-b.y)<=length_eps) contour.pop_back();
        }
        if(contour.size()<3) return false;
        double signed_area2=0.0;
        for(size_t i=0;i<contour.size();i++) {
            auto a=projected[contour[i]];
            auto b=projected[contour[(i+1)%contour.size()]];
            signed_area2+=a.x*b.y-a.y*b.x;
        }
        if(std::abs(signed_area2)<=eps) return false;
        double const sign=signed_area2>0.0?1.0:-1.0;
        // An intersecting ring has no unambiguous native-compatible cached
        // triangulation. Reject it rather than drawing accidental triangles.
        for(size_t i=0;i<contour.size();i++) for(size_t j=i+1;j<contour.size();j++) {
            if(j==i+1 || (i==0 && j+1==contour.size())) continue;
            auto a=projected[contour[i]];
            auto b=projected[contour[(i+1)%contour.size()]];
            auto c=projected[contour[j]];
            auto d=projected[contour[(j+1)%contour.size()]];
            if(gpu_3do_segments_intersect(a,b,c,d,length_eps,eps))
                return false;
        }

        // Ear clipping handles concave polygons; the old triangle fan didn't.
        // Append only after the ENTIRE face has been triangulated successfully.
        std::vector<std::array<uint32_t,3>> result;
        result.reserve(contour.size()-2);
        auto emit=[&](uint32_t ia,uint32_t ib,uint32_t ic) {
            auto const &a=points[ia],&b=points[ib],&c=points[ic];
            double const ux=double(b[0])-a[0],uy=double(b[1])-a[1],
                         uz=double(b[2])-a[2];
            double const vx=double(c[0])-a[0],vy=double(c[1])-a[1],
                         vz=double(c[2])-a[2];
            double const facing=(uy*vz-uz*vy)*face_normal[0]+
                                (uz*vx-ux*vz)*face_normal[1]+
                                (ux*vy-uy*vx)*face_normal[2];
            if(facing<0.0) std::swap(ib,ic);
            result.push_back({ia,ib,ic});
        };
        size_t iterations=0;
        while(contour.size()>3 && iterations++<points.size()*points.size()) {
            bool clipped=false;
            for(size_t i=0;i<contour.size();i++) {
                size_t const prev=(i+contour.size()-1)%contour.size();
                size_t const next=(i+1)%contour.size();
                uint32_t a=contour[prev],b=contour[i],c=contour[next];
                double const area=sign*gpu_3do_cross(
                    projected[a],projected[b],projected[c]);
                // Remove redundant straight-edge corners if necessary.
                if(std::abs(area)<=eps) {
                    contour.erase(contour.begin()+i);
                    clipped=true;
                    break;
                }
                if(area<eps) continue;
                bool contains=false;
                for(uint32_t v:contour) {
                    if(v==a || v==b || v==c) continue;
                    auto const p=projected[v];
                    if(sign*gpu_3do_cross(projected[a],projected[b],p)>=-eps &&
                       sign*gpu_3do_cross(projected[b],projected[c],p)>=-eps &&
                       sign*gpu_3do_cross(projected[c],projected[a],p)>=-eps) {
                        contains=true;
                        break;
                    }
                }
                if(contains) continue;
                emit(a,b,c);
                contour.erase(contour.begin()+i);
                clipped=true;
                break;
            }
            if(!clipped) return false;
        }
        if(contour.size()!=3 ||
           sign*gpu_3do_cross(projected[contour[0]],
                             projected[contour[1]],
                             projected[contour[2]])<=eps) return false;
        emit(contour[0],contour[1],contour[2]);
        triangles=std::move(result);
        return !triangles.empty();
    }
}
