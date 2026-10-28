#pragma once
// Small, pure numeric helpers exposed as JKGM COG verbs.
// COG handlers pop arguments in reverse order; keep arithmetic independent
// of the Sith interpreter so it can be regression-tested directly.
#include <algorithm>
#include <cmath>

namespace jkgm {

inline float cog_math_min(float a,float b)
{
    return std::fmin(a,b);
}

inline float cog_math_max(float a,float b)
{
    return std::fmax(a,b);
}

// Reversed bounds are accepted and treated as the same interval.
inline float cog_math_clamp(float value,float lower,float upper)
{
    if(lower>upper)std::swap(lower,upper);
    return std::fmin(std::fmax(value,lower),upper);
}

inline int cog_math_sign(float value)
{
    return int(value>0.0f)-int(value<0.0f);
}

// Unclamped: t<0 or t>1 extrapolates beyond the endpoints.
inline float cog_math_lerp(float a,float b,float t)
{
    return a+(b-a)*t;
}

} // namespace jkgm
