#include "../renderer/fast_vertex_colors.hpp"
#include <cassert>
#include <cmath>

int main()
{
    using namespace jkgm;
    for(int alpha = 0; alpha < 256; alpha += 17) {
        for(int red = 0; red < 256; red += 11) {
            int green = (red * 47 + alpha) & 255;
            int blue = (red * 19 + alpha * 3) & 255;
            auto converted = gamma18_premultiplied_rgba8_lut(
                uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha));
            float const af = float(alpha) / 255.0f;
            auto reference = [&](int v) {
                return std::pow((float(v) / 255.0f) * af, 1.8f);
            };
            assert(std::abs(get<r>(converted) - reference(red)) <= 1e-7f);
            assert(std::abs(get<g>(converted) - reference(green)) <= 1e-7f);
            assert(std::abs(get<b>(converted) - reference(blue)) <= 1e-7f);
            assert(std::abs(get<a>(converted) - af) <= 1e-7f);
        }
    }
    return 0;
}
