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
#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace jkgm {
struct gpu_3do_auto_lod_prune_stats {
    uint32_t components=0;
    uint32_t removed_components=0;
    uint32_t removed_triangles=0;
};
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
    struct candidate {
        edge endpoints;
        lod_point position;
        double error;
        // Quality mode may collapse b into a to retain an original
        // silhouette/crease vertex; the aggressive path keeps b as before.
        bool keep_first=false;
    };

    inline std::array<double,3> qem_cross(lod_point const &p,
                                           lod_point const &q,
                                           lod_point const &r) {
        double const ax=double(q[0])-p[0],ay=double(q[1])-p[1],
                     az=double(q[2])-p[2];
        double const bx=double(r[0])-p[0],by=double(r[1])-p[1],
                     bz=double(r[2])-p[2];
        return {{ay*bz-az*by,az*bx-ax*bz,ax*by-ay*bx}};
    }
    inline double qem_length2(std::array<double,3> const &n) {
        return n[0]*n[0]+n[1]*n[1]+n[2]*n[2];
    }
    inline bool qem_optimal(quadric const &q,lod_point &out) {
        double a[3][4]={{q.q[0],q.q[1],q.q[2],-q.q[3]},
                        {q.q[1],q.q[4],q.q[5],-q.q[6]},
                        {q.q[2],q.q[5],q.q[7],-q.q[8]}};
        double scale=0.0;
        for(unsigned i=0;i<3;++i)
            for(unsigned j=0;j<3;++j)
                scale=std::max(scale,std::fabs(a[i][j]));
        if(!std::isfinite(scale)||scale<=1.e-20)return false;
        for(unsigned col=0;col<3;++col) {
            unsigned pivot=col;
            for(unsigned row=col+1;row<3;++row)
                if(std::fabs(a[row][col])>std::fabs(a[pivot][col]))pivot=row;
            if(std::fabs(a[pivot][col])<scale*1.e-10)return false;
            if(pivot!=col)
                for(unsigned k=0;k<4;++k)std::swap(a[pivot][k],a[col][k]);
            double const denom=a[col][col];
            for(unsigned k=col;k<4;++k)a[col][k]/=denom;
            for(unsigned row=0;row<3;++row) {
                if(row==col)continue;
                double const factor=a[row][col];
                for(unsigned k=col;k<4;++k)a[row][k]-=factor*a[col][k];
            }
        }
        for(unsigned i=0;i<3;++i) {
            double const v=a[i][3];
            if(!std::isfinite(v) ||
               std::fabs(v)>std::numeric_limits<float>::max())
                return false;
            out[i]=float(v);
        }
        return true;
    }
    inline bool qem_safe_collapse(
        uint32_t a,uint32_t b,lod_point const &replacement,
        std::vector<lod_point> const &points,
        std::vector<face> const &faces,
        std::vector<std::vector<size_t>> const &incident,
        size_t expected_removed=2U)
    {
        size_t removed=0;
        auto check=[&](size_t index)->bool {
            auto const &f=faces[index];
            if(!f.live)return true;
            bool has_a=false,has_b=false;
            for(uint32_t p:f.points) {
                has_a|=p==a;has_b|=p==b;
            }
            if(has_a&&has_b) { ++removed;return true; }
            std::array<lod_point,3> after{};
            for(unsigned k=0;k<3;++k)
                after[k]=(f.points[k]==a||f.points[k]==b)
                    ?replacement:points[f.points[k]];
            auto const old=qem_cross(points[f.points[0]],
                                     points[f.points[1]],
                                     points[f.points[2]]);
            auto const fresh=qem_cross(after[0],after[1],after[2]);
            double const old2=qem_length2(old),fresh2=qem_length2(fresh);
            if(!(old2>1.e-24)||!(fresh2>old2*0.0025))return false;
            double const dot=old[0]*fresh[0]+old[1]*fresh[1]+
                             old[2]*fresh[2];
            return dot>0.5*std::sqrt(old2*fresh2);
        };
        for(size_t index:incident[a])if(!check(index))return false;
        for(size_t index:incident[b]) {
            bool duplicate=false;
            for(uint32_t p:faces[index].points)duplicate|=p==a;
            if(!duplicate&&!check(index))return false;
        }
        return removed==expected_removed;
    }
    // Squared distance of the removed contour vertex to the new contour
    // chord. This measures silhouette deviation even on coplanar surfaces
    // where conventional face-plane QEM has zero error.
    inline double qem_point_segment_distance2(
        lod_point const &p,lod_point const &a,lod_point const &b)
    {
        double d[3]={double(b[0])-a[0],double(b[1])-a[1],
                     double(b[2])-a[2]};
        double v[3]={double(p[0])-a[0],double(p[1])-a[1],
                     double(p[2])-a[2]};
        double const d2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
        if(!(d2>1.e-24))return distance2(p,a);
        double const t=std::max(0.0,std::min(1.0,
            (d[0]*v[0]+d[1]*v[1]+d[2]*v[2])/d2));
        lod_point const closest{{float(double(a[0])+t*d[0]),
                                 float(double(a[1])+t*d[1]),
                                 float(double(a[2])+t*d[2])}};
        return distance2(p,closest);
    }

    // Identify disconnected pieces in the SAME rdMesh using the exact-
    // position welded topology already used by QEM. A badge/decal polygon
    // island can be small even if it contains many triangles. Remove a
    // whole island only if its bounding-box diagonal is below the chosen
    // fraction of the source mesh diagonal; never remove the largest island.
    // This deliberately does not cut connected protrusions or other nodes.
    inline void qem_prune_small_components(
        std::vector<face> &faces,
        std::vector<lod_point> const &points,
        double relative_diameter,
        gpu_3do_auto_lod_prune_stats *stats)
    {
        if(!(relative_diameter>0.0)||points.empty()||faces.empty())
            return;
        std::vector<uint32_t> parent(points.size());
        for(uint32_t i=0;i<parent.size();++i)parent[i]=i;
        auto root=[&](uint32_t i)->uint32_t {
            while(parent[i]!=i) {
                parent[i]=parent[parent[i]];
                i=parent[i];
            }
            return i;
        };
        auto join=[&](uint32_t a,uint32_t b) {
            a=root(a);b=root(b);
            if(a!=b)parent[std::max(a,b)]=std::min(a,b);
        };
        for(auto const &f:faces) {
            join(f.points[0],f.points[1]);
            join(f.points[1],f.points[2]);
        }
        struct component {
            lod_point lo{},hi{};
            uint32_t triangles=0;
        };
        std::map<uint32_t,component> components;
        bool global_valid=false;
        lod_point global_lo{},global_hi{};
        for(auto const &f:faces) {
            uint32_t const id=root(f.points[0]);
            auto it=components.find(id);
            if(it==components.end()) {
                auto const &p=points[f.points[0]];
                it=components.emplace(id,component{p,p,0U}).first;
            }
            auto &item=it->second;
            ++item.triangles;
            for(uint32_t index:f.points) {
                auto const &p=points[index];
                for(unsigned axis=0;axis<3;++axis) {
                    item.lo[axis]=std::min(item.lo[axis],p[axis]);
                    item.hi[axis]=std::max(item.hi[axis],p[axis]);
                    if(!global_valid) {
                        global_lo[axis]=p[axis];
                        global_hi[axis]=p[axis];
                    } else {
                        global_lo[axis]=std::min(global_lo[axis],p[axis]);
                        global_hi[axis]=std::max(global_hi[axis],p[axis]);
                    }
                }
                global_valid=true;
            }
        }
        if(stats)stats->components=static_cast<uint32_t>(components.size());
        if(components.size()<2U)return;
        double const source_diagonal2=distance2(global_lo,global_hi);
        if(!(source_diagonal2>0.0)||!std::isfinite(source_diagonal2))
            return;
        double const cutoff2=source_diagonal2*
                             relative_diameter*relative_diameter;
        // Preserve the spatially largest part even if the user requests
        // extreme detail pruning; a small disconnected single-triangle
        // model is not considered disposable by itself.
        uint32_t largest=components.begin()->first;
        double largest_diagonal2=-1.0;
        uint32_t largest_triangles=0U;
        for(auto const &pair:components) {
            auto const &component=pair.second;
            double const d2=distance2(component.lo,component.hi);
            if(d2>largest_diagonal2 ||
               (d2==largest_diagonal2 &&
                component.triangles>largest_triangles)) {
                largest=pair.first;
                largest_diagonal2=d2;
                largest_triangles=component.triangles;
            }
        }
        std::set<uint32_t> rejected;
        for(auto const &pair:components) {
            if(pair.first==largest)continue;
            auto const &item=pair.second;
            if(distance2(item.lo,item.hi)<cutoff2)
                rejected.insert(pair.first);
        }
        if(rejected.empty())return;
        for(auto &f:faces) {
            if(rejected.count(root(f.points[0]))!=0U) {
                f.live=false;
                if(stats)++stats->removed_triangles;
            }
        }
        if(stats)stats->removed_components=
            static_cast<uint32_t>(rejected.size());
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
    gpu_3do_geometry const &source,unsigned level,
    int simplifier_mode=0,float max_error_ratio=0.01f,
    float detail_prune_ratio=0.0f,
    gpu_3do_auto_lod_prune_stats *prune_stats=nullptr)
{
    using namespace auto_lod_detail;
    bool const quality=simplifier_mode==1;
    if(prune_stats)*prune_stats={};
    if(level<1U || level>2U || source.indices.empty() ||
       source.groups.empty() ||
       (quality && (!std::isfinite(max_error_ratio) ||
                    max_error_ratio<0.0f ||
                    !std::isfinite(detail_prune_ratio) ||
                    detail_prune_ratio<0.0f ||
                    detail_prune_ratio>0.25f)))return {};

    std::vector<face> faces;
    std::vector<lod_point> points;
    std::map<lod_point,uint32_t> point_ids;
    std::vector<uint8_t> locked;
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
                if(inserted.second) {
                    points.push_back(position);
                    if(quality)locked.push_back(0U);
                }
                // Preserve original UV, tint and lighting attributes on
                // surviving per-face corners, but do not hard-lock a shared
                // position merely because those corners differ. Such seams
                // are widespread in JK 3DOs and previously froze meshes.
                f.corners[k]=corner;
                f.points[k]=inserted.first->second;
            }
            faces.push_back(f);
        }
    }
    group_begins.push_back(faces.size());
    if(faces.empty() || points.empty())return {};
    // LOD1 removes only half as large detail islands as LOD2. Both are
    // explicit opt-in; 0.0 preserves the previous quality-mode output.
    if(quality && detail_prune_ratio>0.0f)
        qem_prune_small_components(
            faces,points,double(detail_prune_ratio)*
                (level==1U?0.5:1.0),prune_stats);
    size_t remaining=faces.size();
    if(quality && detail_prune_ratio>0.0f)
        for(auto const &f:faces)if(!f.live)--remaining;
    size_t const target=faces.size()/(level==1U?2U:4U);
    // QEM errors have squared world-space units. Keep ORIGINAL face-plane
    // quadrics accumulated across passes in quality mode, avoiding drift.
    std::vector<quadric> original_quadrics;
    double allowed_error=0.0,diagonal=0.0;
    if(quality) {
        lod_point low=points.front(),high=low;
        for(auto const &p:points)
            for(unsigned axis=0;axis<3;++axis) {
                low[axis]=std::min(low[axis],p[axis]);
                high[axis]=std::max(high[axis],p[axis]);
            }
        diagonal=std::sqrt(distance2(low,high));
        if(!(diagonal>0.0)||!std::isfinite(diagonal))return {};
        allowed_error=std::pow(diagonal*max_error_ratio,2.0);
        original_quadrics.resize(points.size());
    }
    // Recompute topology/QEM after each parallel independent collapse
    // batch. In particular, do not operate on stale edges after a merge.
    for(unsigned pass=0;pass<64U && remaining>target;++pass) {
        std::set<edge> edges;
        std::vector<quadric> q(points.size());
        std::vector<std::set<uint32_t>> neighbors(points.size());
        struct edge_faces { unsigned count=0;size_t first=0,second=0; };
        std::map<edge,edge_faces> edge_data;
        std::vector<std::vector<size_t>> incident;
        std::vector<std::array<double,3>> normals;
        std::vector<uint8_t> pinned,feature_vertex;
        std::map<edge,uint8_t> feature_edges;
        using feature_neighbor=std::pair<uint32_t,uint8_t>;
        std::vector<std::vector<feature_neighbor>> feature_neighbors;
        if(quality) {
            incident.resize(points.size());
            normals.resize(faces.size());
            pinned=locked;
            feature_vertex.resize(points.size());
            feature_neighbors.resize(points.size());
        }
        for(size_t face_id=0;face_id<faces.size();++face_id) {
            auto const &f=faces[face_id];
            if(!f.live)continue;
            uint32_t const a=f.points[0],b=f.points[1],c=f.points[2];
            for(auto const &e:{make_edge(a,b),make_edge(b,c),make_edge(c,a)}) {
                edges.insert(e);
                if(quality) {
                    auto &info=edge_data[e];
                    if(info.count==0U)info.first=face_id;
                    else if(info.count==1U)info.second=face_id;
                    ++info.count;
                }
                neighbors[e.first].insert(e.second);
                neighbors[e.second].insert(e.first);
            }
            if(quality) {
                for(uint32_t point:f.points)incident[point].push_back(face_id);
                normals[face_id]=qem_cross(points[a],points[b],points[c]);
                if(qem_length2(normals[face_id])<=1.e-24)
                    pinned[a]=pinned[b]=pinned[c]=1U;
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
        if(quality) {
            if(pass==0)original_quadrics=q;
            for(auto const &entry:edge_data) {
                auto const &e=entry.first;
                auto const &info=entry.second;
                uint8_t kind=0U;
                if(info.count==1U)kind|=1U; // open silhouette
                if(info.count>2U)kind|=8U; // nonmanifold, never collapse
                if(info.count==2U) {
                    auto const &a=faces[info.first],&b=faces[info.second];
                    auto const &ga=source.groups[a.group];
                    auto const &gb=source.groups[b.group];
                    if(ga.material!=gb.material||ga.cel!=gb.cel||
                       ga.face_type!=gb.face_type||
                       ga.solid_color!=gb.solid_color||
                       ga.saber_trail_join!=gb.saber_trail_join||
                       ga.saber_trail_tip_bridge!=gb.saber_trail_tip_bridge)
                        kind|=2U; // material/alpha draw boundary
                    auto const &na=normals[info.first],&nb=normals[info.second];
                    double const la=qem_length2(na),lb=qem_length2(nb);
                    if(!(la>1.e-24&&lb>1.e-24) ||
                       na[0]*nb[0]+na[1]*nb[1]+na[2]*nb[2]<
                           0.7071067811865476*std::sqrt(la*lb))
                        kind|=4U; // crease sharper than 45 degrees
                }
                if(kind!=0U) {
                    feature_edges[e]=kind;
                    feature_neighbors[e.first].push_back({e.second,kind});
                    feature_neighbors[e.second].push_back({e.first,kind});
                }
            }
            for(uint32_t i=0;i<points.size();++i) {
                auto const &chain=feature_neighbors[i];
                if(chain.empty())continue;
                feature_vertex[i]=1U;
                // Only genuine corners, feature intersections/endpoints,
                // and nonmanifold geometry are immovable. Intermediate
                // vertices along a straight boundary or crease may go.
                if(chain.size()!=2U ||
                   chain[0].second!=chain[1].second ||
                   (chain[0].second&8U)!=0U) {
                    pinned[i]=1U;
                    continue;
                }
                auto const &p=points[i];
                auto const &a=points[chain[0].first];
                auto const &b=points[chain[1].first];
                double const ax=double(a[0])-p[0],ay=double(a[1])-p[1],
                             az=double(a[2])-p[2];
                double const bx=double(b[0])-p[0],by=double(b[1])-p[1],
                             bz=double(b[2])-p[2];
                double const dot=ax*bx+ay*by+az*bz;
                double const len2=(ax*ax+ay*ay+az*az)*
                                  (bx*bx+by*by+bz*bz);
                // >~26 degrees of turn remains a hard silhouette corner.
                if(!(len2>1.e-24) ||
                   dot>-0.9*std::sqrt(len2))
                    pinned[i]=1U;
            }
            if(pass==0)locked=pinned; // preserve original hard corners
        }

        std::vector<candidate> options;
        for(auto const &entry:edges) {
            uint32_t const a=entry.first,b=entry.second;
            if(a==b)continue;
            bool keep_first=false;
            uint8_t kind=0U;
            if(quality) {
                auto const &info=edge_data.at(entry);
                if(info.count>2U || (pinned[a]&&pinned[b]))continue;
                auto const feature=feature_edges.find(entry);
                kind=feature==feature_edges.end()?0U:feature->second;
                // Never bridge unrelated silhouette or crease chains.
                if(kind==0U && feature_vertex[a]&&feature_vertex[b])
                    continue;
                if(kind!=0U && ((kind&8U)!=0U ||
                                 !feature_vertex[a]||!feature_vertex[b]))
                    continue;
                // Manifold boundary edges remove one face; interior edges
                // remove two. Both satisfy their respective link condition.
                unsigned shared=0U;
                for(uint32_t n:neighbors[a])
                    if(neighbors[b].count(n)!=0)++shared;
                if(shared!=(info.count==1U?1U:2U))continue;
            }
            quadric combined=quality?original_quadrics[a]:q[a];
            combined+=(quality?original_quadrics[b]:q[b]);
            lod_point const mid=midpoint(points[a],points[b]);
            lod_point chosen=mid;
            double error=combined.error(mid);
            if(!std::isfinite(error))continue;
            if(quality) {
                // Feature vertices remain ON their original contour. Choose
                // which endpoint survives rather than repositioning either
                // into the interior of the model.
                if(feature_vertex[a]||feature_vertex[b]) {
                    if(feature_vertex[a]&&!feature_vertex[b])
                        keep_first=true;
                    else if(!feature_vertex[a]&&feature_vertex[b])
                        keep_first=false;
                    else if(pinned[a])keep_first=true;
                    else if(pinned[b])keep_first=false;
                    else keep_first=combined.error(points[a])<=
                                    combined.error(points[b]);
                    chosen=keep_first?points[a]:points[b];
                    error=combined.error(chosen);
                } else {
                    if(pinned[a]||pinned[b])continue;
                    error=std::max(0.0,error);
                    auto consider=[&](lod_point const &p) {
                        double const candidate_error=combined.error(p);
                        if(std::isfinite(candidate_error)&&candidate_error<error) {
                            error=std::max(0.0,candidate_error);
                            chosen=p;
                        }
                    };
                    consider(points[a]);consider(points[b]);
                    lod_point optimum{};
                    if(qem_optimal(combined,optimum)) {
                        bool local=true;
                        for(unsigned k=0;k<3;++k) {
                            double const low=std::min(points[a][k],points[b][k]);
                            double const high=std::max(points[a][k],points[b][k]);
                            double const margin=(high-low)*0.25+diagonal*1.e-6;
                            local&=optimum[k]>=low-margin&&
                                   optimum[k]<=high+margin;
                        }
                        if(local)consider(optimum);
                    }
                }
                if(!std::isfinite(error) ||
                   std::max(0.0,error)>allowed_error+1.e-12)continue;
                if(kind!=0U) {
                    uint32_t const removed=keep_first?b:a;
                    uint32_t const survivor=keep_first?a:b;
                    // A straight chain is cheap to simplify, but a curved
                    // contour must additionally satisfy the error limit in
                    // geometric space, even on a perfectly flat face.
                    bool found_other=false;
                    for(auto const &n:feature_neighbors[removed]) {
                        if(n.first==survivor)continue;
                        if(qem_point_segment_distance2(
                               points[removed],points[n.first],
                               points[survivor])>allowed_error+1.e-12) {
                            found_other=false;
                            break;
                        }
                        found_other=true;
                    }
                    if(!found_other)continue;
                }
            }
            options.push_back({entry,chosen,
                std::max(error,0.0)+
                1.e-8*distance2(points[a],points[b]),keep_first});
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
            size_t expected_removed=2U;
            if(quality && edge_data.at(choice.endpoints).count==1U)
                expected_removed=1U;
            if(conflict ||
               (quality && (!qem_safe_collapse(
                                 a,b,choice.position,points,faces,incident,
                                 expected_removed) ||
                            remaining<=expected_removed)))continue;
            touched[a]=touched[b]=1U;
            for(uint32_t n:neighbors[a])touched[n]=1U;
            for(uint32_t n:neighbors[b])touched[n]=1U;
            uint32_t const keep=quality&&choice.keep_first?a:b;
            uint32_t const discard=quality&&choice.keep_first?b:a;
            remap[discard]=keep;
            points[keep]=choice.position;
            if(quality)original_quadrics[keep]+=original_quadrics[discard];
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
        // A better-fit QEM vertex can move outside an original per-group
        // bound. Recompute output bounds to keep camera/frustum culling safe.
        if(quality && next.count) {
            auto const &first=out->vertices[out->indices[next.offset]];
            next.bounds_min={{first.x,first.y,first.z}};
            next.bounds_max=next.bounds_min;
            for(size_t i=next.offset;i<next.offset+next.count;++i) {
                auto const &v=out->vertices[out->indices[i]];
                for(unsigned axis=0;axis<3;++axis) {
                    float const component=axis==0?v.x:(axis==1?v.y:v.z);
                    next.bounds_min[axis]=std::min(next.bounds_min[axis],component);
                    next.bounds_max[axis]=std::max(next.bounds_max[axis],component);
                }
            }
        }
        // Original JK face-plane CPU backface culling no longer exactly
        // describes collapsed faces. Fail open for affected groups; leave
        // all unmodified groups with their original CPU culling.
        if(modified)next.plane_count=0U;
        if(next.count)out->groups.push_back(next);
    }
    return out;
}
} // namespace jkgm
