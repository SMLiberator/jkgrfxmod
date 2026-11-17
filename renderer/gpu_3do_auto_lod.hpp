#pragma once
// Experimental GPU-only 3DO LOD generation. Material draw ranges are
// retained; modified face-plane culling is deliberately disabled. The generator never mutates the
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
    // Exact closed 12-triangle cuboids are preserved even when embedded
    // in a larger Thing. This is NOT a per-rdMesh triangle floor.
    inline bool is_closed_cuboid(gpu_3do_geometry const &mesh) {
        if(mesh.indices.size()!=36U)return false;
        std::set<lod_point> unique;
        lod_point lo{},hi{};
        bool begun=false;
        for(uint32_t index:mesh.indices) {
            if(index>=mesh.vertices.size())return false;
            auto const &v=mesh.vertices[index];
            if(!std::isfinite(v.x)||!std::isfinite(v.y)||
               !std::isfinite(v.z))return false;
            lod_point const p{{v.x,v.y,v.z}};
            unique.insert(p);
            if(!begun)lo=hi=p;
            else for(unsigned axis=0;axis<3;++axis) {
                lo[axis]=std::min(lo[axis],p[axis]);
                hi[axis]=std::max(hi[axis],p[axis]);
            }
            begun=true;
        }
        if(unique.size()!=8U)return false;
        for(unsigned axis=0;axis<3;++axis)
            if(!(hi[axis]>lo[axis]))return false;
        for(auto const &p:unique)
            for(unsigned axis=0;axis<3;++axis)
                if(p[axis]!=lo[axis]&&p[axis]!=hi[axis])return false;
        std::array<unsigned,6> planes{};
        for(size_t i=0;i<mesh.indices.size();i+=3U) {
            lod_point const a{{mesh.vertices[mesh.indices[i]].x,
                               mesh.vertices[mesh.indices[i]].y,
                               mesh.vertices[mesh.indices[i]].z}};
            lod_point const b{{mesh.vertices[mesh.indices[i+1U]].x,
                               mesh.vertices[mesh.indices[i+1U]].y,
                               mesh.vertices[mesh.indices[i+1U]].z}};
            lod_point const c{{mesh.vertices[mesh.indices[i+2U]].x,
                               mesh.vertices[mesh.indices[i+2U]].y,
                               mesh.vertices[mesh.indices[i+2U]].z}};
            double const ux=double(b[0])-a[0],uy=double(b[1])-a[1],
                         uz=double(b[2])-a[2];
            double const vx=double(c[0])-a[0],vy=double(c[1])-a[1],
                         vz=double(c[2])-a[2];
            double const nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,
                         nz=ux*vy-uy*vx;
            if(nx*nx+ny*ny+nz*nz<=1.e-24)return false;
            unsigned plane=6U;
            for(unsigned axis=0;axis<3;++axis) {
                if(a[axis]==lo[axis]&&b[axis]==lo[axis]&&
                   c[axis]==lo[axis])plane=2U*axis;
                else if(a[axis]==hi[axis]&&b[axis]==hi[axis]&&
                        c[axis]==hi[axis])plane=2U*axis+1U;
            }
            if(plane==6U)return false;
            ++planes[plane];
        }
        return std::all_of(planes.begin(),planes.end(),
                           [](unsigned count){return count==2U;});
    }
}

