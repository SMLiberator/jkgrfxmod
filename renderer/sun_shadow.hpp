#pragma once
// Orthographic directional-sun shadow projection math.
// Pure CPU helpers shared by the depth pass and deferred lighting pass.
#include "spotlight_shadow.hpp"
#include "sun.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <unordered_set>
#include <utility>

namespace jkgm {

// Only atlas-rendered owners leave the broad cascades. Unknown owners
// and overflowing/unrendered Things retain their low-res shadow fallback.
inline bool sun_shadow_caster_in_static_cascades(
    uint64_t owner,std::unordered_set<uint64_t> const &atlas_owners)
{
    return owner==0 || atlas_owners.find(owner)==atlas_owners.end();
}

// A stable, verified SithThing may join the cached sunlight layer. Actors
// in physics/path motion and unverified objects stay in the working cascade.
// A supposedly stationary Thing changing pose will invalidate that layer
// through its transform/mesh/material signature.
inline bool sun_shadow_cache_stationary_model(
    uint64_t shadow_owner,bool dynamic_motion,
    bool enabled,bool target_complete)
{
    return enabled && target_complete && shadow_owner!=0 &&
           !dynamic_motion;
}

// A material can affect the sun projection's depth range only when its
// geometry actually writes shadow depth. In particular, legacy scaled VFX
// with translucent, special or emissive-only MATs must not invalidate the
// entire directional shadow map simply by entering the camera's view.
inline bool sun_shadow_material_casts_depth(
    uint32_t albedo_source,bool special,bool emissive_only,
    float opacity,bool cutout)
{
    return albedo_source!=0U && !special && !emissive_only &&
           opacity>=0.999f && (!cutout || albedo_source==1U);
}

// The global shadow projection must describe stable level geometry, not
// moving Things or legacy VFX with scaled/sheared R/L/U axes. Those Things
// remain perfectly valid depth casters and retain their exact affine matrices;
// only their *contribution to the shared light-space depth range* is omitted.
// Otherwise a single large scaled Thing can make both cascades lose precision
// or fail when it enters the camera view. Mirrored rigid transforms are valid.
inline bool sun_shadow_stable_reference_transform(
    std::array<float,12> const &m,bool dynamic_caster)
{
    if(dynamic_caster)return false;
    for(float x:m)if(!std::isfinite(x))return false;
    // Columns are Right/Look/Up; allow normal floating-point pose drift,
    // but don't reinterpret deliberate per-Thing scaling as camera/world
    // extents. No modification or normalization of the matrix is performed.
    for(int a=0;a<3;++a) {
        float len2=0.0f;
        for(int i=0;i<3;++i)len2+=m[3*a+i]*m[3*a+i];
        if(!std::isfinite(len2) || std::fabs(len2-1.0f)>0.02f)
            return false;
        for(int b=0;b<a;++b) {
            float dot=0.0f;
            for(int i=0;i<3;++i)dot+=m[3*a+i]*m[3*b+i];
            if(!std::isfinite(dot) || std::fabs(dot)>0.02f)
                return false;
        }
    }
    return true;
}

// Exact world-space AABB of an affine-transformed 3DO mesh group, using its
// center and half-extents. Computes nine absolute products instead of
// transforming eight corners per group (and doing it once per cascade).
inline decal_volume_aabb sun_shadow_transformed_aabb(
    std::array<float,3> const &minimum,
    std::array<float,3> const &maximum,
    std::array<float,12> const &transform)
{
    decal_volume_aabb bounds{};
    float center[3],extent[3];
    for(int i=0;i<3;++i) {
        center[i]=0.5f*(minimum[i]+maximum[i]);
        extent[i]=0.5f*(maximum[i]-minimum[i]);
    }
    for(int a=0;a<3;++a) {
        float const position=transform[a]*center[0]+
            transform[a+3]*center[1]+transform[a+6]*center[2]+transform[a+9];
        float const radius=std::fabs(transform[a])*extent[0]+
            std::fabs(transform[a+3])*extent[1]+
            std::fabs(transform[a+6])*extent[2];
        bounds.low[a]=position-radius;
        bounds.high[a]=position+radius;
    }
    return bounds;
}

// Optional hard range for added sunlight and its shadows. Zero disables it.
// Beyond the range JK's authored lighting is left untouched, so the far
// cascade never needs to reach past it.
inline float sun_shadow_effective_far_distance(float far_distance,
                                               float max_distance)
{
    if(!(max_distance>0.0f) || !std::isfinite(max_distance))return far_distance;
    return std::min(far_distance,max_distance);
}

// CPU mirror of sun_range_fade() in the lighting shaders: full strength to
// 85% of the range, smooth fade to zero at the range, zero beyond it.
inline float sun_range_fade(float camera_depth,float max_distance)
{
    if(!(max_distance>0.0f) || !std::isfinite(max_distance))return 1.0f;
    float const begin=0.85f*max_distance;
    float t=(camera_depth-begin)/(max_distance-begin);
    t=std::min(1.0f,std::max(0.0f,t));
    return 1.0f-t*t*(3.0f-2.0f*t);
}

// Two nested camera-depth regions. The far cascade overlaps the near
// cascade, so near receivers can fall back if a light-space edge is crossed.
constexpr float sun_shadow_near_distance=16.0f;
constexpr float sun_shadow_far_distance=128.0f;
constexpr float sun_shadow_blend_start=13.0f;
constexpr float sun_shadow_blend_end=15.0f;
struct sun_shadow_projection {
    decal_vec3 origin{},right{},up{},forward{};
    float width=1.0f,height=1.0f,range=1.0f;
};

struct sun_shadow_clip_point {
    float x=0.0f,y=0.0f,z=0.0f;
};

inline decal_vec3 sun_shadow_cross(decal_vec3 const &a,decal_vec3 const &b)
{
    return {{a[1]*b[2]-a[2]*b[1],
             a[2]*b[0]-a[0]*b[2],
             a[0]*b[1]-a[1]*b[0]}};
}

inline bool make_sun_shadow_projection(
    decal_world_geometry const &world,
    std::array<float,3> const &direction_toward_sun,
    sun_shadow_projection &out,
    std::vector<decal_volume_aabb> const &model_bounds={})
{
    if(!world.valid_world || (world.receivers.empty() && model_bounds.empty()) ||
       !std::isfinite(direction_toward_sun[0]) ||
       !std::isfinite(direction_toward_sun[1]) ||
       !std::isfinite(direction_toward_sun[2]))return false;

    auto const sun=normalized_sun_direction(direction_toward_sun);
    auto forward=decal_vec3{{-sun[0],-sun[1],-sun[2]}};
    decal_vec3 reference_up = std::fabs(forward[2])>0.95f
        ? decal_vec3{{0.0f,1.0f,0.0f}}
        : decal_vec3{{0.0f,0.0f,1.0f}};
    auto right=sun_shadow_cross(reference_up,forward);
    float const right_len=std::sqrt(decal_dot(right,right));
    if(!(right_len>1.0e-6f) || !std::isfinite(right_len))return false;
    for(float &v:right)v/=right_len;
    auto up=sun_shadow_cross(forward,right);

    float min_x=std::numeric_limits<float>::max();
    float min_y=min_x,min_z=min_x;
    float max_x=-min_x,max_y=-min_x,max_z=-min_x;
    bool found=false;
    auto include=[&](decal_vec3 const &point) {
            float const x=decal_dot(point,right);
            float const y=decal_dot(point,up);
            float const z=decal_dot(point,forward);
            min_x=std::min(min_x,x);max_x=std::max(max_x,x);
            min_y=std::min(min_y,y);max_y=std::max(max_y,y);
            min_z=std::min(min_z,z);max_z=std::max(max_z,z);
            found=true;
    };
    for(auto const &surface:world.receivers) {
        for(auto const &point:surface.polygon) {
            if(!decal_finite(point))return false;
            include(point);
        }
    }
    for(auto const &bounds:model_bounds) {
        if(!decal_finite(bounds.low) || !decal_finite(bounds.high))return false;
        for(int corner=0;corner<8;++corner) {
            decal_vec3 point{{(corner&1)?bounds.high[0]:bounds.low[0],
                              (corner&2)?bounds.high[1]:bounds.low[1],
                              (corner&4)?bounds.high[2]:bounds.low[2]}};
            include(point);
        }
    }
    if(!found)return false;

    float const raw_width=max_x-min_x;
    float const raw_height=max_y-min_y;
    float const raw_range=max_z-min_z;
    if(!(std::isfinite(raw_width)&&std::isfinite(raw_height)&&
         std::isfinite(raw_range)) || raw_width<0.0f ||
       raw_height<0.0f || raw_range<0.0f)return false;
    // A small perimeter protects edge receivers from clipping. The map spans
    // the full loaded level, so it remains stable as the camera moves.
    float const margin=std::max(1.0f,
        std::max(raw_width,std::max(raw_height,raw_range))*0.02f);
    out.right=right;out.up=up;out.forward=forward;
    out.origin={{right[0]*(min_x-margin)+up[0]*(min_y-margin)+
                     forward[0]*(min_z-margin),
                 right[1]*(min_x-margin)+up[1]*(min_y-margin)+
                     forward[1]*(min_z-margin),
                 right[2]*(min_x-margin)+up[2]*(min_y-margin)+
                     forward[2]*(min_z-margin)}};
    out.width=raw_width+2.0f*margin;
    out.height=raw_height+2.0f*margin;
    out.range=raw_range+2.0f*margin;
    return std::isfinite(out.width)&&std::isfinite(out.height)&&
           std::isfinite(out.range);
}

// Spend the fixed depth-map resolution on the camera's nearby view rather
// than the entire loaded level. Keep the full caster depth along sunlight:
// an object outside the view can still cast into the visible region.
inline bool fit_sun_shadow_to_camera(
    sun_shadow_projection &projection,
    std::array<float,3> const &position,
    std::array<float,3> const &camera_right,
    std::array<float,3> const &camera_up,
    std::array<float,3> const &camera_forward,
    float tan_half_horizontal,float tan_half_vertical,float distance)
{
    if(!(std::isfinite(tan_half_horizontal) && tan_half_horizontal>0.0f &&
         std::isfinite(tan_half_vertical) && tan_half_vertical>0.0f &&
         std::isfinite(distance) && distance>0.0f))return false;
    for(auto const &axis:{position,camera_right,camera_up,camera_forward})
        for(float v:axis)if(!std::isfinite(v))return false;
    float min_x=std::numeric_limits<float>::max(),min_y=min_x;
    float max_x=-min_x,max_y=-min_y;
    for(float depth:{0.0f,distance})
        for(int x:{-1,1})for(int y:{-1,1}) {
            decal_vec3 point{};
            for(int i=0;i<3;++i)
                point[i]=position[i]+depth*(camera_forward[i]+
                    x*tan_half_horizontal*camera_right[i]+
                    y*tan_half_vertical*camera_up[i]);
            float const u=decal_dot(point,projection.right);
            float const v=decal_dot(point,projection.up);
            min_x=std::min(min_x,u);max_x=std::max(max_x,u);
            min_y=std::min(min_y,v);max_y=std::max(max_y,v);
        }
    // Extra light-space area reduces churn on small camera movements and
    // lets shadow filtering sample across the view-frustum boundary.
    float const margin=std::max(2.0f,
        0.05f*std::max(max_x-min_x,max_y-min_y));
    min_x-=margin;min_y-=margin;
    float const old_z=decal_dot(projection.origin,projection.forward);
    for(int i=0;i<3;++i)
        projection.origin[i]=projection.right[i]*min_x+
            projection.up[i]*min_y+projection.forward[i]*old_z;
    projection.width=max_x-min_x+margin;
    projection.height=max_y-min_y+margin;
    return std::isfinite(projection.width) &&
           std::isfinite(projection.height);
}

// Keep a previously rendered directional map unchanged while the current
// camera's complete near/far frustum remains safely inside its light-space
// coverage. This saves costly depth clears/draws without imposing a fixed
// update rate or allowing the view to outgrow a stale texture.
inline bool sun_shadow_projection_covers_camera(
    sun_shadow_projection const &p,
    std::array<float,3> const &position,
    std::array<float,3> const &camera_right,
    std::array<float,3> const &camera_up,
    std::array<float,3> const &camera_forward,
    float horizontal,float vertical,float distance,
    float margin=0.03f)
{
    if(!(p.width>0.0f && p.height>0.0f &&
         std::isfinite(p.width) && std::isfinite(p.height) &&
         std::isfinite(horizontal) && horizontal>0.0f &&
         std::isfinite(vertical) && vertical>0.0f &&
         std::isfinite(distance) && distance>0.0f &&
         std::isfinite(margin) && margin>=0.0f && margin<0.5f))
        return false;
    for(float d:{0.0f,distance})
        for(int x:{-1,1})for(int y:{-1,1}) {
            decal_vec3 point{};
            for(int axis=0;axis<3;++axis)
                point[axis]=position[axis]+d*(
                    camera_forward[axis]+x*horizontal*camera_right[axis]+
                    y*vertical*camera_up[axis]);
            auto const rel=decal_sub(point,p.origin);
            float const u=decal_dot(rel,p.right)/p.width;
            float const v=decal_dot(rel,p.up)/p.height;
            if(!std::isfinite(u) || !std::isfinite(v) ||
               u<margin || u>1.0f-margin ||
               v<margin || v>1.0f-margin)return false;
        }
    return true;
}

// A per-Thing light-space crop. Include receiver positions a few units down
// each ray without inflating to the entire camera frustum. Keep the full
// depth range of the level to avoid clipping the source geometry.
inline bool make_sun_dynamic_shadow_projection(
    sun_shadow_projection const &world,
    decal_volume_aabb const &bounds,
    sun_shadow_projection &out,float ray_length=4.0f)
{
    if(!decal_finite(bounds.low) || !decal_finite(bounds.high) ||
       !std::isfinite(ray_length) || ray_length<0.0f ||
       !(world.range>0.0f))return false;
    for(int i=0;i<3;++i)if(bounds.low[i]>bounds.high[i])return false;
    float lo_x=std::numeric_limits<float>::max(),lo_y=lo_x;
    float hi_x=-lo_x,hi_y=-lo_y;
    // Keep the world reference range unchanged unless this particular
    // atlas caster lies outside it. Extending the per-Thing range is safe:
    // it cannot degrade the shared near/far shadow depth textures.
    float const world_z=decal_dot(world.origin,world.forward);
    if(!std::isfinite(world_z) ||
       !std::isfinite(world_z+world.range))return false;
    float lo_z=world_z,hi_z=world_z+world.range;
    for(int c=0;c<8;++c) {
        decal_vec3 p{{(c&1)?bounds.high[0]:bounds.low[0],
                      (c&2)?bounds.high[1]:bounds.low[1],
                      (c&4)?bounds.high[2]:bounds.low[2]}};
        for(float t:{0.0f,ray_length}) {
            decal_vec3 q{{p[0]+world.forward[0]*t,
                          p[1]+world.forward[1]*t,
                          p[2]+world.forward[2]*t}};
            float const x=decal_dot(q,world.right);
            float const y=decal_dot(q,world.up);
            float const z=decal_dot(q,world.forward);
            if(!std::isfinite(x) || !std::isfinite(y) ||
               !std::isfinite(z))return false;
            lo_x=std::min(lo_x,x);hi_x=std::max(hi_x,x);
            lo_y=std::min(lo_y,y);hi_y=std::max(hi_y,y);
            lo_z=std::min(lo_z,z);hi_z=std::max(hi_z,z);
        }
    }
    float const margin=std::max(0.2f,0.05f*std::max(hi_x-lo_x,hi_y-lo_y));
    lo_x-=margin;lo_y-=margin;hi_x+=margin;hi_y+=margin;
    // Include the casting Thing and any direct downstream receiver even
    // when its scaled transform lies beyond the reference world range.
    lo_z=std::min(lo_z,world_z);
    hi_z=std::max(hi_z,world_z+world.range);
    out=world;
    for(int i=0;i<3;++i)out.origin[i]=
        world.right[i]*lo_x+world.up[i]*lo_y+world.forward[i]*lo_z;
    out.width=std::max(0.5f,hi_x-lo_x);
    out.height=std::max(0.5f,hi_y-lo_y);
    out.range=hi_z-lo_z;
    return decal_finite(out.origin) && std::isfinite(out.width) &&
           std::isfinite(out.height) && std::isfinite(out.range) &&
           out.range>0.0f;
}

// Cache keys must reject any change to a light-space orthographic mapping,
// including orientation, not just the snapped XY origin and dimensions.
inline bool sun_shadow_same_projection(
    sun_shadow_projection const &a,sun_shadow_projection const &b,
    float tolerance=1.0e-5f)
{
    if(!(tolerance>=0.0f) || !std::isfinite(tolerance))return false;
    for(auto const &vectors: {
        std::pair<decal_vec3,decal_vec3>{a.origin,b.origin},
        {a.right,b.right},{a.up,b.up},{a.forward,b.forward}}) {
        for(int i=0;i<3;++i) {
            if(!std::isfinite(vectors.first[i]) ||
               !std::isfinite(vectors.second[i]) ||
               std::fabs(vectors.first[i]-vectors.second[i])>tolerance)
                return false;
        }
    }
    return std::isfinite(a.width) && std::isfinite(b.width) &&
           std::isfinite(a.height) && std::isfinite(b.height) &&
           std::isfinite(a.range) && std::isfinite(b.range) &&
           std::fabs(a.width-b.width)<=tolerance &&
           std::fabs(a.height-b.height)<=tolerance &&
           std::fabs(a.range-b.range)<=tolerance;
}

// Snap orthographic translation to exact texels; static shadows then
// stay fixed during sub-texel camera translation.
inline bool stabilize_sun_shadow_projection(
    sun_shadow_projection &projection,unsigned resolution)
{
    if(resolution==0 || !(projection.width>0.0f) ||
       !(projection.height>0.0f) || !std::isfinite(projection.width) ||
       !std::isfinite(projection.height))return false;
    float const tx=projection.width/float(resolution);
    float const ty=projection.height/float(resolution);
    float const x=decal_dot(projection.origin,projection.right);
    float const y=decal_dot(projection.origin,projection.up);
    float const dx=std::floor(x/tx)*tx-x;
    float const dy=std::floor(y/ty)*ty-y;
    if(!std::isfinite(dx) || !std::isfinite(dy))return false;
    for(int i=0;i<3;++i)
        projection.origin[i]+=projection.right[i]*dx+projection.up[i]*dy;
    return decal_finite(projection.origin);
}

inline sun_shadow_clip_point sun_shadow_clip(
    sun_shadow_projection const &p,decal_vec3 const &world)
{
    auto const relative=decal_sub(world,p.origin);
    return {
        2.0f*decal_dot(relative,p.right)/p.width-1.0f,
        2.0f*decal_dot(relative,p.up)/p.height-1.0f,
        2.0f*decal_dot(relative,p.forward)/p.range-1.0f
    };
}

inline decal_volume_aabb sun_shadow_volume_bounds(
    sun_shadow_projection const &p)
{
    decal_volume_aabb bounds{};
    for(int axis=0;axis<3;++axis) {
        float const center=p.origin[axis]+
            p.right[axis]*p.width*0.5f+
            p.up[axis]*p.height*0.5f+
            p.forward[axis]*p.range*0.5f;
        float const extent=std::fabs(p.right[axis])*p.width*0.5f+
            std::fabs(p.up[axis])*p.height*0.5f+
            std::fabs(p.forward[axis])*p.range*0.5f;
        bounds.low[axis]=center-extent;
        bounds.high[axis]=center+extent;
    }
    return bounds;
}

inline bool sun_shadow_world_face_visible(
    uint32_t geometry_mode,decal_vec3 const &normal,
    std::array<float,3> const &direction_toward_sun)
{
    if(geometry_mode<2U || !decal_finite(normal))return false;
    return normal[0]*direction_toward_sun[0]+
           normal[1]*direction_toward_sun[1]+
           normal[2]*direction_toward_sun[2]>0.005f;
}

} // namespace jkgm
