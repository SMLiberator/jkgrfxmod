#pragma once
// Experimental GPU-only 3DO LOD generation. Material draw ranges and their
// native face-plane metadata are preserved. The generator never mutates the
// native rdModel3 or its authored GeoSets.
#include "gpu_3do_preview.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace jkgm {
namespace auto_lod_detail {
    using xyz=std::array<float,3>;
    using tri=std::array<uint32_t,3>;
    struct face { tri corners{},points{}; bool live=true; };
    struct quadric {
        std::array<double,10> q{};
        void add(double a,double b,double c,double d) {
            double p[4]={a,b,c,d};
            static constexpr int row[10]={0,0,0,0,1,1,1,2,2,3};
            static constexpr int col[10]={0,1,2,3,1,2,3,2,3,3};
            for(int i=0;i<10;++i)q[i]+=p[row[i]]*p[col[i]];
        }
        quadric &operator+=(quadric const &b) {
            for(int i=0;i<10;++i)q[i]+=b.q[i];
            return *this;
        }
        double error(xyz const &v) const {
            double x=v[0],y=v[1],z=v[2];
            return q[0]*x*x+2*q[1]*x*y+2*q[2]*x*z+2*q[3]*x+
                   q[4]*y*y+2*q[5]*y*z+2*q[6]*y+
                   q[7]*z*z+2*q[8]*z+q[9];
        }
    };
    inline double distance2(xyz const &a,xyz const &b) {
        double x=double(a[0])-b[0],y=double(a[1])-b[1],
               z=double(a[2])-b[2];
        return x*x+y*y+z*z;
    }
    inline xyz midpoint(xyz const &a,xyz const &b) {
        return {{float((double(a[0])+b[0])*0.5),
                 float((double(a[1])+b[1])*0.5),
                 float((double(a[2])+b[2])*0.5)}};
    }
    using edge=std::pair<uint32_t,uint32_t>;
    inline edge make_edge(uint32_t a,uint32_t b) {
        return a<b ? edge{a,b}:edge{b,a};
    }
    struct candidate { edge endpoints; xyz position; double error; };
}

