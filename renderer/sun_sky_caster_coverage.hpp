#pragma once
// Sky-first Phase 1B: comparative CPU-only estimates at 32/64/128 tiles.
// Conservative convex hulls (supersets of projected polygons) avoid most
// bounding-rectangle false positives. These tests never prune casters.
#include "sun_shadow.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace jkgm {

struct sun_sky_caster_coverage {
    static constexpr int grid_side=32;
    static constexpr int max_grid_side=128;
    static constexpr int tile_count=grid_side*grid_side;

    struct tile_rect {
        int x0=0,y0=0,x1=-1,y1=-1;
        bool unknown=false;
        bool intersects() const {return x0<=x1 && y0<=y1;}
        unsigned area() const {
            return intersects()?unsigned((x1-x0+1)*(y1-y0+1)):0U;
        }
    };
    struct projected_point {double x=0.0,y=0.0;};
    struct projected_shape {
        tile_rect rect{};
        std::vector<projected_point> hull;
        bool unknown=false;
    };
    struct overlap_result {
        bool legacy=false,precise=false,unknown=false;
        unsigned rect_tiles=0;
    };

    sun_shadow_projection view{};
    int side=grid_side;
    std::vector<uint8_t> tiles=std::vector<uint8_t>(tile_count,0U);
    std::vector<uint8_t> bbox_tiles=std::vector<uint8_t>(tile_count,0U);
    unsigned active_tiles=0,bbox_active_tiles=0;
    bool ready=false;
    bool fail_open=false;

    void reset(sun_shadow_projection const &projection,
               int requested_side=grid_side)
    {
        view=projection;
        side=requested_side==64 || requested_side==128
            ?requested_side:grid_side;
        tiles.assign(size_t(side)*size_t(side),0U);
        bbox_tiles.assign(size_t(side)*size_t(side),0U);
        active_tiles=bbox_active_tiles=0;
        fail_open=false;
        ready=view.width>0.0f && view.height>0.0f &&
            std::isfinite(view.width) && std::isfinite(view.height) &&
            decal_finite(view.origin) && decal_finite(view.right) &&
            decal_finite(view.up);
        if(!ready)mark_all();
    }

    // Malformed source geometry is always counted as potentially relevant.
    void mark_all()
    {
        std::fill(tiles.begin(),tiles.end(),1U);
        std::fill(bbox_tiles.begin(),bbox_tiles.end(),1U);
        active_tiles=bbox_active_tiles=unsigned(tiles.size());
        fail_open=true;
    }

    tile_rect projected_bounds(decal_vec3 const *points,size_t count,
                               int padding_tiles=1) const
    {
        return project(points,count,padding_tiles).rect;
    }

    void include_polygon(std::vector<decal_vec3> const &polygon)
    {
        if(polygon.size()<3)return;
        if(fail_open)return;
        auto shape=project(polygon.data(),polygon.size(),1);
        if(shape.unknown){mark_all();return;}
        auto const &r=shape.rect;
        if(!r.intersects())return;
        for(int y=r.y0;y<=r.y1;++y)
            for(int x=r.x0;x<=r.x1;++x) {
                size_t const index=size_t(y*side+x);
                if(bbox_tiles[index]==0U) {
                    bbox_tiles[index]=1U;
                    ++bbox_active_tiles;
                }
                if((shape.hull.size()<3 ||
                    hull_hits_tile(shape.hull,x,y,1)) &&
                    tiles[index]==0U) {
                    tiles[index]=1U;
                    ++active_tiles;
                }
            }
    }

    overlap_result evaluate_polygon(
        std::vector<decal_vec3> const &polygon) const
    {
        if(polygon.size()<3)return {true,true,true,0U};
        return evaluate(project(polygon.data(),polygon.size(),1));
    }

    overlap_result evaluate_aabb(decal_volume_aabb const &bounds) const
    {
        if(!decal_finite(bounds.low) || !decal_finite(bounds.high))
            return {true,true,true,0U};
        for(int axis=0;axis<3;++axis)
            if(bounds.low[axis]>bounds.high[axis])
                return {true,true,true,0U};
        std::array<decal_vec3,8> corners{};
        for(int c=0;c<8;++c)
            corners[size_t(c)]={{
                (c&1)?bounds.high[0]:bounds.low[0],
                (c&2)?bounds.high[1]:bounds.low[1],
                (c&4)?bounds.high[2]:bounds.low[2]}};
        return evaluate(project(corners.data(),corners.size(),1));
    }

    bool intersects_polygon(std::vector<decal_vec3> const &polygon) const
    {
        return evaluate_polygon(polygon).precise;
    }

