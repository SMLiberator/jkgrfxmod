#include "renderer/native_procedural_draw.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
int main()
{
    using namespace jkgm::native_procedural_draw;
    std::map<uintptr_t,std::vector<uint8_t>> source;
    source[rdpolyline_draw]={polyline_signature.begin(),polyline_signature.end()};
    source[rdpolyline_draw_face]={polyline_face_signature.begin(),polyline_face_signature.end()};
    source[rdparticle_draw]={particle_signature.begin(),particle_signature.end()};
    source[rdthing_draw+0x4EU]={polyline_dispatch_signature.begin(),polyline_dispatch_signature.end()};
    auto reader=[&](uintptr_t address,void *dst,size_t n) {
        auto it=source.find(address);
        if(it==source.end() || it->second.size()!=n)return false;
        std::memcpy(dst,it->second.data(),n);return true;
    };
    assert(verified(reader));
    source[rdpolyline_draw][0]^=1U;
    assert(!verified(reader));
    source[rdpolyline_draw][0]^=1U;
    source[rdthing_draw+0x4EU][7]^=1U;
    assert(!verified(reader));
    assert(particle_material_offset==0x38U);
    assert(polyline_side_face_offset+polyline_face_material_offset==0x58U);
    assert(polyline_tip_face_offset+polyline_face_material_offset==0x98U);
    assert(polyline_side_face_offset+polyline_face_uv_offset_offset==0x60U);
    assert(polyline_tip_face_offset+polyline_face_uv_offset_offset==0xA0U);
}
