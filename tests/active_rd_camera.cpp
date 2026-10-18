#include "renderer/engine_camera_data.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
static void f(std::array<uint8_t,0x3C> &b, size_t offset,float x) {
    std::memcpy(b.data()+offset,&x,sizeof(x));
}
static bool near(float a,float b) { return std::fabs(a-b)<0.0001f; }
int main() {
    std::array<uint8_t,0x3C> b{};
    f(b,8,1);f(b,24,1);f(b,40,1);
    f(b,44,-5);f(b,48,-6);f(b,52,-7);f(b,0x38,90);
    auto a=jkgm::parse_active_rd_camera(b.data());
    assert(a.valid && near(a.position[0],5)&&near(a.position[1],6));
    assert(near(a.position[2],7)&&near(a.forward[1],1));
    // Rotated view: the camera axes must invert the 3x3 *and* translate.
    f(b,8,0);f(b,12,1);f(b,20,-1);f(b,24,0);
    f(b,44,-2);f(b,48,3);f(b,52,-4);
    a=jkgm::parse_active_rd_camera(b.data());
    assert(a.valid && near(a.right[1],-1) && near(a.forward[0],1));
    assert(near(a.position[0],-3)&&near(a.position[1],-2)&&near(a.position[2],4));
    f(b,0x38,250);
    assert(!jkgm::parse_active_rd_camera(b.data()).valid);
    return 0;
}
