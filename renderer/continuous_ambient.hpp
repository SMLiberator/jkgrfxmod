#pragma once
// CPU sparse world-aligned, portal-stitched continuous ambient field.
// Native capture is in continuous_ambient_capture.hpp; the GPU transport
// and vertex shader consume this immutable field and live sector floors.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jkgm {

struct continuous_ambient_stats {
    uint32_t source_vertices=0, raw_nodes=0, final_nodes=0;
    uint32_t portal_triangles=0, stitched_locations=0, shared_nodes=0;
    uint32_t skipped_oversized_portals=0, fallback_sectors=0;
};

class continuous_ambient_field {
public:
    using point=std::array<float,3>;
    using coord=std::array<int32_t,3>;
private:
    struct node_key {
        uint32_t sector=0;
        coord at{};
        bool operator==(node_key const &b) const {
            return sector==b.sector && at==b.at;
        }
    };
    struct coord_hash {
        size_t operator()(coord const &p) const noexcept {
            uint64_t h=14695981039346656037ULL;
            for(int32_t v:p) {h^=uint32_t(v);h*=1099511628211ULL;}
            return static_cast<size_t>(h);
        }
    };
    struct node_hash {
        size_t operator()(node_key const &k) const noexcept {
            size_t h=coord_hash{}(k.at);
            h^=size_t(k.sector)+size_t(0x9e3779b9U)+(h<<6U)+(h>>2U);
            return h;
        }
    };
    struct accumulation {
        double weighted_sum=0,weight=0;
    };
    struct triangle {
        uint32_t a=0,b=0;
        std::array<point,3> vertices{};
    };
    struct authored_source {
        point position{};
        float light=0.0f;
    };
    struct source_sector {
        point lo{{std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max()}};
        point hi{{std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest()}};
        std::vector<authored_source> points;
    };
    struct fallback_sector {
        point lo{},hi{};
        std::array<uint8_t,8> corners{};
        float sample(point const &position,float range) const {
            std::array<double,3> t{};
            for(int d=0;d<3;++d) {
                double const span=double(hi[d])-lo[d];
                t[d]=span>1e-6
                    ? std::clamp((double(position[d])-lo[d])/span,0.0,1.0)
                    : 0.5;
            }
            double raw=0.0;
            for(unsigned corner=0;corner<8;++corner) {
                double w=1.0;
                for(int d=0;d<3;++d)
                    w*=((corner>>d)&1U)?t[d]:1.0-t[d];
                raw+=w*(double(corners[corner])/255.0)*range;
            }
            return float(raw);
        }
    };
    struct node {
        uint8_t brightness=0;
    };
    using edges=std::vector<std::pair<uint32_t,uint32_t>>;
    std::unordered_map<node_key,accumulation,node_hash> accumulating_;
    std::unordered_map<node_key,node,node_hash> nodes_;
    // Sparse world nodes retain detail near authored surfaces. Large empty
    // sector interiors use a bounded 8-corner low-frequency analytic value,
    // rather than allocating a potentially enormous 3D box of cells.
    std::unordered_map<uint32_t,source_sector> authored_;
    std::unordered_map<uint32_t,fallback_sector> fallback_;
    // Only stitched nodes store this information. Other nodes use their
    // own sector's live floor; no permanent ExtraLight is baked.
    std::unordered_map<node_key,std::vector<uint32_t>,node_hash> floor_owners_;
    std::vector<triangle> portals_;
    continuous_ambient_stats stats_{};
    float spacing_=1.0f,range_=1.0f;
    bool ready_=false;
    static constexpr size_t max_nodes=2000000U;

