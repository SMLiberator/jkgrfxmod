#include "renderer/planar_receiver_visibility.hpp"
#include <cassert>
#include <cmath>
int main(){
    using namespace jkgm;
    engine_camera a{};
    a.valid=true;
    assert(planar_receiver_camera_near(a,a));
    auto b=a;
    b.position[0]+=0.1f;
    assert(planar_receiver_camera_near(a,b));
    b.position[0]+=0.3f;
    assert(!planar_receiver_camera_near(a,b));
    b=a;
    b.forward[0]=0.05f;
    assert(!planar_receiver_camera_near(a,b));
    b=a;b.fov=80.0f;
    assert(!planar_receiver_camera_near(a,b));
    b=a;b.valid=false;
    assert(!planar_receiver_camera_near(a,b));
}
