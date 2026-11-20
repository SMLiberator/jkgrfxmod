#include "renderer/camera_to_mat.hpp"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    jkgm::camera_to_mat_controls c;
    c.prepare_world(100);
    assert(!c.request(1,0,100,90));
    assert(!c.request(1,(uint64_t(0x12345000U)<<32)|77U,100,-1.0f));
    assert(!c.request(1,(uint64_t(0x12345000U)<<32)|77U,100,179.0f));
    assert(!c.request(1,(uint64_t(0x12345000U)<<32)|77U,100,std::numeric_limits<float>::quiet_NaN()));
    assert(!c.request(16384,(uint64_t(0x12345000U)<<32)|77U,0,90));
    assert(c.request(1,(uint64_t(0x12345000U)<<32)|77U,100,90));
    auto s=c.snapshot();
    assert(s.size()==1 && s[0].mat==1 && s[0].fov==90);
    assert(c.request(1,(uint64_t(0x12345000U)<<32)|88U,100,0));
    s=c.snapshot();
    assert(s[0].fov==0 && uint32_t(s[0].owner)==88U);
    for(unsigned i=2;i<=4;++i)
        assert(c.request(i,(uint64_t(0x12345000U)<<32)|77U,100,70));
    assert(!c.request(5,(uint64_t(0x12345000U)<<32)|77U,100,70));
    c.erase(2);
    assert(c.snapshot().size()==3);
    assert(c.request(5,(uint64_t(0x12345000U)<<32)|77U,100,70));
    c.prepare_world(101);
    assert(c.snapshot().empty());
    assert(c.request(32768,(uint64_t(0x12345000U)<<32)|77U,101,90));
    assert(c.snapshot()[0].mat==32768);
    c.clear();
    assert(c.snapshot().empty());
    return 0;
}
