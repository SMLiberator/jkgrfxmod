#include "renderer/gpu_3do_uv.hpp"
#include <cassert>
#include <cmath>

static float repeats(float span,float native_extent) {
    return span/jkgm::gpu_3do_model_uv_extent(native_extent);
}
int main() {
    // A model authored in a 256x256 UV reference square.
    assert(repeats(256.0f,64.0f)==4.0f);
    assert(repeats(256.0f,128.0f)==2.0f);
    assert(repeats(256.0f,256.0f)==1.0f);
    assert(repeats(256.0f,512.0f)==1.0f);
    assert(repeats(256.0f,1024.0f)==1.0f);
    assert(repeats(256.0f,2048.0f)==1.0f);
    // Rectangular 512x256 and 1024x2048 MATs normalize independently.
    assert(repeats(256.0f,512.0f)==1.0f);
    assert(repeats(256.0f,256.0f)==1.0f);
    assert(repeats(256.0f,1024.0f)==1.0f);
    assert(repeats(256.0f,2048.0f)==1.0f);
    // A rectangular small texture still tiles along its shorter axis.
    assert(repeats(256.0f,64.0f)==4.0f);
    assert(repeats(256.0f,128.0f)==2.0f);
    assert(repeats(256.0f,0.0f)==256.0f);
    assert(repeats(256.0f,-1.0f)==256.0f);
    assert(repeats(256.0f,NAN)==256.0f);
    return 0;
}
