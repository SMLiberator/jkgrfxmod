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
    using lod_point=std::array<float,3>;
    using tri=std::array<uint32_t,3>;
    struct face { tri corners{},points{}; uint32_t group=0; bool live=true; };
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
        double error(lod_point const &v) const {
            double x=v[0],y=v[1],z=v[2];
            return q[0]*x*x+2*q[1]*x*y+2*q[2]*x*z+2*q[3]*x+
                   q[4]*y*y+2*q[5]*y*z+2*q[6]*y+
                   q[7]*z*z+2*q[8]*z+q[9];
        }
    };
    inline double distance2(lod_point const &a,lod_point const &b) {
        double x=double(a[0])-b[0],y=double(a[1])-b[1],
               z=double(a[2])-b[2];
        return x*x+y*y+z*z;
    }
    inline lod_point midpoint(lod_point const &a,lod_point const &b) {
        return {{float((double(a[0])+b[0])*0.5),
                 float((double(a[1])+b[1])*0.5),
                 float((double(a[2])+b[2])*0.5)}};
    }
    using edge=std::pair<uint32_t,uint32_t>;
    inline edge make_edge(uint32_t a,uint32_t b) {
        return a<b ? edge{a,b}:edge{b,a};
    }
    struct candidate { edge endpoints; lod_point position; double error; };
}

// Screen-space LOD selection with hysteresis; radius is projected pixels.
// Model nodes retain LOD0 near the camera, independent of JK's GeoSet.
// A 4-pixel 20k-triangle mesh can select LOD2 without distance constants.
inline unsigned select_gpu_3do_auto_lod(
    float radius,unsigned previous=0,
    float lod1_radius_px=90.0f,float lod2_radius_px=28.0f,
    int debug_mode=0)
{
    // Debug forcing ignores ALL camera-distance and screen-size rules,
    // but cannot make an ineligible/low-poly/alpha mesh reducible.
    if(debug_mode>=1 && debug_mode<=3)
        return static_cast<unsigned>(debug_mode-1);
    if(!std::isfinite(radius) || radius<0.0f ||
       !std::isfinite(lod1_radius_px) ||
       !std::isfinite(lod2_radius_px) ||
       lod2_radius_px<1.0f || lod1_radius_px<=lod2_radius_px)
        return 0U;
    // Hysteresis: leaving LOD1/2 requires a ~20-43% larger radius
    // than entering them; user-configured thresholds scale both edges.
    float const lod1_exit=lod1_radius_px*(110.0f/90.0f);
    float const lod2_exit=lod2_radius_px*(40.0f/28.0f);
    if(previous>=2U)
        return radius>lod1_exit?0U:(radius>lod2_exit?1U:2U);
    if(previous==1U)
        return radius>lod1_exit?0U:(radius<lod2_radius_px?2U:1U);
    return radius<lod2_radius_px?2U:
           (radius<lod1_radius_px?1U:0U);
}

