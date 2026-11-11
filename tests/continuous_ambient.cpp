#include "renderer/continuous_ambient.hpp"
#ifdef JKGM_NATIVE_CAPTURE_TEST
#include "renderer/continuous_ambient_capture.hpp"
#include <cstring>
#include <unordered_map>
#endif
#include <cassert>
#include <cmath>
#include <map>
using jkgm::continuous_ambient_field;
using P=continuous_ambient_field::point;
static bool near(float a,float b,float eps=0.015f){return std::fabs(a-b)<eps;}
#ifdef JKGM_NATIVE_CAPTURE_TEST
struct mock_memory {
        std::unordered_map<uintptr_t,uint8_t> data;
        template<class T> void put(uintptr_t p,T const &v) {
            uint8_t const *b=reinterpret_cast<uint8_t const *>(&v);
            for(size_t i=0;i<sizeof(v);++i)data[p+i]=b[i];
        }
        bool read(uintptr_t p,void *out,size_t n) const {
            uint8_t *d=static_cast<uint8_t *>(out);
            for(size_t i=0;i<n;++i){
                auto it=data.find(p+i);
                if(it==data.end())return false;
                d[i]=it->second;
            }
            return true;
        }
};
#endif
int main(){
    continuous_ambient_field f;
    assert(!f.ready());
    assert(!f.add(1,{{NAN,0,0}},0.1f));
    for(int x=-2;x<=2;++x)for(int y=-2;y<=2;++y)for(int z=-2;z<=2;++z){
        assert(f.add(1,{{float(x),float(y),float(z)}},0.1f));
        assert(f.add(2,{{float(x),float(y),float(z)}},0.9f));
    }
    // A triangular opening in X=0.3, inside Y/Z +/-0.75.
    std::array<P,3> door{{P{{0.3f,-0.75f,-0.75f}},P{{0.3f,0.75f,-0.75f}},P{{0.3f,0.0f,0.75f}}}};
    assert(f.add_portal(1,2,door));
    assert(f.finalize() && f.ready());
    assert(!f.finalize());
    float a=0,b=0;
    auto zero=[](uint32_t,float &value){value=0;return true;};
    assert(f.sample(1,{{0.3f,0,0}},zero,a));
    assert(f.sample(2,{{0.3f,0,0}},zero,b));
    assert(near(a,b));
    assert(a>0.3f && a<0.7f);
    assert(f.sample(1,{{0.299f,0,0}},zero,a));
    assert(f.sample(2,{{0.301f,0,0}},zero,b));
    assert(near(a,b));
    // Same overlapping sectors, but a solid-wall position outside the
    // ACTUAL opening must retain distinct local authored values.
    assert(f.sample(1,{{0.3f,3,0}},zero,a));
    assert(f.sample(2,{{0.3f,3,0}},zero,b));
    assert(a<0.2f && b>0.8f);
    // Live sector ExtraLight without reconstruction. The shared voxel has
    // the same result from either owner and changes as either floor fades.
    std::map<uint32_t,float> floors{{1,0},{2,0}};
    auto live=[&](uint32_t id,float &v){auto i=floors.find(id);if(i==floors.end())return false;v=i->second;return true;};
    floors[1]=0.75f;
    assert(f.sample(1,{{0.3f,0,0}},live,a));
    assert(f.sample(2,{{0.3f,0,0}},live,b));
    assert(near(a,b));
    float first=a;
    floors[1]=1.0f;
    assert(f.sample(1,{{0.3f,0,0}},live,a));
    assert(a>first);
    // No portal, no connection; two sectors overlap spatially but retain
    // independent lighting. Quantized values respect [0,2] in HDR mode.
    f.reset(0.5f,true);
    assert(f.add(1,{{0,0,0}},1.6f));
    assert(f.add(2,{{0,0,0}},0.2f));
    assert(f.finalize());
    assert(f.sample(1,{{0,0,0}},zero,a) && a>1.55f);
    assert(f.sample(2,{{0,0,0}},zero,b) && b<0.25f);
    assert(f.node_count()>0);
    assert(f.stats().raw_nodes>0 && f.stats().final_nodes>0);
    assert(!f.sample(2,{{INFINITY,0,0}},zero,b));
    assert(!f.add(1,{{0,0,0}},0.4f)); // immutable after finalize
    // Multiple portals intersecting at the same voxel must reconcile
    // connected components transitively, independent of portal order.
    f.reset();
    for(uint32_t id=1;id<=3;++id)
        for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)
        for(int z=-1;z<=1;++z)
            assert(f.add(id,{{float(x),float(y),float(z)}},
                         0.1f+0.4f*float(id-1)));
    assert(f.add_portal(1,2,door));
    assert(f.add_portal(2,3,door));
    assert(f.finalize());
    float c=0;
    assert(f.sample(1,{{0.3f,0,0}},zero,a));
    assert(f.sample(2,{{0.3f,0,0}},zero,b));
    assert(f.sample(3,{{0.3f,0,0}},zero,c));
    assert(near(a,b) && near(b,c));
    assert(near(a,0.5f,0.04f));
    // No native pointer reads or GL dispatches occur in CPU sampling.
