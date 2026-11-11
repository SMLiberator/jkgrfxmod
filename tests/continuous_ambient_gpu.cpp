// Pure CPU tests for a compact GL 3.3 GPU transport of sparse voxels.
// No OpenGL headers or context required.
#include "renderer/continuous_ambient_gpu.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>
using namespace jkgm;
using field=continuous_ambient_field;
using table=continuous_ambient_gpu_table;

int main()
{
    field source;
    source.reset(1.0f,true);
    for(int x=-2;x<=2;++x)
    for(int y=-2;y<=2;++y)
    for(int z=-2;z<=2;++z) {
        assert(source.add(0U,{{float(x),float(y),float(z)}},0.4f));
        assert(source.add(1U,{{float(x),float(y),float(z)}},1.4f));
    }
    field::point a{{0.2f,-0.8f,-0.8f}};
    field::point b{{0.2f,0.8f,-0.8f}};
    field::point c{{0.2f,0.0f,0.8f}};
    assert(source.add_portal(0U,1U,{{a,b,c}}));
    assert(source.finalize());
    table packed;
    assert(packed.build(source) && packed.ready());
    assert(packed.node_count()==source.node_count());
    assert(packed.capacity()>packed.node_count());
    assert(packed.capacity() && ((packed.capacity()&(packed.capacity()-1U))==0U));
    assert(packed.max_probes()>0U &&
           packed.max_probes()<=table::max_probe_budget);
    assert(packed.key_bytes()==size_t(packed.capacity())*16U);
    assert(packed.value_bytes()==size_t(packed.capacity())*8U);
    assert(packed.spacing()==1.0f);
    assert(packed.find(0U,{{0,0,0}})>=0);
    assert(packed.find(1U,{{0,0,0}})>=0);
    assert(packed.find(123456U,{{0,0,0}})==-1);
    assert(packed.find(0U,{{999,0,0}})==-1);
    assert(packed.find(0xffffffffU,{{0,0,0}})==-1);
    // Signed grid coordinates are encoded by their uint32 bit patterns;
    // no float hashing or negative-coord aliasing is allowed.
    int const negative=packed.find(0U,{{-2,-2,-2}});
    assert(negative>=0);
    assert(packed.keys()[size_t(negative)][1]==uint32_t(-2));
    // Check EVERY exported key against its own probe lookup. Collision
    // probing must be bounded and never return an unrelated sector.
    for(size_t i=0;i<packed.keys().size();++i) {
        auto const &k=packed.keys()[i];
        if(k[0]==table::empty_sector)continue;
        assert(packed.find(k[0],{{int32_t(k[1]),int32_t(k[2]),
                                 int32_t(k[3])}})==int(i));
    }
    auto const old_keys=packed.keys();
    assert(packed.build(source));
    assert(packed.keys()==old_keys); // canonical insertion order

    // Floor averaging across stitched nodes matches the runtime CPU
    // field: both participating owners see the SAME light at a portal.
    float const f0=0.7f,f1=1.75f;
    auto floors=[&](uint32_t id,float &floor) {
        if(id>1U)return false;
        floor=id==0U?f0:f1;
        return true;
    };
    assert(packed.update_shared_floors(floors,source.range()));
    size_t shared=0,unshared=0;
    for(size_t i=0;i<packed.keys().size();++i) {
        auto const &k=packed.keys()[i];
        if(k[0]==table::empty_sector)continue;
        auto const &v=packed.values()[i];
        assert(v[0]>=0.0f && v[0]<=2.0f);
        if(v[1]>=0.0f) {
            ++shared;
            // Both sectors contribute max(authored, floor).
            float expect=(std::max(v[0],f0)+std::max(v[0],f1))*0.5f;
            assert(std::fabs(v[1]-expect)<0.00001f);
        } else ++unshared;
    }
    assert(shared>0U && unshared>0U);
    // Invalid floor reads cannot leave half-updated GPU values.
    auto const old_values=packed.values();
    auto fail=[](uint32_t,float &){return false;};
    assert(!packed.update_shared_floors(fail,source.range()));
    assert(packed.values()==old_values);
    auto nan_floor=[](uint32_t,float &v){v=NAN;return true;};
    assert(!packed.update_shared_floors(nan_floor,source.range()));
    assert(packed.values()==old_values);
    assert(!packed.update_shared_floors(floors,0.0f));

    field::point lo{},hi{};
    std::array<uint8_t,8> samples{};
    assert(source.get_fallback(0U,lo,hi,samples));
    assert(!source.get_fallback(99U,lo,hi,samples));

    // A deliberately sparse 3000-source layout exercises the budget
    // limit under realistic (~80k-node) load, without dense volume
    // allocations or pathological insertion-order dependence.
    source.reset();
    for(int i=0;i<3000;++i) {
        field::point p{{float(i*5),float((i%7)*5),
                        float((i%13)*5)}};
        assert(source.add(uint32_t(i%7),p,0.3f));
    }
    assert(source.finalize());
    assert(source.node_count()>40000U);
    assert(packed.build(source));
    assert(packed.node_count()==source.node_count());
    assert(packed.capacity()>=packed.node_count()*2U);
    assert(packed.max_probes()<=table::max_probe_budget);
    for(auto const &k:packed.keys()) {
        if(k[0]==table::empty_sector)continue;
        assert(packed.find(k[0],{{int32_t(k[1]),int32_t(k[2]),
                                 int32_t(k[3])}})>=0);
    }
    packed.reset();
    assert(!packed.ready());
    assert(packed.find(0U,{{0,0,0}})==-1);
    return 0;
}