// Simplify one complete rdMesh at a time, not one MAT draw-range at a time.
// In JK a visually continuous mesh may consist of hundreds of tiny material
// groups. Shared position topology allows QEM to span compatible group
// boundaries without changing the order or identities of those draw ranges.
// Return null when no safe, meaningful reduction exists.
inline std::shared_ptr<gpu_3do_geometry const> generate_gpu_3do_auto_lod(
    gpu_3do_geometry const &source,unsigned level)
{
    using namespace auto_lod_detail;
    constexpr size_t minimum_triangles=24U;
    if(level<1U || level>2U ||
       source.indices.size()<minimum_triangles*3U ||
       source.groups.empty())return {};

    std::vector<face> faces;
    std::vector<lod_point> points;
    std::map<lod_point,uint32_t> point_ids;
    std::vector<uint8_t> uv_seams;
    std::vector<std::array<float,2>> first_uv;
    std::vector<size_t> group_begins;
    std::vector<bool> group_safe;
    faces.reserve(source.indices.size()/3U);
    group_begins.reserve(source.groups.size()+1U);
    group_safe.reserve(source.groups.size());
    for(size_t group_index=0;group_index<source.groups.size();++group_index) {
        auto const &group=source.groups[group_index];
        if(group.offset>source.indices.size() ||
           group.count>source.indices.size()-group.offset ||
           group.count%3U!=0U)return {};
        group_begins.push_back(faces.size());
        group_safe.push_back(
            (group.face_type&2U)==0U && !group.nonplanar &&
            !group.saber_trail_join && !group.saber_trail_tip_bridge);
        for(uint32_t offset=0;offset<group.count;offset+=3U) {
            face f{};
            f.group=static_cast<uint32_t>(group_index);
            for(unsigned k=0;k<3;++k) {
                uint32_t const corner=source.indices[group.offset+offset+k];
                if(corner>=source.vertices.size())return {};
                auto const &v=source.vertices[corner];
                lod_point const position{{v.x,v.y,v.z}};
                if(!std::isfinite(v.x) || !std::isfinite(v.y) ||
                   !std::isfinite(v.z) ||
                   !std::isfinite(v.u) || !std::isfinite(v.v))return {};
                auto const inserted=point_ids.emplace(
                    position,static_cast<uint32_t>(points.size()));
                if(inserted.second) {
                    points.push_back(position);
                    first_uv.push_back({{v.u,v.v}});
                    uv_seams.push_back(0U);
                } else {
                    uint32_t const p=inserted.first->second;
                    if(std::fabs(first_uv[p][0]-v.u)>0.001f ||
                       std::fabs(first_uv[p][1]-v.v)>0.001f)
                        uv_seams[p]=1U;
                }
                f.corners[k]=corner;
                f.points[k]=inserted.first->second;
            }
            faces.push_back(f);
        }
    }
    group_begins.push_back(faces.size());
    if(faces.size()<minimum_triangles || points.size()<6U)return {};
    if(std::none_of(group_safe.begin(),group_safe.end(),
                    [](bool safe){return safe;}))return {};

    // Draw-range equality does not imply geometric compatibility, and
    // distinct source ranges can still have exactly the same material.
    // Alpha, nonplanar and procedural groups always stop edge collapses.
    auto compatible=[&](uint32_t a,uint32_t b)->bool {
        if(!group_safe[a] || !group_safe[b])return false;
        auto const &x=source.groups[a],&y=source.groups[b];
        return x.material==y.material && x.cel==y.cel &&
               x.face_type==y.face_type &&
               x.solid_color==y.solid_color &&
               x.nonplanar==y.nonplanar;
    };
    lod_point lo=points.front(),hi=points.front();
    for(auto const &p:points)
        for(unsigned axis=0;axis<3U;++axis) {
            lo[axis]=std::min(lo[axis],p[axis]);
            hi[axis]=std::max(hi[axis],p[axis]);
        }
    double const extent=std::sqrt(distance2(lo,hi));
    double const maximum_edge=extent*(level==1U?2.0:3.0)/
                              std::sqrt(double(faces.size()));
    double const maximum_edge2=maximum_edge*maximum_edge;
    size_t remaining=faces.size();
    size_t const target=faces.size()/(level==1U?2U:4U);
    struct edge_info {
        uint32_t first_face=0,second_face=0;
        unsigned count=0;
    };
    // Recompute topology/QEM after each parallel independent collapse
    // batch. In particular, do not operate on stale edges after a merge.
    for(unsigned pass=0;pass<(level==1U?8U:12U) &&
                        remaining>target;++pass) {
        std::map<edge,edge_info> edges;
        std::vector<quadric> q(points.size());
        std::vector<std::set<uint32_t>> neighbors(points.size());
        std::vector<std::vector<uint32_t>> incident(points.size());
        std::vector<lod_point> face_normals(faces.size());
        for(uint32_t fidx=0;fidx<faces.size();++fidx) {
            auto const &f=faces[fidx];
            if(!f.live)continue;
            uint32_t const a=f.points[0],b=f.points[1],c=f.points[2];
            for(uint32_t p:f.points)incident[p].push_back(fidx);
            for(auto const &e:{make_edge(a,b),make_edge(b,c),make_edge(c,a)}) {
                auto &record=edges[e];
                if(record.count==0U)record.first_face=fidx;
                else if(record.count==1U)record.second_face=fidx;
                ++record.count;
                neighbors[e.first].insert(e.second);
                neighbors[e.second].insert(e.first);
            }
            auto const &p=points[a],&r=points[b],&s=points[c];
            double const ux=r[0]-p[0],uy=r[1]-p[1],uz=r[2]-p[2],
                         vx=s[0]-p[0],vy=s[1]-p[1],vz=s[2]-p[2];
            double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,
                   nz=ux*vy-uy*vx;
            double const len=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(len<=1.e-12)continue;
            nx/=len;ny/=len;nz/=len;
            face_normals[fidx]={{float(nx),float(ny),float(nz)}};
            double const d=-(nx*p[0]+ny*p[1]+nz*p[2]);
            q[a].add(nx,ny,nz,d);
            q[b].add(nx,ny,nz,d);
            q[c].add(nx,ny,nz,d);
        }

        // Freeze all mesh open edges, material/UV boundaries, hard edges,
        // alpha boundaries and nonmanifold edges. These are actual
        // 3D geometric boundaries, NOT artificial rdMesh draw-range splits.
        std::vector<uint8_t> fixed=uv_seams;
        for(auto const &entry:edges) {
            auto const &e=entry.first;
            auto const &record=entry.second;
            bool boundary=record.count!=2U;
            if(!boundary) {
                auto const &a=faces[record.first_face];
                auto const &b=faces[record.second_face];
                boundary=!compatible(a.group,b.group);
                if(!boundary) {
                    auto const &n=face_normals[record.first_face];
                    auto const &m=face_normals[record.second_face];
                    double const cosine=double(n[0])*m[0]+
                        double(n[1])*m[1]+double(n[2])*m[2];
                    boundary=cosine<0.866; // keep hard creases (>30 degrees)
                }
            }
            if(boundary)fixed[e.first]=fixed[e.second]=1U;
        }
        // Check every affected incident triangle before accepting a
        // collapse: no surviving face may flip or become nearly zero-area.
        auto safe_collapse=[&](uint32_t a,uint32_t b,
                               lod_point const &middle)->bool {
            auto const check_face=[&](uint32_t fi)->bool {
                auto const &f=faces[fi];
                if(!f.live)return true;
                bool has_a=false,has_b=false;
                for(uint32_t p:f.points) {
                    if(p==a)has_a=true;
                    if(p==b)has_b=true;
                }
                if(has_a && has_b)return true; // edge's two removed faces
                lod_point const *v[3];
                for(unsigned k=0;k<3;++k)
                    v[k]=(f.points[k]==a || f.points[k]==b)
                        ?&middle:&points[f.points[k]];
                double const ux=double((*v[1])[0])-(*v[0])[0],
                             uy=double((*v[1])[1])-(*v[0])[1],
                             uz=double((*v[1])[2])-(*v[0])[2],
                             vx=double((*v[2])[0])-(*v[0])[0],
                             vy=double((*v[2])[1])-(*v[0])[1],
                             vz=double((*v[2])[2])-(*v[0])[2];
                double const nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,
                             nz=ux*vy-uy*vx;
                double const length=std::sqrt(nx*nx+ny*ny+nz*nz);
                if(length<1.e-10)return false;
                auto const &old=face_normals[fi];
                return (nx*old[0]+ny*old[1]+nz*old[2])>=
                       0.35*length;
            };
            for(uint32_t fi:incident[a])if(!check_face(fi))return false;
            for(uint32_t fi:incident[b])if(!check_face(fi))return false;
            return true;
        };
        std::vector<candidate> options;
        for(auto const &entry:edges) {
            if(entry.second.count!=2U)continue;
            uint32_t const a=entry.first.first,b=entry.first.second;
            if(fixed[a] || fixed[b] ||
               distance2(points[a],points[b])>maximum_edge2)continue;
            // For a manifold interior edge, exactly two vertices must
            // belong to the intersection of both one-ring neighborhoods.
            unsigned shared=0U;
            for(uint32_t n:neighbors[a])
                if(neighbors[b].count(n))++shared;
            if(shared!=2U)continue;
            quadric combined=q[a];combined+=q[b];
            lod_point const mid=midpoint(points[a],points[b]);
            double const error=combined.error(mid);
            if(!std::isfinite(error) || !safe_collapse(a,b,mid))continue;
            options.push_back({entry.first,mid,
                std::max(error,0.0)+
                1.e-8*distance2(points[a],points[b])});
        }
        std::sort(options.begin(),options.end(),
            [](candidate const &a,candidate const &b) {
                if(a.error!=b.error)return a.error<b.error;
                return a.endpoints<b.endpoints;
            });
        std::vector<uint32_t> remap(points.size());
        std::vector<uint8_t> touched(points.size(),0U);
        for(uint32_t i=0;i<remap.size();++i)remap[i]=i;
        size_t collapses=0U;
        for(auto const &choice:options) {
            uint32_t const a=choice.endpoints.first;
            uint32_t const b=choice.endpoints.second;
            if(touched[a] || touched[b])continue;
            // Prevent conflicting collapses in a single triangle's
            // one-ring: endpoint disjointness alone is insufficient.
            bool conflict=false;
            for(uint32_t n:neighbors[a])if(touched[n]){conflict=true;break;}
            if(!conflict)
                for(uint32_t n:neighbors[b])if(touched[n]){conflict=true;break;}
            if(conflict)continue;
            touched[a]=touched[b]=1U;
            for(uint32_t n:neighbors[a])touched[n]=1U;
            for(uint32_t n:neighbors[b])touched[n]=1U;
            remap[a]=b;
            points[b]=choice.position;
            ++collapses;
            if(collapses>=remaining-target)break;
        }
        if(!collapses)break;
        remaining=0U;
        for(auto &f:faces) {
            if(!f.live)continue;
            for(uint32_t &p:f.points)p=remap[p];
            if(f.points[0]==f.points[1] ||
               f.points[1]==f.points[2] ||
               f.points[2]==f.points[0])f.live=false;
            else ++remaining;
        }
    }

    size_t const removed=faces.size()-remaining;
    // Avoid duplicate VBO memory for negligible reductions.
    if(removed<2U || removed*50U<faces.size())return {};
    auto out=std::make_shared<gpu_3do_geometry>(source);
    out->vertices.clear();
    out->indices.clear();
    out->groups.clear();
    out->vertices.reserve(remaining*3U);
    out->indices.reserve(remaining*3U);
    for(size_t g=0;g<source.groups.size();++g) {
        auto next=source.groups[g];
        next.offset=static_cast<uint32_t>(out->indices.size());
        bool modified=false;
        for(size_t i=group_begins[g];i<group_begins[g+1U];++i) {
            auto const &f=faces[i];
            if(!f.live){modified=true;continue;}
            for(unsigned k=0;k<3;++k) {
                auto v=source.vertices[f.corners[k]];
                auto const &position=points[f.points[k]];
                if(v.x!=position[0] || v.y!=position[1] ||
                   v.z!=position[2])modified=true;
                v.x=position[0];
                v.y=position[1];
                v.z=position[2];
                out->indices.push_back(static_cast<uint32_t>(
                    out->vertices.size()));
                out->vertices.push_back(v);
            }
        }
        next.count=static_cast<uint32_t>(out->indices.size())-next.offset;
        // Original JK face-plane CPU backface culling no longer exactly
        // describes collapsed faces. Fail open for affected groups; leave
        // all unmodified groups with their original CPU culling.
        if(modified)next.plane_count=0U;
        if(next.count)out->groups.push_back(next);
    }
    return out;
}
} // namespace jkgm