    bool intersects_aabb(decal_volume_aabb const &bounds) const
    {
        return evaluate_aabb(bounds).precise;
    }

private:
    static double cross(projected_point const &a,
                        projected_point const &b,
                        projected_point const &p)
    {
        return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);
    }

    // Monotone convex hull deliberately OVER-approximates concave and
    // self-intersecting authored surfaces: it may keep extra casters but
    // cannot remove a region actually inside the original polygon.
    static std::vector<projected_point> convex_hull(
        std::vector<projected_point> points)
    {
        std::sort(points.begin(),points.end(),
            [](projected_point const &a,projected_point const &b) {
                return a.x<b.x || (a.x==b.x && a.y<b.y);
            });
        points.erase(std::unique(points.begin(),points.end(),
            [](projected_point const &a,projected_point const &b) {
                return a.x==b.x && a.y==b.y;
            }),points.end());
        if(points.size()<3)return points;
        std::vector<projected_point> hull;
        hull.reserve(points.size()*2);
        for(auto const &p:points) {
            while(hull.size()>=2 &&
                  cross(hull[hull.size()-2],hull.back(),p)<=0.0)
                hull.pop_back();
            hull.push_back(p);
        }
        size_t const lower=hull.size();
        for(size_t i=points.size()-1;i>0;--i) {
            auto const &p=points[i-1];
            while(hull.size()>lower &&
                  cross(hull[hull.size()-2],hull.back(),p)<=0.0)
                hull.pop_back();
            hull.push_back(p);
        }
        hull.pop_back();
        return hull;
    }

    projected_shape project(decal_vec3 const *points,size_t count,
                            int padding_tiles) const
    {
        projected_shape result{};
        if(!ready || points==nullptr || count==0) {
            result.unknown=true;
            result.rect={0,0,side-1,side-1,true};
            return result;
        }
        double min_x=std::numeric_limits<double>::infinity();
        double min_y=min_x,max_x=-min_x,max_y=-min_x;
        std::vector<projected_point> projected;
        projected.reserve(count);
        for(size_t i=0;i<count;++i) {
            if(!decal_finite(points[i])) {
                result.unknown=true;
                result.rect={0,0,side-1,side-1,true};
                return result;
            }
            double const dx=double(points[i][0])-view.origin[0];
            double const dy=double(points[i][1])-view.origin[1];
            double const dz=double(points[i][2])-view.origin[2];
            double const x=(dx*view.right[0]+dy*view.right[1]+
                            dz*view.right[2])/double(view.width);
            double const y=(dx*view.up[0]+dy*view.up[1]+
                            dz*view.up[2])/double(view.height);
            if(!std::isfinite(x) || !std::isfinite(y)) {
                result.unknown=true;
                result.rect={0,0,side-1,side-1,true};
                return result;
            }
            projected.push_back({x,y});
            min_x=std::min(min_x,x);max_x=std::max(max_x,x);
            min_y=std::min(min_y,y);max_y=std::max(max_y,y);
        }
        if(max_x<0.0 || min_x>1.0 || max_y<0.0 || min_y>1.0)
            return result;
        auto coord=[&](double v) {
            return int(std::floor(std::max(-1.0,std::min(2.0,v))*
                                  double(side)));
        };
        result.rect.x0=std::max(0,coord(min_x)-padding_tiles);
        result.rect.y0=std::max(0,coord(min_y)-padding_tiles);
        result.rect.x1=std::min(side-1,coord(max_x)+padding_tiles);
        result.rect.y1=std::min(side-1,coord(max_y)+padding_tiles);
        result.hull=convex_hull(std::move(projected));
        return result;
    }

    // Separating-axis test for a CCW convex hull and a tile expanded
    // by one cell as a conservative safety margin (including boundaries).
    bool hull_hits_tile(std::vector<projected_point> const &hull,
                        int x,int y,int padding_tiles) const
    {
        double const lo_x=double(x-padding_tiles)/double(side);
        double const lo_y=double(y-padding_tiles)/double(side);
        double const hi_x=double(x+1+padding_tiles)/double(side);
        double const hi_y=double(y+1+padding_tiles)/double(side);
        std::array<projected_point,4> const corners{{
            {lo_x,lo_y},{hi_x,lo_y},{hi_x,hi_y},{lo_x,hi_y}}};
        for(size_t i=0;i<hull.size();++i) {
            auto const &a=hull[i],&b=hull[(i+1)%hull.size()];
            double max_side=-std::numeric_limits<double>::infinity();
            for(auto const &p:corners)
                max_side=std::max(max_side,cross(a,b,p));
            if(max_side< -1.0e-9)return false;
        }
        return true;
    }

    bool intersects_rect(tile_rect const &r,
                         std::vector<uint8_t> const &mask) const
    {
        if(!r.intersects())return false;
        for(int y=r.y0;y<=r.y1;++y)
            for(int x=r.x0;x<=r.x1;++x)
                if(mask[size_t(y*side+x)]!=0U)return true;
        return false;
    }

    overlap_result evaluate(projected_shape const &shape) const
    {
        overlap_result rv{};
        rv.rect_tiles=shape.rect.area();
        rv.unknown=shape.unknown;
        if(fail_open || !ready || shape.unknown) {
            rv.legacy=rv.precise=true;
            return rv;
        }
        rv.legacy=intersects_rect(shape.rect,bbox_tiles);
        if(!shape.rect.intersects())return rv;
        for(int y=shape.rect.y0;y<=shape.rect.y1;++y)
            for(int x=shape.rect.x0;x<=shape.rect.x1;++x)
                if(tiles[size_t(y*side+x)]!=0U &&
                   (shape.hull.size()<3 ||
                    hull_hits_tile(shape.hull,x,y,1))) {
                    rv.precise=true;
                    return rv;
                }
        return rv;
    }
};

} // namespace jkgm
