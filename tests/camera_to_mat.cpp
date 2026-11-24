#include "renderer/camera_to_mat.hpp"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
    jkgm::camera_to_mat_controls c;
    c.prepare_world(100);
    assert(!c.has_requests());
    assert(!c.request(1,0,100,90));
    assert(!c.request(1,(uint64_t(0x12345000U)<<32)|77U,100,
                      -std::numeric_limits<float>::infinity()));
    // Zero FOV is a no-op: no new slot or generation, even before a photo.
    uint64_t const initial_revision=c.revision();
    assert(c.request(1,(uint64_t(0x12345000U)<<32)|77U,100,0.0f));
    assert(!c.has_requests() && c.revision()==initial_revision);
    assert(!c.request(1,(uint64_t(0x12345000U)<<32)|77U,100,179.0f));
    assert(!c.request(1,(uint64_t(0x12345000U)<<32)|77U,100,std::numeric_limits<float>::quiet_NaN()));
    assert(!c.request(16384,(uint64_t(0x12345000U)<<32)|77U,0,90));
    assert(c.request(1,(uint64_t(0x12345000U)<<32)|77U,100,90));
    assert(c.has_requests());
    assert(!jkgm::camera_to_mat_controls::needs_capture(true,17,17));
    assert(jkgm::camera_to_mat_controls::needs_capture(true,17,18));
    assert(jkgm::camera_to_mat_controls::needs_capture(false,17,17));
    auto s=c.snapshot();
    assert(s.size()==1 && s[0].mat==1 && s[0].fov==90);
    uint64_t const old_generation=s[0].generation;
    // Zero FOV must not replace a completed or pending photograph.
    assert(c.request(1,(uint64_t(0x12345000U)<<32)|88U,100,0.0f));
    s=c.snapshot();
    assert(s[0].fov==90 && uint32_t(s[0].owner)==77U);
    assert(s[0].generation==old_generation);
    assert(c.request(1,(uint64_t(0x12345000U)<<32)|88U,100,-0.5f));
    s=c.snapshot();
    assert(s[0].fov==-0.5f && uint32_t(s[0].owner)==88U);
    // Explicit repeat requests are distinct one-shot generations, not live.
    assert(s[0].generation>old_generation);
    assert(jkgm::camera_to_mat_controls::needs_capture(
        true,old_generation,s[0].generation));
    assert(!jkgm::camera_to_mat_controls::needs_capture(
        true,s[0].generation,s[0].generation));
    for(unsigned i=2;i<=4;++i)
        assert(c.request(i,(uint64_t(0x12345000U)<<32)|77U,100,70));
    assert(!c.request(5,(uint64_t(0x12345000U)<<32)|77U,100,70));
    // Zero doesn't consume additional slots even at capacity.
    assert(c.request(5,(uint64_t(0x12345000U)<<32)|77U,100,0.0f));
    assert(c.snapshot().size()==4);
    c.erase(2);
    assert(c.snapshot().size()==3);
    assert(c.request(5,(uint64_t(0x12345000U)<<32)|77U,100,70));
    c.prepare_world(101);
    assert(c.snapshot().empty());
    assert(c.request(32768,(uint64_t(0x12345000U)<<32)|77U,101,-1.0f));
    assert(c.snapshot()[0].mat==32768);
    c.clear();
    assert(c.snapshot().empty());

    // New mode-based API is independent of the signed legacy FOV.
    using namespace jkgm;
    assert(camera_to_mat_valid_mode(0x0U));
    assert(camera_to_mat_valid_mode(0x1U));
    assert(camera_to_mat_valid_mode(0x2U));
    assert(camera_to_mat_valid_mode(0x3U));
    assert(camera_to_mat_valid_mode(0x4U));
    assert(camera_to_mat_valid_mode(0x5U));
    assert(!camera_to_mat_valid_mode(0x6U)); // simplified + unlit
    assert(!camera_to_mat_valid_mode(0x7U));
    assert(!camera_to_mat_valid_mode(0x8U)); // unknown flags

    constexpr uint64_t source=(uint64_t(0x12345000U)<<32)|77U;
    assert(c.request_ex(7,source,102,20.0f,0x5U));
    s=c.snapshot();
    assert(s.size()==1 && s[0].mat==7 &&
           s[0].fov==-20.0f && s[0].mode==0x5U);
    uint64_t const ex_generation=s[0].generation;
    assert(c.request_ex(7,source,102,0.0f,0x5U));
    assert(c.snapshot()[0].generation==ex_generation);
    assert(!c.request_ex(8,source,102,45.0f,0x6U));
    assert(!c.request_ex(8,source,102,45.0f,0x8U));
    assert(!c.request_ex(8,source,102,-1.0f,0x1U));
    assert(!c.request_ex(8,source,102,179.0f,0x0U));
    assert(!c.request_ex(8,source,102,
        std::numeric_limits<float>::infinity(),0x1U));
    assert(c.snapshot().size()==1);
    // Height 179 JKU is valid orthographic area, not a 179-degree FOV.
    assert(c.request_ex(7,source,102,179.0f,0x1U));
    assert(c.snapshot()[0].fov==-179.0f &&
           c.snapshot()[0].mode==0x1U);
    assert(c.request_ex(7,source,102,75.0f,0x0U));
    assert(c.snapshot()[0].fov==75.0f &&
           c.snapshot()[0].mode==0x0U);
    assert(c.request_ex(7,source,102,75.0f,0x4U));
    assert(c.snapshot()[0].mode==camera_to_mat_unlit);
    assert(c.request(7,source,102,-0.5f));
    assert(c.snapshot()[0].fov==-0.5f &&
           c.snapshot()[0].mode==camera_to_mat_simplified);
    return 0;
}
