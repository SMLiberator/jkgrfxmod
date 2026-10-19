#include "renderer/gpu_3do_colormap.hpp"
#include <cassert>
#include <limits>
int main() {
    using jkgm::gpu_3do_cmp_tint;
    auto a=gpu_3do_cmp_tint(true,{{0.1f,0.2f,0.3f}});
    assert(a[0]==1 && a[1]==1 && a[2]==1);
    auto b=gpu_3do_cmp_tint(false,{{0.25f,0.5f,1.f}});
    assert(b[0]==0.25f && b[1]==0.5f && b[2]==1);
    auto c=gpu_3do_cmp_tint(false,{{std::numeric_limits<float>::quiet_NaN(),0,1}});
    assert(c[0]==1 && c[1]==1 && c[2]==1);
}