// Returns null if the source is too small, malformed, or cannot be reduced
// without moving a material boundary. Output is a separate immutable GPU
// geometry; original vertex attributes/UVs are kept at every surviving
// corner, while the position alone follows the collapsed geometric vertex.
inline std::shared_ptr<gpu_3do_geometry const> generate_gpu_3do_auto_lod(
    gpu_3do_geometry const &source,unsigned level)
{
    using namespace auto_lod_detail;
    if(level<1U || level>2U || source.indices.size()<192U ||
       source.groups.empty())return {};
    auto out=std::make_shared<gpu_3do_geometry>(source);
    out->vertices.clear();out->indices.clear();
    out->groups.clear();
    size_t const original=source.indices.size();
    for(auto const &group:source.groups) {
        if(group.offset>source.indices.size() ||
           group.count>source.indices.size()-group.offset ||
           group.count%3U!=0U)return {};
        gpu_3do_group next=group;
        next.offset=static_cast<uint32_t>(out->indices.size());
        std::vector<face> faces;
        std::vector<xyz> points;
        std::map<xyz,uint32_t> point_ids;
        faces.reserve(group.count/3U);
        bool valid=true;
        for(uint32_t i=0;i<group.count;i+=3U) {
            face f{};
            for(unsigned k=0;k<3U;++k) {
                uint32_t const corner=source.indices[group.offset+i+k];
                if(corner>=source.vertices.size()){valid=false;break;}
                auto const &v=source.vertices[corner];
                xyz position{{v.x,v.y,v.z}};
                if(!std::isfinite(v.x)||!std::isfinite(v.y)||
                   !std::isfinite(v.z)){valid=false;break;}
                auto const inserted=point_ids.emplace(
                    position,static_cast<uint32_t>(points.size()));
                if(inserted.second)points.push_back(position);
                f.corners[k]=corner;
                f.points[k]=inserted.first->second;
            }
            if(!valid)break;
            faces.push_back(f);
        }
        if(!valid)return {};
        // A low-poly mesh, alpha-blended surface or procedural fan does not
        // justify topology changes. Preserve its original triangles.
        bool const eligible=faces.size()>=64U &&
            !(group.face_type&2U) &&
            !group.saber_trail_join && !group.saber_trail_tip_bridge;
        if(eligible) {
            size_t const target=faces.size()*(level==1U?1U:1U)/
                                (level==1U?2U:4U);
            xyz lo=points.front(),hi=points.front();
            for(auto const &p:points)
                for(unsigned k=0;k<3;++k) {
                    lo[k]=std::min(lo[k],p[k]);
                    hi[k]=std::max(hi[k],p[k]);
                }
            double const extent=std::sqrt(distance2(lo,hi));
            // Only collapse short interior edges relative to the source
            // triangle density; never merge detached geometry by proximity.
            double const max_edge=extent*
                (level==1U?2.0:3.0)/std::sqrt(double(faces.size()));
            double const max_edge2=max_edge*max_edge;
            size_t remaining=faces.size();
            for(unsigned pass=0;pass<(level==1U?6U:10U) &&
                               remaining>target;++pass) {
                std::map<edge,uint32_t> edges;
                std::vector<quadric> q(points.size());
                std::vector<std::set<uint32_t>> neighbors(points.size());
                for(auto const &f:faces) {
                    if(!f.live)continue;
                    uint32_t a=f.points[0],b=f.points[1],c=f.points[2];
                    for(auto const &e:{make_edge(a,b),make_edge(b,c),make_edge(c,a)}) {
                        ++edges[e];
                        neighbors[e.first].insert(e.second);
                        neighbors[e.second].insert(e.first);
                    }
                    auto const &p=points[a],&r=points[b],&s=points[c];
                    double ux=r[0]-p[0],uy=r[1]-p[1],uz=r[2]-p[2],
                           vx=s[0]-p[0],vy=s[1]-p[1],vz=s[2]-p[2];
                    double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,
                           nz=ux*vy-uy*vx;
                    double len=std::sqrt(nx*nx+ny*ny+nz*nz);
                    if(len<=1.e-12)continue;
                    nx/=len;ny/=len;nz/=len;
                    double d=-(nx*p[0]+ny*p[1]+nz*p[2]);
                    q[a].add(nx,ny,nz,d);
                    q[b].add(nx,ny,nz,d);
                    q[c].add(nx,ny,nz,d);
                }
                std::vector<uint8_t> boundary(points.size(),0U);
                for(auto const &e:edges)
                    if(e.second!=2U) {
                        boundary[e.first.first]=1U;
                        boundary[e.first.second]=1U;
                    }
                std::vector<candidate> options;
                for(auto const &entry:edges) {
                    if(entry.second!=2U)continue;
                    uint32_t a=entry.first.first,b=entry.first.second;
                    if(boundary[a] || boundary[b] ||
                       distance2(points[a],points[b])>max_edge2)continue;
                    // Manifold link condition: an interior edge has exactly
                    // two shared one-ring neighbors. Otherwise skip collapse.
                    unsigned shared=0U;
                    for(uint32_t n:neighbors[a])
                        if(neighbors[b].count(n))++shared;
                    if(shared!=2U)continue;
                    quadric combined=q[a];combined+=q[b];
                    xyz const mid=midpoint(points[a],points[b]);
                    double const error=combined.error(mid);
                    if(!std::isfinite(error))continue;
                    options.push_back({entry.first,mid,
                        std::max(error,0.0)+
                        1.e-8*distance2(points[a],points[b])});
                }
                std::sort(options.begin(),options.end(),
                    [](candidate const &a,candidate const &b){
                        if(a.error!=b.error)return a.error<b.error;
                        return a.endpoints<b.endpoints;
                    });
                std::vector<uint32_t> remap(points.size());
                std::vector<uint8_t> touched(points.size(),0U);
                for(uint32_t i=0;i<remap.size();++i)remap[i]=i;
                size_t collapses=0U;
                for(auto const &choice:options) {
                    uint32_t a=choice.endpoints.first,b=choice.endpoints.second;
                    if(touched[a] || touched[b])continue;
                    touched[a]=touched[b]=1U;
                    remap[a]=b;
                    points[b]=choice.position;
                    ++collapses;
                    if(collapses>remaining-target)break;
                }
                if(!collapses)break;
                remaining=0;
                for(auto &f:faces) {
                    if(!f.live)continue;
                    for(uint32_t &p:f.points)p=remap[p];
                    if(f.points[0]==f.points[1] ||
                       f.points[1]==f.points[2] ||
                       f.points[0]==f.points[2]) {
                        f.live=false;continue;
                    }
                    ++remaining;
                }
            }
        }
        // Preserve all authored vertex attributes and group/material order.
        // Duplicate only the surviving corners; no native data is modified.
        for(auto const &f:faces) {
            if(!f.live)continue;
            for(unsigned k=0;k<3;++k) {
                auto v=source.vertices[f.corners[k]];
                auto const &position=points[f.points[k]];
                v.x=position[0];v.y=position[1];v.z=position[2];
                out->indices.push_back(static_cast<uint32_t>(
                    out->vertices.size()));
                out->vertices.push_back(v);
            }
        }
        next.count=static_cast<uint32_t>(out->indices.size())-next.offset;
        out->groups.push_back(next);
    }
    // Use native source for trivial reductions: avoid duplicate VBO uploads.
    if(out->indices.size()>=original*9U/10U)return {};
    return out;
}
} // namespace jkgm