// Screen-space LOD selection with hysteresis; radius is projected pixels.
// Model nodes retain LOD0 near the camera, independent of JK's GeoSet.
// A 4-pixel 20k-triangle mesh can select LOD2 without distance constants.
inline unsigned select_gpu_3do_auto_lod(
    float radius,unsigned previous=0,
    float lod1_radius_px=90.0f,float lod2_radius_px=28.0f,
    int debug_mode=0)
{
    // Debug forcing ignores camera distance. Only an irreducible or entirely
    // collapsed source keeps its original triangles.
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

// Project an authored SithThing-centered rdModel3 bounding sphere from
// the Thing's WORLD position, not the per-node rdMesh view-space matrix.
// Return -1 if the projected radius is invalid.
inline float gpu_3do_auto_lod_projected_radius_px(
    std::array<float,3> const &world_center,
    std::array<float,3> const &camera_position,
    std::array<float,3> const &camera_forward,
    double world_radius,double screen_half_px,double tan_half_fov)
{
    double const forward=
        (double(world_center[0])-camera_position[0])*camera_forward[0]+
        (double(world_center[1])-camera_position[1])*camera_forward[1]+
        (double(world_center[2])-camera_position[2])*camera_forward[2];
    double const near_depth=forward-world_radius;
    if(!(near_depth>0.01) || !(world_radius>0.0) ||
       !(screen_half_px>0.0) || !(tan_half_fov>0.0))
        return -1.0f;
    double const projected=world_radius*screen_half_px/
                           (tan_half_fov*near_depth);
    if(!std::isfinite(projected) || projected>1.e8)
        return -1.0f;
    return static_cast<float>(projected);
}

// The entire owning Thing shares a triangle floor across all its rdMeshes.
// Never apply the minimum independently to a small mesh node.
inline bool gpu_3do_auto_lod_object_allows(
    uint64_t original_triangles,uint64_t proposed_triangles,
    unsigned minimum_object_triangles)
{
    return minimum_object_triangles==0U ||
           (original_triangles>minimum_object_triangles &&
            proposed_triangles>=minimum_object_triangles);
}

// CPU generation scheduling is independent of visual simplification.
inline bool gpu_3do_auto_lod_budget_allows(
    unsigned generated_this_frame,int budget)
{
    return budget==0 ||
           generated_this_frame<static_cast<unsigned>(budget);
}

// AGGRESSIVE experiment: all native 3DO mesh sizes, opaque/alpha groups,
// material/UV seams, hard/open edges and nonmanifold edges are eligible.
// Surviving corner attributes and group order stay intact; only a completely
// empty result or no triangle reduction reuses source geometry.
inline std::shared_ptr<gpu_3do_geometry const> generate_gpu_3do_auto_lod(
    gpu_3do_geometry const &source,unsigned level)
{
    using namespace auto_lod_detail;
    if(level<1U || level>2U || source.indices.empty() ||
       source.groups.empty() || is_closed_cuboid(source))return {};

    std::vector<face> faces;
    std::vector<lod_point> points;
    std::map<lod_point,uint32_t> point_ids;
    std::vector<size_t> group_begins;
    faces.reserve(source.indices.size()/3U);
    group_begins.reserve(source.groups.size()+1U);
    for(size_t group_index=0;group_index<source.groups.size();++group_index) {
        auto const &group=source.groups[group_index];
        if(group.offset>source.indices.size() ||
           group.count>source.indices.size()-group.offset ||
           group.count%3U!=0U)return {};
        group_begins.push_back(faces.size());
        for(uint32_t offset=0;offset<group.count;offset+=3U) {
            face f{};
            f.group=static_cast<uint32_t>(group_index);
            for(unsigned k=0;k<3;++k) {
                uint32_t const corner=source.indices[group.offset+offset+k];
                if(corner>=source.vertices.size())return {};
                auto const &v=source.vertices[corner];
                lod_point const position{{v.x,v.y,v.z}};
                if(!std::isfinite(v.x) || !std::isfinite(v.y) ||
                   !std::isfinite(v.z))return {};
                auto const inserted=point_ids.emplace(
                    position,static_cast<uint32_t>(points.size()));
                if(inserted.second)points.push_back(position);
                f.corners[k]=corner;
                f.points[k]=inserted.first->second;
            }
            faces.push_back(f);
        }
    }
    group_begins.push_back(faces.size());
    if(faces.empty() || points.empty())return {};

    size_t remaining=faces.size();
    size_t const target=faces.size()/(level==1U?2U:4U);
    // Recompute topology/QEM after each parallel independent collapse
    // batch. In particular, do not operate on stale edges after a merge.
    for(unsigned pass=0;pass<64U && remaining>target;++pass) {
        std::set<edge> edges;
        std::vector<quadric> q(points.size());
        std::vector<std::set<uint32_t>> neighbors(points.size());
        for(auto const &f:faces) {
            if(!f.live)continue;
            uint32_t const a=f.points[0],b=f.points[1],c=f.points[2];
            for(auto const &e:{make_edge(a,b),make_edge(b,c),make_edge(c,a)}) {
                edges.insert(e);
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
            double const d=-(nx*p[0]+ny*p[1]+nz*p[2]);
            q[a].add(nx,ny,nz,d);
            q[b].add(nx,ny,nz,d);
            q[c].add(nx,ny,nz,d);
        }

        std::vector<candidate> options;
        for(auto const &entry:edges) {
            uint32_t const a=entry.first,b=entry.second;
            if(a==b)continue;
            quadric combined=q[a];combined+=q[b];
            lod_point const mid=midpoint(points[a],points[b]);
            double const error=combined.error(mid);
            if(!std::isfinite(error))continue;
            options.push_back({entry,mid,
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

    // No minimum improvement threshold. Only reuse the source if no
    // triangles were removed or the entire mesh disappeared.
    if(remaining==0U || remaining==faces.size())return {};
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
