#include "renderer/gpu_3do_packed_vertex.hpp"
#include <cassert>
#include <cmath>
#include <limits>
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
    // Zero-length normals are legal in native geometry, and must remain
    // exactly zero after decode rather than becoming arbitrary directions.
    source[0].nx=0.0f;source[0].ny=0.0f;source[0].nz=0.0f;
    source[0].lnx=0.0f;source[0].lny=0.0f;source[0].lnz=0.0f;
    assert(gpu_3do_pack_vertices(source,packed));
    for(unsigned shift:{0U,10U,20U}) {
        assert(gpu_3do_decode_normal10_component(
            packed[0].normal_mode,shift)==0.0f);
        assert(gpu_3do_decode_normal10_component(
            packed[0].light_normal,shift)==0.0f);
    }
    // Finite non-unit normals remain eligible and retain their magnitude.
    source[0].nz=0.5f;
    source[0].lnx=-0.25f;
    assert(gpu_3do_pack_vertices(source,packed));
    assert(std::fabs(gpu_3do_decode_normal10_component(
        packed[0].normal_mode,20U)-0.5f)<0.002f);
    assert(std::fabs(gpu_3do_decode_normal10_component(
        packed[0].light_normal,0U)+0.25f)<0.002f);
    source[0].nx=1.1f;
    assert(!gpu_3do_pack_vertices(source,packed));
    source[0].nx=0.0f;
    // Valid fractional colors quantize to RGB8 rather than forcing float80.
    source[0].cr=0.25f;source[0].cg=0.5f;source[0].cb=0.0f;
    assert(gpu_3do_pack_vertices(source,packed));
    assert((packed[0].rgba&255U)==64U);
    assert(((packed[0].rgba>>8U)&255U)==128U);
    source[0].cr=1.2f;
    assert(!gpu_3do_pack_vertices(source,packed)); // HDR tint
    source[0].cr=-0.1f;
    assert(!gpu_3do_pack_vertices(source,packed)); // negative tint

    // Profiling scans the whole rejected mesh, counting every DISTINCT
    // rejection category exactly once per upload, not once per vertex.
    source[0].cr=1.2f;              // out-of-range RGB
    source[0].nx=1.1f;              // out-of-range face normal
    source[1].light_mode=4.0f;      // MotS mode
    source[2].lnx=std::numeric_limits<float>::infinity();
    uint32_t reasons=0;
    assert(!gpu_3do_pack_vertices(source,packed,&reasons));
    assert(packed.empty());
    auto has=[&](gpu_3do_pack_reject_reason reason) {
        return (reasons&gpu_3do_pack_reason_bit(reason))!=0;
    };
    assert(has(gpu_3do_pack_rgb_out_of_range));
    assert(has(gpu_3do_pack_face_normal_out_of_range));
    assert(has(gpu_3do_pack_mode_out_of_range));
    assert(has(gpu_3do_pack_light_normal_nonfinite));
    gpu_3do_pack_profile profile;
    profile.record(false,source.size(),reasons);
    assert(profile.rejected_uploads==1 && profile.accepted_uploads==0);
    assert(profile.reasons[gpu_3do_pack_rgb_out_of_range]==1);
    assert(profile.reasons[gpu_3do_pack_mode_out_of_range]==1);
    assert(profile.reasons[gpu_3do_pack_face_normal_out_of_range]==1);
    assert(profile.reasons[gpu_3do_pack_light_normal_nonfinite]==1);
    source[0].cr=0.25f;source[0].nx=0.0f;
    source[1].light_mode=1.0f;
    source[2].lnx=0.0f;
    assert(gpu_3do_pack_vertices(source,packed,&reasons));
    assert(reasons==0);
    profile.record(true,source.size(),reasons);
    assert(profile.accepted_uploads==1 && profile.packed_vertices==4);
    assert(profile.rejected_uploads==1 && profile.legacy_vertices==4);
    std::vector<gpu_3do_vertex> empty;
    assert(!gpu_3do_pack_vertices(empty,packed,&reasons));
    assert(has(gpu_3do_pack_empty));
    return 0;
}
