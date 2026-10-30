#pragma once
// Experimental JKGM collide=3 precision fix: zero-radius ray against
// model polygons. Pure math, independent of rendering and the Sith ABI.
// Inverse affine transforms keep ray parameters in world-distance units,
// including mirrored, scaled, and non-orthogonal model axes.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace jkgm {
namespace precise_type3 {
using vec3=std::array<float,3>;
using matrix34=std::array<float,12>;
struct ray {
    std::array<double,3> start{},direction{};
};
struct hit {
    double distance=std::numeric_limits<double>::infinity();
    vec3 world_normal{};
    bool valid=false;
};

inline bool finite(vec3 const& p) {
    return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);
}
inline std::array<double,3> cross(std::array<double,3> const& a,
                                  std::array<double,3> const& b) {
    return {{a[1]*b[2]-a[2]*b[1],
             a[2]*b[0]-a[0]*b[2],
             a[0]*b[1]-a[1]*b[0]}};
}
inline double dot(std::array<double,3> const& a,
                  std::array<double,3> const& b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
inline std::array<double,3> minus(std::array<double,3> a,
                                  std::array<double,3> const& b) {
    for(int i=0;i<3;++i)a[i]-=b[i];
    return a;
}
inline std::array<double,3> to_double(vec3 const& v) {
    return {{v[0],v[1],v[2]}};
}
struct transform {
    // inverse[local-axis][world-component], determinant may be negative
    std::array<std::array<double,3>,3> inverse{};
    std::array<double,3> translation{};
    bool valid=false;
};
inline transform invert(matrix34 const& m) {
    transform t{};
    for(float x:m)if(!std::isfinite(x))return t;
    std::array<double,3> const a{{m[0],m[1],m[2]}};
    std::array<double,3> const b{{m[3],m[4],m[5]}};
    std::array<double,3> const c{{m[6],m[7],m[8]}};
    auto const bc=cross(b,c),ca=cross(c,a),ab=cross(a,b);
    double const det=dot(a,bc);
    double const lengths=std::sqrt(dot(a,a)*dot(b,b)*dot(c,c));
    if(!(lengths>1.e-30) || !std::isfinite(lengths) ||
       !std::isfinite(det) || std::fabs(det)<=lengths*1.e-12)
        return t;
    for(int i=0;i<3;++i) {
        t.inverse[0][i]=bc[i]/det;
        t.inverse[1][i]=ca[i]/det;
        t.inverse[2][i]=ab[i]/det;
    }
    t.translation={{m[9],m[10],m[11]}};
    t.valid=true;
    return t;
}
inline ray to_local(transform const& t,vec3 const& origin,
                     vec3 const& direction) {
    ray result{};
    auto const p=minus(to_double(origin),t.translation);
    auto const d=to_double(direction);
    for(int i=0;i<3;++i) {
        result.start[i]=dot(t.inverse[i],p);
        result.direction[i]=dot(t.inverse[i],d);
    }
    return result;
}
inline bool world_normal(transform const& t,vec3 const& local,
                          vec3 &result) {
    auto const n=to_double(local);
    std::array<double,3> v{};
    for(int i=0;i<3;++i)
        for(int j=0;j<3;++j)
            v[i]+=t.inverse[j][i]*n[j]; // inverse-transpose
    double const len=std::sqrt(dot(v,v));
    if(!(len>1.e-25) || !std::isfinite(len))return false;
    for(int i=0;i<3;++i)result[i]=float(v[i]/len);
    return finite(result);
}
inline bool point_on_segment(double x,double y,double ax,double ay,
                              double bx,double by,double tolerance) {
    double const dx=bx-ax,dy=by-ay;
    double const len=std::hypot(dx,dy);
    if(len==0.0)return std::hypot(x-ax,y-ay)<=tolerance;
    double const side=((x-ax)*dy-(y-ay)*dx)/len;
    double const along=((x-ax)*dx+(y-ay)*dy)/(len*len);
    return std::fabs(side)<=tolerance &&
           along>=-tolerance/len && along<=1.0+tolerance/len;
}
// Conservative local-space AABB, including rays parallel to an axis.
// Deliberately preserve a small positive margin at face/mesh boundaries.
// A failed/ambiguous bounds check must never suppress a real hit.
inline bool ray_intersects_bounds(ray const& r,vec3 const& lower,
                                  vec3 const& upper,double max_distance)
{
    if(!finite(lower)||!finite(upper)||!(max_distance>=0.0)||
       !std::isfinite(max_distance))return true;
    double enter=0.0,leave=max_distance;
    for(int axis=0;axis<3;++axis) {
        double const extent=double(upper[axis])-lower[axis];
        if(extent<0.0)return true;
        double const margin=std::max(1.e-5,extent*1.e-5);
        double const lo=double(lower[axis])-margin;
        double const hi=double(upper[axis])+margin;
        double const position=r.start[axis],direction=r.direction[axis];
        if(!std::isfinite(position)||!std::isfinite(direction))return true;
        if(std::fabs(direction)<1.e-16) {
            if(position<lo || position>hi)return false;
            continue;
        }
        double a=(lo-position)/direction;
        double b=(hi-position)/direction;
        if(a>b)std::swap(a,b);
        enter=std::max(enter,a);
        leave=std::min(leave,b);
        if(leave<enter)return false;
    }
    return true;
}

// Even-odd polygon test works with nonconvex JK faces. Count true edge
// contact as inside to avoid rejecting a collision due to float rounding.
template<class Vertex>
bool inside_polygon(std::array<double,3> const& p,
                    vec3 const& normal,std::size_t count,Vertex vertex) {
    if(count<3)return false;
    int ignored_axis=0;
    for(int a=1;a<3;++a)
        if(std::fabs(normal[a])>std::fabs(normal[ignored_axis]))
            ignored_axis=a;
    int const ax=(ignored_axis+1)%3,ay=(ignored_axis+2)%3;
    double minx=std::numeric_limits<double>::infinity();
    double miny=minx,maxx=-minx,maxy=-minx;
    for(std::size_t i=0;i<count;++i) {
        auto const v=vertex(i);
        if(!finite(v))return false;
        minx=std::min(minx,double(v[ax]));
        miny=std::min(miny,double(v[ay]));
        maxx=std::max(maxx,double(v[ax]));
        maxy=std::max(maxy,double(v[ay]));
    }
    double const extent=std::max(maxx-minx,maxy-miny);
    double const tolerance=std::max(1.e-8,std::min(1.e-5,extent*1.e-6));
    bool inside=false;
    for(std::size_t i=0,j=count-1;i<count;j=i++) {
        auto const a=vertex(j),b=vertex(i);
        double const x=p[ax],y=p[ay];
        if(point_on_segment(x,y,a[ax],a[ay],b[ax],b[ay],tolerance))
            return true;
        double const ya=a[ay],yb=b[ay];
        if((ya>y)!=(yb>y)) {
            double const at_x=double(a[ax])+
                (y-ya)*(double(b[ax])-a[ax])/(yb-ya);
            if(x<at_x)inside=!inside;
        }
    }
    return inside;
}
// Input direction must be unit length in world space. The local ray is
// deliberately NOT renormalized after the inverse transform.
// Only front-facing intersections are returned (native JK convention).
template<class Vertex>
bool hit_face(ray const& local,vec3 const& normal,
              std::size_t count,Vertex vertex,double max_distance,
              double &distance) {
    if(count<3 || !finite(normal) || !std::isfinite(max_distance) ||
       max_distance<=0.0)return false;
    auto const n=to_double(normal);
    double const nlen=std::sqrt(dot(n,n));
    double const nd=dot(n,local.direction);
    if(!(nlen>1.e-20) || !(nd< -1.e-12*nlen))return false;
    auto const v0=vertex(0);
    if(!finite(v0))return false;
    double const t=dot(n,minus(to_double(v0),local.start))/nd;
    if(!std::isfinite(t) || t<0.0 || t>max_distance)return false;
    std::array<double,3> const p{{
        local.start[0]+t*local.direction[0],
        local.start[1]+t*local.direction[1],
        local.start[2]+t*local.direction[2]}};
    if(!inside_polygon(p,normal,count,vertex))return false;
    distance=t;
    return true;
}
} // namespace precise_type3
} // namespace jkgm
