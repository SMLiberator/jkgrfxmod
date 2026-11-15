#pragma once
// Generate a seamless 8-bit Voronoi ridge texture ONCE per GL context.
// The GPU's mipmap chain then supplies soft caustics without per-frame blur.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
namespace jkgm {
constexpr int sun_caustic_pattern_size=256;
constexpr int sun_caustic_cell_count=12;
inline std::uint32_t sun_caustic_hash(int x,int y)
{
    std::uint32_t h=std::uint32_t(x)*0x9e3779b9U+
                    std::uint32_t(y)*0x85ebca6bU+0x68bc21ebU;
    h^=h>>16;h*=0x7feb352dU;h^=h>>15;h*=0x846ca68bU;h^=h>>16;
    return h;
}
inline std::array<std::uint8_t,
    sun_caustic_pattern_size*sun_caustic_pattern_size>
generate_sun_caustic_pattern()
{
    std::array<std::uint8_t,
        sun_caustic_pattern_size*sun_caustic_pattern_size> pixels{};
    constexpr int cells=sun_caustic_cell_count;
    for(int y=0;y<sun_caustic_pattern_size;++y)
    for(int x=0;x<sun_caustic_pattern_size;++x) {
        float px=(float(x)+0.5f)*float(cells)/sun_caustic_pattern_size;
        float py=(float(y)+0.5f)*float(cells)/sun_caustic_pattern_size;
        int const cx=int(std::floor(px)),cy=int(std::floor(py));
        float closest=1000.0f,second=1000.0f;
        for(int iy=-1;iy<=1;++iy)
        for(int ix=-1;ix<=1;++ix) {
            int const fx=cx+ix,fy=cy+iy;
            int const wrapped_x=(fx+cells)%cells;
            int const wrapped_y=(fy+cells)%cells;
            auto const h=sun_caustic_hash(wrapped_x,wrapped_y);
            float const jitter_x=float(h&0xffffU)/65535.0f;
            float const jitter_y=float(h>>16)/65535.0f;
            float const dx=px-(float(fx)+0.15f+jitter_x*0.7f);
            float const dy=py-(float(fy)+0.15f+jitter_y*0.7f);
            float const d=dx*dx+dy*dy;
            if(d<closest) {second=closest;closest=d;}
            else if(d<second)second=d;
        }
        float const gap=std::sqrt(second)-std::sqrt(closest);
        float const bright=std::exp(-gap*11.0f);
        pixels[std::size_t(y)*sun_caustic_pattern_size+std::size_t(x)]=
            std::uint8_t(std::min(255.0f,std::max(0.0f,bright*255.0f)));
    }
    return pixels;
}
} // namespace jkgm