    static double sq(double x){return x*x;}
    static std::array<double,3> difference(point const &a,point const &b) {
        return {{double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]}};
    }
    static double dot(std::array<double,3> const &a,
                      std::array<double,3> const &b) {
        return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    }
    static std::array<double,3> subtract(std::array<double,3> const &a,
                                          std::array<double,3> const &b) {
        return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}};
    }
    static double segment_distance2(point const &p,point const &u,
                                    point const &v) {
        auto const a=difference(v,u),b=difference(p,u);
        double const aa=dot(a,a);
        double const t=aa>1e-20?std::clamp(dot(a,b)/aa,0.0,1.0):0.0;
        auto const d=subtract(b,{{a[0]*t,a[1]*t,a[2]*t}});
        return dot(d,d);
    }
    static double triangle_distance2(point const &p,
                                     std::array<point,3> const &t) {
        auto const ab=difference(t[1],t[0]);
        auto const ac=difference(t[2],t[0]);
        auto const ap=difference(p,t[0]);
        double const aa=dot(ab,ab),bb=dot(ab,ac),cc=dot(ac,ac);
        double const denom=aa*cc-bb*bb;
        if(denom>1e-20) {
            double const u=dot(ap,ab),v=dot(ap,ac);
            double const beta=(u*cc-v*bb)/denom;
            double const gamma=(v*aa-u*bb)/denom;
            if(beta>=0.0 && gamma>=0.0 && beta+gamma<=1.0) {
                auto const d=subtract(ap,{{beta*ab[0]+gamma*ac[0],
                    beta*ab[1]+gamma*ac[1],beta*ab[2]+gamma*ac[2]}});
                return dot(d,d);
            }
        }
        return std::min(segment_distance2(p,t[0],t[1]),
            std::min(segment_distance2(p,t[1],t[2]),
                     segment_distance2(p,t[2],t[0])));
    }
    static bool valid_position(point const &p) {
        for(float v:p)if(!std::isfinite(v))return false;
        return true;
    }
    bool grid_coord(point const &p,coord &base) const {
        if(!valid_position(p))return false;
        for(int i=0;i<3;++i) {
            double const x=std::floor(double(p[i])/double(spacing_));
            if(x < -1000000.0 || x > 1000000.0)return false;
            base[i]=static_cast<int32_t>(x);
        }
        return true;
    }
    point location(coord const &c) const {
        return {{float(double(c[0])*spacing_),float(double(c[1])*spacing_),
                 float(double(c[2])*spacing_)}};
    }
    // A local nearest-node fallback is used only to seed the shared portal
    // strip when a sector's authored surfaces are slightly farther away.
    // This does not expand illumination across nonportal walls.
    bool raw_near(std::unordered_map<node_key,accumulation,node_hash> const &original,
                  uint32_t sector,coord const &position,float &brightness) const {
        auto it=original.find({sector,position});
        if(it!=original.end() && it->second.weight>0.0) {
            brightness=float(it->second.weighted_sum/it->second.weight);
            return true;
        }
        double nearest=std::numeric_limits<double>::infinity();
        bool valid=false;
        for(int radius=1;radius<=3;++radius) {
            for(int x=-radius;x<=radius;++x)
            for(int y=-radius;y<=radius;++y)
            for(int z=-radius;z<=radius;++z) {
                if(std::max({std::abs(x),std::abs(y),std::abs(z)})!=radius)
                    continue;
                coord c{{position[0]+x,position[1]+y,position[2]+z}};
                auto found=original.find({sector,c});
                if(found==original.end() || found->second.weight<=0.0)
                    continue;
                double const dist=sq(double(x))+sq(double(y))+sq(double(z));
                if(dist>=nearest)continue;
                nearest=dist;
                brightness=float(found->second.weighted_sum/
                                 found->second.weight);
                valid=true;
            }
            if(valid)break;
        }
        return valid;
    }
    static uint8_t pack(float v,float range) {
        return static_cast<uint8_t>(std::lround(
            std::clamp(double(v)/double(range),0.0,1.0)*255.0));
    }
