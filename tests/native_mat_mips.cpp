#include "renderer/native_mat_mips.hpp"
#include <cassert>
#include <cmath>
int main() {
    float dist[4]={10.0f,20.0f,40.0f,80.0f};
    jkgm::prefer_native_mat_mip_zero(dist);
    for(float d:dist) {
        assert(std::isfinite(d));
        assert(d>1.0e9f);
    }
    float const distances[]={1.0f,20.0f,100.0f,100000.0f};
    for(float z:distances) {
        unsigned mip=3;
        if(z*2.0f<=dist[0]) mip=0;
        else if(z*2.0f<=dist[1]) mip=1;
        else if(z*2.0f<=dist[2]) mip=2;
        assert(mip==0);
    }
    return 0;
}
