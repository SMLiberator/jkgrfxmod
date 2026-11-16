#include "renderer/gpu_3do_packed_vertex.hpp"
#include <cassert>
#include <cmath>
#include <vector>

int main()
{
    using namespace jkgm;
    std::vector<gpu_3do_vertex> source(4);
    for(unsigned i=0;i<4;++i) {
        auto &v=source[i];
        v.x=float(i);v.y=2.0f;v.z=3.0f;
        v.u=0.3f;v.v=0.7f;
        v.nx=0.0f;v.ny=0.0f;v.nz=1.0f;
        v.lnx=0.0f;v.lny=1.0f;v.lnz=0.0f;
        v.light_mode=float(i);
        v.static_light=0.35f;v.face_extra=0.07f;
        v.fax=-7.0f;v.fay=2.0f;v.faz=8.0f;
        v.cr=128.0f/255.0f;v.cg=1.0f;v.cb=0.0f;
    }
    std::vector<gpu_3do_packed_vertex> packed;
    assert(gpu_3do_pack_vertices(source,packed));
    assert(packed.size()==4);
    for(unsigned i=0;i<4;++i) {
        auto const &v=packed[i];
        assert((v.normal_mode>>30U)==i);
        assert(std::fabs(gpu_3do_decode_normal10_component(
            v.normal_mode,20U)-1.0f)<0.001f);
        assert(std::fabs(gpu_3do_decode_normal10_component(
            v.normal_mode,0U))<0.002f);
        assert(std::fabs(gpu_3do_decode_normal10_component(
            v.light_normal,10U)-1.0f)<0.001f);
        assert((v.rgba&255U)==128U);
        assert(((v.rgba>>8U)&255U)==255U);
        assert((v.rgba>>24U)==0U); // reserved material-blend weight
        assert(v.fax==-7.0f && v.fay==2.0f && v.faz==8.0f);
        assert(v.static_light==0.35f && v.face_extra==0.07f);
    }
    for(int mode=4;mode<=6;++mode) {
        source[0].light_mode=float(mode);
        assert(!gpu_3do_pack_vertices(source,packed));
        assert(packed.empty()); // preserve nonstandard native/MotS modes
    }
    source[0].light_mode=0.0f;
    source[1].light_mode=1.5f;
    assert(!gpu_3do_pack_vertices(source,packed));
    source[1].light_mode=1.0f;
    source[0].nx=0.0f;source[0].ny=0.0f;source[0].nz=0.0f;
    assert(!gpu_3do_pack_vertices(source,packed));
    source[0].nz=1.0f;
    source[0].cr=1.2f;
    assert(!gpu_3do_pack_vertices(source,packed));
    source[0].cr=0.25f;
    assert(!gpu_3do_pack_vertices(source,packed)); // not exact RGB8
    return 0;
}
