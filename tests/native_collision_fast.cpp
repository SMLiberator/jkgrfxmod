#include "renderer/native_collision_fast.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <random>

using namespace jkgm::collision_fast;
template<class T> bool native_plane_hit(float const *s,float const *d,float len,float r,
                                        float const *n,float const *p) {
    T dist=(T(s[1])-p[1])*n[1]+(T(s[2])-p[2])*n[2]+(T(s[0])-p[0])*n[0];
    if(std::abs(dist)<=T(0.00001f))dist=0;
    if(dist<0 || dist-r>len)return false;
    float gap=float(dist-r);
    if(gap<0)return true;
    float approach=float(-(T(d[1])*n[1]+T(d[2])*n[2]+T(d[0])*n[0]));
    return approach>0 && T(approach)*len>=gap;
}
template<class T> bool native_sphere_hit(float const *s,float const *d,float len,float r,
                                         float const *p,float size,bool initial) {
    float const reach=float(T(r)+size);
    float v[3];
    for(int i=0;i<3;++i)v[i]=float(T(p[i])-s[i]);
    auto overlap=[&]{return std::sqrt(T(v[0])*v[0]+T(v[1])*v[1]+T(v[2])*v[2])<reach;};
    if(len==0)return overlap();
    T const along=T(v[0])*d[0]+T(v[1])*d[1]+T(v[2])*d[2];
    if(along<0 || float(along)>T(reach)+len)return initial && overlap();
    float off[3];
    for(int i=0;i<3;++i) {
        float const projected=float(T(s[i])+T(d[i])*float(along));
        off[i]=float(T(p[i])-projected);
    }
    // Native reports a hit even if its subsequent distance computation
    // returns zero for an out-of-range root.
    return std::sqrt(T(off[0])*off[0]+T(off[1])*off[1]+T(off[2])*off[2])<reach;
}
int main() {
    float s[3]={0,0,0},d[3]={1,0,0},n[3]={-1,0,0};
    float far[3]={4,4,0},near[3]={1.8f,0.59f,0};
    query q(s,d,1,0.4f);
    assert(sphere_miss(q,far,0.5f,true));
    assert(!sphere_miss(q,near,0.5f,true)); // native zero-distance hit quirk
    assert(native_sphere_hit<double>(s,d,1,0.4f,near,0.5f,true));
    assert(plane_miss(q,n,far));
    float close[3]={0.1f,0,0};
    assert(!plane_miss(q,n,close)); // initial overlap
    float epsilon[3]={-0.000005f,0,0};
    assert(!plane_miss(q,n,epsilon)); // clip-near-zero
    float invalid[3]={std::numeric_limits<float>::quiet_NaN(),0,0};
    assert(!plane_miss(q,n,invalid));
    assert(!sphere_miss(q,invalid,0.1f,false));
    assert(!sphere_miss(query(s,d,-1,0.1f),far,0.1f,false));
    std::mt19937 rng(91813);
    std::uniform_real_distribution<float> uniform(-1,1),positive(0,1);
    unsigned sphere_reject=0,plane_reject=0;
    for(int i=0;i<250000;++i) {
        float const scale=i%4==0?30000.0f:i%4==1?100.0f:i%4==2?1.0f:0.001f;
        float p[3],axis[3];
        for(int k=0;k<3;++k) {
            s[k]=uniform(rng)*scale;
            d[k]=uniform(rng);
            n[k]=uniform(rng);
            axis[k]=uniform(rng);
        }
        float mag=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        for(float &x:d)x/=mag;
        float const len=i%17==0?0.0f:positive(rng)*scale;
        float const r=positive(rng)*scale*0.1f,size=positive(rng)*scale*0.2f;
        // Include arbitrary points and points around the native projection
        // thresholds/tangencies rather than only far misses.
        float const along=(i%5==0?len+r+size:uniform(rng)*scale);
        for(int k=0;k<3;++k)p[k]=s[k]+d[k]*along+axis[k]*(r+size);
        query test(s,d,len,r);
        bool const initial=(i&1)!=0;
        if(sphere_miss(test,p,size,initial)) {
            ++sphere_reject;
            assert(!native_sphere_hit<float>(s,d,len,r,p,size,initial));
            assert(!native_sphere_hit<double>(s,d,len,r,p,size,initial));
        }
        if(plane_miss(test,n,p)) {
            ++plane_reject;
            assert(!native_plane_hit<float>(s,d,len,r,n,p));
            assert(!native_plane_hit<double>(s,d,len,r,n,p));
        }
    }
    assert(sphere_reject>10000 && plane_reject>10000);
}
