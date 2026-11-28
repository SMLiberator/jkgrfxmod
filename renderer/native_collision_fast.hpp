#pragma once
// Fast *miss-only* predicates for the original JKGM x86 intersection routines.
// Do not substitute a textbook swept-segment test: JK's sphere query accepts
// some zero-distance hits beyond the geometric segment and optionally tests
// initial overlap when the projection is outside its accepted interval.
// No output distances, normals or collision state are changed here.
#include <xmmintrin.h>
#include <cmath>

namespace jkgm::collision_fast {
inline __m128 load3(float const *p) noexcept {
    // No 16-byte overread of a native 12-byte vector.
    return _mm_set_ps(0.0f,p[2],p[1],p[0]);
}
inline __m128 abs4(__m128 v) noexcept {
    return _mm_andnot_ps(_mm_set1_ps(-0.0f),v);
}
inline bool bounded(__m128 v,float limit) noexcept {
    // Ordered comparisons also exclude NaNs and infinities.
    return _mm_movemask_ps(_mm_cmple_ps(abs4(v),_mm_set1_ps(limit)))==15;
}
inline float sum3(__m128 v) noexcept {
    auto s=_mm_add_ss(v,_mm_shuffle_ps(v,v,_MM_SHUFFLE(1,1,1,1)));
    return _mm_cvtss_f32(_mm_add_ss(s,_mm_shuffle_ps(v,v,_MM_SHUFFLE(2,2,2,2))));
}
inline float dot3(__m128 a,__m128 b) noexcept {
    return sum3(_mm_mul_ps(a,b));
}
inline float sum_abs3(__m128 a) noexcept {return sum3(abs4(a));}
inline bool valid_scalar(float x) noexcept {return x>=0.0f && x<=65536.0f;}

struct query {
    __m128 start{},dir{};
    float distance=0.0f,radius=0.0f,start_scale=0.0f;
    bool valid=false;
    query(float const *s,float const *d,float length,float r) noexcept
        :distance(length),radius(r) {
        if(!s || !d || !valid_scalar(length) || !valid_scalar(r))return;
        start=load3(s); dir=load3(d);
        if(!bounded(start,65536.0f) || !bounded(dir,2.0f))return;
        start_scale=sum_abs3(start);
        valid=true;
    }
};

// The native x87 plane routine at 0x508e70 clips signed distances within
// 1e-5 of zero, rejects negative signed distance, then tests d-radius
// against distance and approach. Initial penetration can return a hit even
// while moving away; never reject it based solely on orientation.
// Leave a scale-dependent margin for native float spills, x87 precision
// and SSE arithmetic differences. Only misses before the first output
// write may be skipped (the enclosing face routine can write on a miss).
inline bool plane_miss(query const &q,float const *normal,float const *point) noexcept {
    if(!q.valid || !normal || !point)return false;
    auto const n=load3(normal),p=load3(point);
    if(!bounded(n,2.0f) || !bounded(p,65536.0f))return false;
    float const margin=0.00002f+0.000008f*
        (1.0f+q.start_scale+sum_abs3(p)+q.radius+q.distance);
    float const signed_distance=dot3(_mm_sub_ps(q.start,p),n);
    float const gap=signed_distance-q.radius;
    if(signed_distance < -margin || gap > q.distance+margin)return true;
    if(gap<=margin)return false;
    float const approach=-dot3(q.dir,n);
    return approach < -0.000008f || gap > q.distance*approach+margin;
}

// Enclose every possible center accepted by the native sphere test, not
// merely a physically correct swept capsule. Native accepts a projection
// parameter up to distance + combined radius, or an initial overlap. The
// direction need not be unit length. A miss must be outside the resulting
// enlarged swept box. The padding covers x87/SSE rounding differences.
// All query-dependent terms are computed once per uninterrupted list run.
struct sector_envelope {
    __m128 start{},low{},high{},growth{};
    float radius=0.0f,margin=0.0f;
    bool valid=false;
    sector_envelope(float const *s,float const *d,float length,float r) noexcept {
        query const q(s,d,length,r);
        if(!q.valid)return;
        start=q.start;
        auto const zero=_mm_setzero_ps();
        auto const travel=_mm_mul_ps(q.dir,_mm_set1_ps(length));
        low=_mm_min_ps(zero,travel);
        high=_mm_max_ps(zero,travel);
        growth=_mm_add_ps(_mm_set1_ps(1.0f),abs4(q.dir));
        radius=r;
        margin=0.00004f+0.000032f*(1.0f+q.start_scale+length+r);
        valid=true;
    }
    bool misses(float const *center,float size) const noexcept {
        if(!valid || !center || !valid_scalar(size))return false;
        auto const c=load3(center);
        if(!bounded(c,65536.0f))return false;
        auto const delta=_mm_sub_ps(c,start);
        auto const extra=_mm_set1_ps(margin+0.000032f*(sum_abs3(c)+size));
        auto const pad=_mm_add_ps(_mm_mul_ps(growth,_mm_set1_ps(radius+size)),extra);
        auto const outside=_mm_or_ps(
            _mm_cmplt_ps(delta,_mm_sub_ps(low,pad)),
            _mm_cmpgt_ps(delta,_mm_add_ps(high,pad)));
        return (_mm_movemask_ps(outside)&7)!=0;
    }
};

// Match the native projection interval [0, moveDist+combinedRadius],
// not the physical ray segment. Native sphere tests use the projected
// point on the *infinite line*, then may return a zero-distance hit when
// the computed hit distance lies outside the segment.
inline bool sphere_miss(query const &q,float const *center,float size,
                        bool check_initial) noexcept {
    if(!q.valid || !center || !valid_scalar(size))return false;
    auto const c=load3(center);
    if(!bounded(c,65536.0f))return false;
    auto const delta=_mm_sub_ps(c,q.start);
    float const reach=q.radius+size;
    float const along=dot3(delta,q.dir);
    float const margin=0.00002f+0.000012f*
        (1.0f+q.start_scale+sum_abs3(c)+reach+q.distance+std::fabs(along));
    float const expanded=reach+margin;
    float const bound2=expanded*expanded;
    bool const initial_miss=dot3(delta,delta)>bound2;
    if(q.distance==0.0f)return initial_miss;
    float const upper=q.distance+reach;
    if(along < -margin || along > upper+margin)
        return !check_initial || initial_miss;
    // Near the projection thresholds the native routine may instead test
    // initial overlap. In that case reject only if that path misses too.
    if(check_initial && !initial_miss &&
       !(along>margin && along<upper-margin))return false;
    auto const projected=_mm_add_ps(q.start,_mm_mul_ps(q.dir,_mm_set1_ps(along)));
    auto const offset=_mm_sub_ps(c,projected);
    return dot3(offset,offset)>bound2;
}
}
