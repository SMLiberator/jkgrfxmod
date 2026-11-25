#include "renderer/gpu_3do_compact_vertex.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

int main()
{
    using namespace jkgm;
    static_assert(sizeof(gpu_3do_vertex)==80,"Legacy vertex changed");
    static_assert(sizeof(gpu_3do_packed_vertex)==52,"Packed vertex changed");
    static_assert(sizeof(gpu_3do_compact_vertex)==40,"Compact vertex changed");
    assert(gpu_3do_to_half(0.0f)==0x0000U);
    assert(gpu_3do_to_half(1.0f)==0x3c00U);
    assert(gpu_3do_to_half(0.5f)==0x3800U);
    assert(gpu_3do_to_half(-2.0f)==0xc000U);
    assert(gpu_3do_to_half(65504.0f)==0x7bffU);
    assert(gpu_3do_to_half(std::ldexp(1.0f,-24))==0x0001U);
    assert(gpu_3do_to_half(std::ldexp(1.0f,-25))==0x0000U);
    // Exact halfway case: round to nearest even (1.0).
    assert(gpu_3do_to_half(1.0f+1.0f/2048.0f)==0x3c00U);
    for(float value : {0.0f,1.0f,-2.0f,0.5f,65504.0f,0.0625f}) {
        assert(gpu_3do_from_half(gpu_3do_to_half(value))==value);
    }

    std::vector<gpu_3do_vertex> src(4);
    for(unsigned i=0;i<src.size();++i) {
        auto &v=src[i];
        v.x=float(i)+0.03125f;v.y=-2.0f;v.z=3.0f;
        v.u=123.125f;v.v=-14.5f;
        v.nx=0.0f;v.ny=0.0f;v.nz=1.0f;
        v.lnx=0.0f;v.lny=1.0f;v.lnz=0.0f;
        v.light_mode=float(i);
        v.static_light=1.75f;v.face_extra=0.0625f;
        v.fax=-7.25f;v.fay=2.5f;v.faz=8.0f;
        v.cr=128.0f/255.0f;v.cg=1.0f;v.cb=0.0f;
    }
    std::vector<gpu_3do_compact_vertex> compact;
    uint32_t reasons=0;
    assert(gpu_3do_pack_compact_vertices(src,compact,&reasons));
    assert(reasons==0 && compact.size()==4U);
    for(unsigned i=0;i<4;++i) {
        auto const &v=compact[i];
        assert(v.x==src[i].x && v.y==src[i].y && v.z==src[i].z);
        assert(gpu_3do_from_half(v.u)==123.125f);
        assert(gpu_3do_from_half(v.v)==-14.5f);
        assert(gpu_3do_from_half(v.static_light)==1.75f);
        assert(gpu_3do_from_half(v.face_extra)==0.0625f);
        assert(gpu_3do_from_half(v.fax)==-7.25f);
        assert(gpu_3do_from_half(v.fay)==2.5f);
        assert(gpu_3do_from_half(v.faz)==8.0f);
        assert((v.normal_mode>>30U)==i);
        assert((v.rgba&255U)==128U && ((v.rgba>>8U)&255U)==255U);
        assert((v.rgba>>24U)==0U);
    }
    // Rejected compact meshes retain their original CPU vertices and
    // remain eligible for the existing 52-byte mesh-level fallback.
    src[0].u=3001.0f;
    assert(!gpu_3do_pack_compact_vertices(src,compact,&reasons));
    assert((reasons&gpu_3do_compact_uv)!=0 && compact.empty());
    src[0].u=123.125f;
    src[1].fax=2000.3f;
    assert(!gpu_3do_pack_compact_vertices(src,compact,&reasons));
    assert((reasons&gpu_3do_compact_anchor)!=0 && compact.empty());
    src[1].fax=-7.25f;
    src[2].face_extra=3000.3f;
    assert(!gpu_3do_pack_compact_vertices(src,compact,&reasons));
    assert((reasons&gpu_3do_compact_light)!=0 && compact.empty());
    src[2].face_extra=0.0625f;
    src[3].cr=1.2f; // 52-byte format rejects HDR RGB: fallback to 80
    assert(!gpu_3do_pack_compact_vertices(src,compact,&reasons));
    assert((reasons&gpu_3do_compact_base)!=0 && compact.empty());
    src[3].cr=128.0f/255.0f;
    src[0].light_mode=4.0f;
    assert(!gpu_3do_pack_compact_vertices(src,compact,&reasons));
    assert((reasons&gpu_3do_compact_base)!=0 && compact.empty());
    src[0].light_mode=0.0f;
    assert(gpu_3do_pack_compact_vertices(src,compact,&reasons));
    assert(reasons==0);
    src.clear();
    assert(!gpu_3do_pack_compact_vertices(src,compact,&reasons));
    assert((reasons&gpu_3do_compact_base)!=0 && compact.empty());
    return 0;
}