#ifdef JKGM_NATIVE_CAPTURE_TEST
    mock_memory mem;
    constexpr uintptr_t world=0x00100000U,sector_base=0x00200000U,
        surfaces=0x00300000U,lights=0x00400000U,
        adjoin=0x00500000U,vertices=0x00600000U,indices=0x00700000U;
    mem.put(world+0x4cU,uint32_t(2));
    mem.put(world+0x50U,uint32_t(sector_base));
    mem.put(world+0x88U,uint32_t(3));
    mem.put(world+0x8cU,uint32_t(vertices));
    mem.put(world+0xa8U,uint32_t(3));
    mem.put(world+0xacU,uint32_t(surfaces));
    mem.put(sector_base,uint32_t(7));
    mem.put(sector_base+0x98U,uint32_t(8));
    auto put_face=[&](uint32_t i,uint32_t sector,uint32_t light_pointer) {
        uintptr_t const face=surfaces+uintptr_t(i)*0x5cU;
        mem.put(face+0x08U,sector);
        mem.put(face+0x1cU,uint32_t(4));
        mem.put(face+0x20U,uint32_t(3));
        mem.put(face+0x54U,light_pointer);
    };
    put_face(0,uint32_t(sector_base),uint32_t(lights));
    put_face(1,uint32_t(sector_base+0x98U),uint32_t(lights+0x40U));
    for(uint32_t i=0;i<3;++i){
        mem.put(lights+i*4U,0.1f);
        mem.put(lights+0x40U+i*4U,0.9f);
    }
    // Invisible non-sky, PASSABLE portal (not in the visible mesh).
    uintptr_t const portal=surfaces+2U*0x5cU;
    mem.put(portal+0x08U,uint32_t(sector_base));
    mem.put(portal+0x0cU,uint32_t(adjoin));
    mem.put(portal+0x10U,uint32_t(0U));
    mem.put(portal+0x28U,uint32_t(3U));
    mem.put(portal+0x2cU,uint32_t(indices));
    mem.put(adjoin,uint32_t(2U));
    mem.put(adjoin+4U,uint32_t(sector_base+0x98U));
    for(uint32_t i=0;i<3;++i){
        mem.put(indices+i*4U,i);
        for(uint32_t d=0;d<3;++d)
            mem.put(vertices+i*12U+d*4U,door[i][d]);
    }
    jkgm::world_cache_mesh mesh{};
    mesh.indices={0,1,2,3,4,5};
    mesh.vertices.resize(6);
    for(uint32_t i=0;i<6;++i)mesh.vertices[i].position=
        {{i<3?-0.7f:1.3f,float(int(i%3)-1)*0.25f,0.0f}};
    for(uint32_t i=0;i<2;++i){
        jkgm::world_cache_surface_range r{};
        r.surface_index=i;
        r.first_index=i*3;
        r.count=3;
        mesh.surfaces.push_back(r);
    }
    auto reader=[&](uintptr_t p,void *dst,size_t n){return mem.read(p,dst,n);};
    jkgm::continuous_ambient_capture_stats stats{};
    assert(jkgm::capture_continuous_ambient(
        reader,world,surfaces,mesh,false,1.0f,f,stats));
    assert(stats.surfaces_sampled==2 && stats.vertices_sampled==6);
    assert(stats.passable_portals==1 && stats.triangles_accepted==1);
    assert(f.sample(7,{{0.3f,0,0}},zero,a));
    assert(f.sample(8,{{0.3f,0,0}},zero,b));
    assert(near(a,b));
    // Disabling native passability removes the stitched seam:
    mem.put(adjoin,uint32_t(0));
    assert(jkgm::capture_continuous_ambient(
        reader,world,surfaces,mesh,false,1.0f,f,stats));
    assert(stats.passable_portals==0);
    assert(f.sample(7,{{0.3f,0,0}},zero,a));
    assert(f.sample(8,{{0.3f,0,0}},zero,b));
    assert(a<0.2f && b>0.8f);
    // No sky Adjoin should link even if passable.
    mem.put(adjoin,uint32_t(2));
    mem.put(portal+0x10U,uint32_t(0x200U));
    assert(jkgm::capture_continuous_ambient(
        reader,world,surfaces,mesh,false,1.0f,f,stats));
    assert(stats.passable_portals==0);
    // Bad world resets previous samples; cannot leak stale geometry.
    assert(!jkgm::capture_continuous_ambient(
        reader,0,surfaces,mesh,false,1.0f,f,stats));
    assert(!f.ready() && f.node_count()==0);
#endif
}
