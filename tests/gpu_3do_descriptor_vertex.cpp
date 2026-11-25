#include "renderer/gpu_3do_descriptor_vertex.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace jkgm;
int main() {
    static_assert(sizeof(gpu_3do_descriptor_vertex)==24,"format3 stride");
    std::vector<gpu_3do_vertex> src(6);
    for(size_t i=0;i<src.size();++i) {
        auto &v=src[i];
        v.x=float(i)+0.03125f;v.y=2.0f;v.z=-4.0f;
        v.u=123.125f;v.v=15.5f;
        v.nx=0.0f;v.ny=-1.0f;v.nz=0.0f;
        v.lnx=0.0f;v.lny=0.0f;v.lnz=1.0f;
        v.static_light=0.75f;
        v.fax=0.5f;v.fay=3.5f;v.faz=8.0f;
        v.face_extra=-0.0625f;v.light_mode=3.0f;
        v.cr=128.0f/255.0f;v.cg=1.0f;v.cb=0.0f;
    }
    gpu_3do_face_descriptor_pack out;
    uint32_t reason=0;
    assert(gpu_3do_pack_descriptor_vertices(src,out,&reason));
    assert(out.vertices.size()==6 && out.colors.size()==6 && out.count()==1);
    assert(out.texels.size()==2 && reason==0);
    for(size_t i=0;i<6;++i) {
        auto const &p=out.vertices[i];
        assert(p.x==src[i].x && p.y==src[i].y && p.z==src[i].z);
        assert(p.descriptor_index==0);
        assert(gpu_3do_from_half(p.u)==123.125f);
        assert(gpu_3do_from_half(p.v)==15.5f);
        assert(gpu_3do_from_half(p.static_light)==0.75f);
        assert((out.colors[i]&0x00ffffffU)==0x0000ff80U);
        assert(p.light_normal==gpu_3do_encode_normal10(0.0f,0.0f,1.0f));
    }
    assert(out.texels[0][1]==-1.0f && out.texels[0][3]==-0.0625f);
    assert(out.texels[1][0]==0.5f && out.texels[1][3]==3.0f);
    src[3].face_extra=0.125f;
    assert(gpu_3do_pack_descriptor_vertices(src,out,&reason));
    assert(out.count()==2 && out.vertices[3].descriptor_index==1U);
    assert(out.vertices[0].descriptor_index==0U);
    src[0].u=3001.0f;
    assert(!gpu_3do_pack_descriptor_vertices(src,out,&reason));
    assert((reason&gpu_3do_descriptor_uv)!=0 && out.vertices.empty());
    src[0].u=123.125f;
    src[1].static_light=12.3333f;
    assert(!gpu_3do_pack_descriptor_vertices(src,out,&reason));
    assert((reason&gpu_3do_descriptor_light)!=0);
    src[1].static_light=0.75f;
    src[2].cr=2.0f;
    assert(!gpu_3do_pack_descriptor_vertices(src,out,&reason));
    assert((reason&gpu_3do_descriptor_base)!=0);
    src[2].cr=128.0f/255.0f;
    src[4].face_extra=std::numeric_limits<float>::quiet_NaN();
    assert(!gpu_3do_pack_descriptor_vertices(src,out,&reason));
    assert((reason&gpu_3do_descriptor_invalid)!=0);
}
