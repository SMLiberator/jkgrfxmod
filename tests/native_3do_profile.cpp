#include "renderer/native_3do_profile.hpp"
#include <cassert>
int main() {
    jkgm::native_3do_sample a{}, b{};
    a.model_calls = 1; a.model_drawn = 1; a.mesh_calls = 4;
    a.face_calls = 34; a.face_emitted = 20; a.face_ns = 700000;
    a.model_ns = 2000000; a.mesh_ns = 1500000;
    b.model_calls = 2; b.model_drawn = 1; b.mesh_calls = 6;
    b.face_calls = 57; b.face_emitted = 28; b.face_ns = 900000;
    b.model_ns = 3000000; b.mesh_ns = 2800000;
    a.add(b);
    assert(a.model_calls == 3 && a.mesh_calls == 10);
    assert(a.model_ns == 5000000 && a.mesh_ns == 4300000);
    assert(a.model_drawn == 2 && a.face_calls == 91);
    assert(a.face_emitted == 48 && a.face_ns == 1600000);
    return 0;
}
