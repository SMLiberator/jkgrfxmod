#pragma once
// Shared eligibility for physical daylight apertures. A sky-flagged
// SithSurface is a sunlight opening only if it has no SithSurfaceAdjoin
// pointer. Geomode is deliberately ignored: custom 3DO skyboxes commonly
// sit behind invisible (GeoMode 0) sky-flagged world faces.
#include <cstdint>

namespace jkgm {
inline bool sun_sky_surface_eligible(uint32_t flags,uint32_t adjoin)
{
    return (flags&0x600U)!=0U && adjoin==0U;
}
} // namespace jkgm
