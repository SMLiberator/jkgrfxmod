#include "renderer/gpu_3do_auto_lod.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
using namespace jkgm;
static gpu_3do_geometry grid(unsigned n) {
    gpu_3do_geometry mesh{};
    gpu_3do_group group{};
    group.material=0x1234U;
    group.cel=2;
    group.plane_start=3;
    group.plane_count=7;
    for(unsigned y=0;y<n;++y)
        for(unsigned x=0;x<n;++x) {
            float p[4][2]={{float(x),float(y)},{float(x+1),float(y)},
                            {float(x+1),float(y+1)},{float(x),float(y+1)}};
            for(auto const &t : {std::array<int,3>{{0,1,2}},
                                  std::array<int,3>{{0,2,3}}}) {
                for(int k:t) {
                    gpu_3do_vertex v{};
                    v.x=p[k][0];v.y=p[k][1];v.z=0.0f;
                    v.u=p[k][0];v.v=p[k][1];
                    v.nz=1.0f;v.lnz=1.0f;
                    v.fax=float(x);v.fay=float(y);v.faz=0.0f;
                    v.cr=v.cg=v.cb=1.0f;
                    mesh.indices.push_back(
                        static_cast<uint32_t>(mesh.vertices.size()));
                    mesh.vertices.push_back(v);
                }
            }
        }
    group.count=static_cast<uint32_t>(mesh.indices.size());
    mesh.groups.push_back(group);
    return mesh;
}
int main() {
    assert(select_gpu_3do_auto_lod(150.0f)==0U);
    assert(select_gpu_3do_auto_lod(60.0f)==1U);
    assert(select_gpu_3do_auto_lod(4.0f)==2U);
    assert(select_gpu_3do_auto_lod(99.0f,1U)==1U);
    assert(select_gpu_3do_auto_lod(35.0f,2U)==2U);
    assert(select_gpu_3do_auto_lod(120.0f,2U)==0U);
    // Altering the screen-size thresholds moves both transition bands
    // without changing native model GeoSets or the projected-radius units.
    assert(select_gpu_3do_auto_lod(60.0f,0U,50.0f,12.0f)==0U);
    assert(select_gpu_3do_auto_lod(30.0f,0U,50.0f,12.0f)==1U);
    assert(select_gpu_3do_auto_lod(10.0f,0U,50.0f,12.0f)==2U);
    assert(select_gpu_3do_auto_lod(65.0f,1U,50.0f,12.0f)==0U);
    assert(select_gpu_3do_auto_lod(14.0f,2U,50.0f,12.0f)==2U);
    assert(select_gpu_3do_auto_lod(20.0f,2U,50.0f,12.0f)==1U);
    // Forced debug makes reduced geometry visible directly at point-blank
    // range; a separate mode explicitly selects the untouched GeoSet 0.
    assert(select_gpu_3do_auto_lod(10000.0f,0U,90.0f,28.0f,1)==0U);
    assert(select_gpu_3do_auto_lod(10000.0f,0U,90.0f,28.0f,2)==1U);
    assert(select_gpu_3do_auto_lod(10000.0f,0U,90.0f,28.0f,3)==2U);
    assert(select_gpu_3do_auto_lod(0.0f,0U,90.0f,28.0f,3)==2U);
    assert(select_gpu_3do_auto_lod(NAN,0U,90.0f,28.0f,2)==1U);
    auto source=grid(18U);
    assert(!generate_gpu_3do_auto_lod(source,0U));
    assert(!generate_gpu_3do_auto_lod(source,3U));
    auto one=generate_gpu_3do_auto_lod(source,1U);
    auto two=generate_gpu_3do_auto_lod(source,2U);
    assert(one && two);
    assert(one->indices.size()<source.indices.size());
    assert(two->indices.size()<=one->indices.size());
    assert(source.indices.size()==18U*18U*6U); // original immutable
    for(auto const &result:{one,two}) {
        assert(result->groups.size()==1U);
        assert(result->groups[0].material==0x1234U);
        assert(result->groups[0].cel==2);
        assert(result->groups[0].plane_start==3U);
        assert(result->groups[0].plane_count==7U);
        assert(result->groups[0].count==result->indices.size());
        assert(result->indices.size()%3U==0U);
        for(uint32_t i:result->indices) {
            assert(i<result->vertices.size());
            auto const &v=result->vertices[i];
            assert(std::isfinite(v.x)&&std::isfinite(v.y));
            assert(v.x>=0.0f&&v.x<=18.0f);
            assert(v.y>=0.0f&&v.y<=18.0f);
        }
    }
    // Low-poly models stay untouched, avoiding pointless memory and
    // silhouette quality loss.
    auto small=grid(3U);
    assert(!generate_gpu_3do_auto_lod(small,1U));
    // Opaque-only simplification: alpha faces cannot lose painter order.
    auto alpha=grid(18U);
    alpha.groups[0].face_type=2U;
    assert(!generate_gpu_3do_auto_lod(alpha,2U));
}