public:
    void reset(float spacing=1.0f,bool overbright=false) {
        accumulating_.clear();nodes_.clear();floor_owners_.clear();
        authored_.clear();fallback_.clear();
        portals_.clear();stats_={};ready_=false;
        spacing_=std::isfinite(spacing) && spacing>=0.25f && spacing<=4.0f
            ? spacing : 1.0f;
        range_=overbright?2.0f:1.0f;
    }
    float spacing() const {return spacing_;}
    float range() const {return range_;}
    bool ready() const {return ready_;}
    size_t node_count() const {return nodes_.size();}
    continuous_ambient_stats stats() const {return stats_;}

    // Read-only CPU->GPU staging API. No native pointers, GL objects or
    // assumptions about unordered_map iteration order escape this field.
    template<class Visit>
    void for_each_node(Visit &&visit) const {
        if(!ready_)return;
        for(auto const &entry:nodes_) {
            auto const owners=floor_owners_.find(entry.first);
            visit(entry.first.sector,entry.first.at,
                float(entry.second.brightness)/255.0f*range_,
                owners==floor_owners_.end() ? nullptr : &owners->second);
        }
    }
    bool get_fallback(uint32_t sector,point &lo,point &hi,
                      std::array<uint8_t,8> &corners) const {
        if(!ready_)return false;
        auto const found=fallback_.find(sector);
        if(found==fallback_.end())return false;
        lo=found->second.lo;
        hi=found->second.hi;
        corners=found->second.corners;
        return true;
    }

    bool add(uint32_t sector,point const &position,float authored_light) {
        if(ready_ || !std::isfinite(authored_light) ||
           stats_.source_vertices>=1000000U)return false;
        coord base{};
        if(!grid_coord(position,base))return false;
        // 27 neighboring world-aligned nodes; sparse splatting provides a
        // short local interpolation band without allocating a whole level.
        // Author data is never combined across sectors here.
        std::array<std::pair<node_key,double>,27> contributions{};
        size_t ncontributions=0,new_nodes=0;
        for(int x=-1;x<=1;++x)
        for(int y=-1;y<=1;++y)
        for(int z=-1;z<=1;++z) {
            coord at{{base[0]+x,base[1]+y,base[2]+z}};
            point const q=location(at);
            double const distance2=sq((double(q[0])-position[0])/spacing_)+
                sq((double(q[1])-position[1])/spacing_)+
                sq((double(q[2])-position[2])/spacing_);
            node_key const key{sector,at};
            if(accumulating_.find(key)==accumulating_.end())++new_nodes;
            contributions[ncontributions++]={key,1.0/(0.0625+distance2)};
        }
        // Reject atomically: never leave a partially splatted source.
        if(accumulating_.size()+new_nodes>max_nodes)return false;
        for(size_t i=0;i<ncontributions;++i) {
            auto const &contribution=contributions[i];
            auto &n=accumulating_[contribution.first];
            n.weighted_sum+=contribution.second*
                std::clamp(authored_light,0.0f,range_);
            n.weight+=contribution.second;
        }
        auto &sources=authored_[sector];
        for(int d=0;d<3;++d) {
            sources.lo[d]=std::min(sources.lo[d],position[d]);
            sources.hi[d]=std::max(sources.hi[d],position[d]);
        }
        sources.points.push_back({position,
            std::clamp(authored_light,0.0f,range_)});
        ++stats_.source_vertices;
        return true;
    }
    bool add_portal(uint32_t a,uint32_t b,
                    std::array<point,3> const &vertices) {
        if(ready_ || a==b || portals_.size()>=8192U)return false;
        for(auto const &p:vertices)if(!valid_position(p))return false;
        auto const ab=difference(vertices[1],vertices[0]);
        auto const ac=difference(vertices[2],vertices[0]);
        auto const cross=std::array<double,3>{{
            ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],
            ab[0]*ac[1]-ab[1]*ac[0]}};
        if(dot(cross,cross)<1e-16)return false;
        portals_.push_back({a,b,vertices});
        ++stats_.portal_triangles;
        return true;
    }
    bool finalize() {
        if(ready_)return false;
        // Sparse splats deliberately cover only a small area around
        // authored surfaces. Before discarding the source points, build
        // a cheap sector-wide, world-space 2x2x2 fallback for genuinely
        // empty interiors. These values are sampled analytically at
        // missing voxel corners; no dense interior allocation is needed.
        // Portal-reconciled nodes always override this fallback.
        fallback_.clear();
        for(auto const &entry:authored_) {
            auto const &input=entry.second;
            if(input.points.empty())continue;
            fallback_sector output{};
            output.lo=input.lo;output.hi=input.hi;
            for(unsigned corner=0;corner<8;++corner) {
                std::array<double,3> target{};
                for(int d=0;d<3;++d)
                    target[d]=((corner>>d)&1U)?input.hi[d]:input.lo[d];
                double sum=0.0,weights=0.0;
                for(auto const &src:input.points) {
                    double dist2=0.0;
                    for(int d=0;d<3;++d)
                        dist2+=sq(double(src.position[d])-target[d]);
                    double const w=1.0/std::max(0.0001,dist2);
                    sum+=w*src.light;weights+=w;
                }
                output.corners[corner]=pack(
                    float(weights>0.0?sum/weights:0.0),range_);
            }
            fallback_.emplace(entry.first,output);
        }
        stats_.fallback_sectors=static_cast<uint32_t>(fallback_.size());
        authored_.clear(); // No large source arrays remain during gameplay.
        stats_.raw_nodes=static_cast<uint32_t>(accumulating_.size());
        // Never feed already-stitched values into later portal calculations.
        // This immutable source snapshot makes stitching order-independent.
        auto original=std::move(accumulating_);
        std::unordered_map<node_key,accumulation,node_hash> stitched;
        // Global coordinate -> sector edges. Edges are unioned before
        // reconciliation so A-B-C portal junctions agree in ALL sectors,
        // regardless of capture/iteration order.
        std::unordered_map<coord,edges,coord_hash> shared;
        double const band2=3.0*sq(double(spacing_));
        for(auto const &p:portals_) {
            coord low{},high{};
            bool valid=true;
            for(int d=0;d<3;++d) {
                double lo=p.vertices[0][d],hi=lo;
                for(int i=1;i<3;++i) {
                    lo=std::min(lo,double(p.vertices[i][d]));
                    hi=std::max(hi,double(p.vertices[i][d]));
                }
                // sqrt(3) spacing includes every corner of a voxel that
                // intersects the portal triangle, even on an oblique plane.
                double const margin=std::sqrt(3.0)*spacing_;
                double const from=std::floor((lo-margin)/spacing_);
                double const to=std::ceil((hi+margin)/spacing_);
                if(from < -1000000 || to > 1000000 || to<from) {
                    valid=false;break;
                }
                low[d]=int32_t(from);high[d]=int32_t(to);
            }
            if(!valid) {++stats_.skipped_oversized_portals;continue;}
            uint64_t count=1U;
            for(int d=0;d<3;++d)count*=uint64_t(high[d]-low[d]+1);
            if(count>16384U) {++stats_.skipped_oversized_portals;continue;}
            for(int x=low[0];x<=high[0];++x)
            for(int y=low[1];y<=high[1];++y)
            for(int z=low[2];z<=high[2];++z) {
                coord const at{{x,y,z}};
                if(triangle_distance2(location(at),p.vertices)>band2+1e-8)
                    continue;
                shared[at].push_back({p.a,p.b});
            }
        }
        for(auto const &group:shared) {
            std::vector<uint32_t> all;
            for(auto const &e:group.second) {
                all.push_back(e.first);all.push_back(e.second);
            }
            std::sort(all.begin(),all.end());
            all.erase(std::unique(all.begin(),all.end()),all.end());
            if(all.size()<2U)continue;
            // Group connected sectors at this particular voxel only.
            std::vector<size_t> parent(all.size());
            for(size_t i=0;i<all.size();++i)parent[i]=i;
            auto root=[&](size_t i) {
                while(parent[i]!=i)i=parent[i];
                return i;
            };
            for(auto const &e:group.second) {
                size_t a=std::lower_bound(all.begin(),all.end(),e.first)-all.begin();
                size_t b=std::lower_bound(all.begin(),all.end(),e.second)-all.begin();
                parent[root(b)]=root(a);
            }
            for(size_t seed=0;seed<all.size();++seed) {
                if(root(seed)!=seed)continue;
                std::vector<uint32_t> members;
                for(size_t i=0;i<all.size();++i)
                    if(root(i)==seed)members.push_back(all[i]);
                if(members.size()<2U)continue;
                double sum=0.0;size_t found=0U;
                for(uint32_t sector:members) {
                    float light=0.0f;
                    if(!raw_near(original,sector,group.first,light))continue;
                    sum+=light;++found;
                }
                if(!found)continue;
                float const common=float(sum/double(found));
                size_t missing=0;
                for(uint32_t sector:members) {
                    node_key const key{sector,group.first};
                    if(original.find(key)==original.end() &&
                       stitched.find(key)==stitched.end())++missing;
                }
                // Treat one connected component atomically, or the cap
                // could produce only one side of a supposedly shared node.
                if(original.size()+stitched.size()+missing>max_nodes)continue;
                for(uint32_t sector:members) {
                    node_key const key{sector,group.first};
                    // Preserve the shared NODE value for all linked sectors;
                    // live sector floors are still separately evaluated.
                    stitched[key]={double(common),1.0};
                    floor_owners_[key]=members;
                    ++stats_.shared_nodes;
                }
                ++stats_.stitched_locations;
            }
        }
        nodes_.reserve(original.size()+stitched.size());
        for(auto const &entry:original) {
            auto const &acc=entry.second;
            if(acc.weight<=0.0 || !std::isfinite(acc.weighted_sum))continue;
            nodes_.emplace(entry.first,node{pack(
                float(acc.weighted_sum/acc.weight),range_)});
        }
        for(auto const &entry:stitched)
            nodes_[entry.first]=node{pack(float(entry.second.weighted_sum),range_)};
        stats_.final_nodes=static_cast<uint32_t>(nodes_.size());
        accumulating_.clear();ready_=true;
        return true;
    }

    // Live floors are provided separately by native sector state. This
    // API never asks the caller to recapture authored lighting during fades.
    template<class FloorLookup>
    bool sample(uint32_t sector,point const &position,
                FloorLookup const &floor_lookup,float &light) const {
        if(!ready_)return false;
        coord base{};
        if(!grid_coord(position,base))return false;
        std::array<double,3> t{};
        for(int d=0;d<3;++d)
            t[d]=std::clamp(double(position[d])/spacing_-base[d],0.0,1.0);
        double sum=0.0,weights=0.0;
        for(int i=0;i<8;++i) {
            coord const at{{base[0]+(i&1),base[1]+((i>>1)&1),
                            base[2]+((i>>2)&1)}};
            node_key const key{sector,at};
            auto found=nodes_.find(key);
            auto fallback=found==nodes_.end()
                ? fallback_.find(sector) : fallback_.end();
            if(found==nodes_.end() && fallback==fallback_.end())
                continue; // no authored sector or valid stitched cell
            double weight=1.0;
            for(int d=0;d<3;++d)
                weight*=((i>>d)&1)?t[d]:1.0-t[d];
            if(weight<=0.0)continue;
            float const raw=found!=nodes_.end()
                ? float(found->second.brightness)/255.0f*range_
                : fallback->second.sample(location(at),range_);
            float lit=0.0f;
            auto owners=floor_owners_.find(key);
            if(owners==floor_owners_.end()) {
                float floor=0.0f;
                if(!floor_lookup(sector,floor) || !std::isfinite(floor))
                    return false;
                lit=std::max(raw,std::clamp(floor,0.0f,range_));
            } else {
                float total=0.0f;
                for(uint32_t owner:owners->second) {
                    float floor=0.0f;
                    if(!floor_lookup(owner,floor) || !std::isfinite(floor))
                        return false;
                    total+=std::max(raw,std::clamp(floor,0.0f,range_));
                }
                lit=total/float(owners->second.size());
            }
            sum+=weight*lit;weights+=weight;
        }
        if(weights<=1e-12)return false;
        light=float(sum/weights);
        return true;
    }
};

} // namespace jkgm
