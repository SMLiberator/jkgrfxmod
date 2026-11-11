#include "renderer/volumetric_ambient.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace {
struct memory {
    std::unordered_map<uintptr_t,uint8_t> bytes;
    template<class T>
    void put(uintptr_t address,T const &value) {
        auto const *src=reinterpret_cast<uint8_t const *>(&value);
        for(size_t i=0;i<sizeof(value);++i)bytes[address+i]=src[i];
    }
    bool read(uintptr_t address,void *out,size_t n) const {
        auto *dest=static_cast<uint8_t *>(out);
        for(size_t i=0;i<n;++i) {
            auto it=bytes.find(address+i);
            if(it==bytes.end())return false;
            dest[i]=it->second;
        }
        return true;
    }
};
bool near(float a,float b,float tolerance=0.03f) {
    return std::fabs(a-b)<tolerance;
}
} // namespace

int main() {
    jkgm::volumetric_ambient_field field;
    float brightness=123.0f;
    assert(!field.sample(7,{{0.5f,0.5f,0.5f}},brightness));
    assert(brightness==123.0f);

    // Sample eight real corners; interpolation happens inside one sector.
    for(unsigned corner=0;corner<8;++corner) {
        std::array<float,3> const position{{
            float(corner&1U),float((corner>>1)&1U),
            float((corner>>2)&1U)}};
        assert(field.add(7,0x00200000U,position,
                         (corner&1U)?1.0f:0.0f,false));
    }
    // A neighbor's light never enters this sector's volume.
    for(unsigned corner=0;corner<8;++corner) {
        std::array<float,3> const position{{
            float(corner&1U),float((corner>>1)&1U),
            float((corner>>2)&1U)}};
        assert(field.add(8,0x00200098U,position,0.8f,false));
    }
    assert(!field.add(7,0x00200098U,{{0.0f,0.0f,0.0f}},0.5f,false));
    assert(field.source_count()==16U);
    assert(field.finalize(false) && field.ready());
    assert(!field.finalize(false));
    assert(field.sector_count()==2U);
    assert(field.sectors()[0].id==7U && field.sectors()[1].id==8U);
    assert(field.sectors()[0].range==1.0f);
    assert(field.sample(7,{{0.0f,0.0f,0.0f}},brightness));
    assert(near(brightness,0.0f));
    assert(field.sample(7,{{1.0f,0.0f,0.0f}},brightness));
    assert(near(brightness,1.0f));
    assert(field.sample(7,{{0.25f,0.0f,0.0f}},brightness));
    assert(near(brightness,0.25f,0.04f));
    assert(field.sample(7,{{0.75f,0.0f,0.0f}},brightness));
    assert(near(brightness,0.75f,0.04f));
    assert(field.sample(8,{{0.25f,0.2f,0.8f}},brightness));
    assert(near(brightness,0.8f,0.01f));
    assert(!field.sample(9,{{0.0f,0.0f,0.0f}},brightness));
    assert(!field.sample(7,{{INFINITY,0.0f,0.0f}},brightness));
    // Sample source arrays were discarded after quantization.
    assert(!field.add(7,0x00200000U,{{0,0,0}},0.5f,false));

    // The range is explicit and honors baked overbright, not auto-exposure.
    field.reset();
    assert(field.add(1,0x00200000U,{{0,0,0}},1.8f,true));
    assert(field.add(1,0x00200000U,{{1,1,1}},3.0f,true));
    assert(field.finalize(true));
    assert(field.sectors()[0].range==2.0f);
    assert(field.sample(1,{{1,1,1}},brightness));
    assert(brightness>1.95f && brightness<=2.0f);
    field.reset();
    assert(field.add(1,0x00200000U,{{0,0,0}},3.0f,false));
    assert(field.finalize(false));
    assert(field.sample(1,{{0,0,0}},brightness) && brightness==1.0f);

    // Mock native SithWorld capture. Sector +0x04 AmbientLight and +0x08
    // ExtraLight are deliberately absent: this must still build correctly.
    // Face ExtraLight is present and intentionally NOT baked into the field.
    constexpr uintptr_t world=0x00100000U;
    constexpr uintptr_t sectors=0x00200000U;
    constexpr uintptr_t surfaces=0x00300000U;
    constexpr uintptr_t intensities=0x00400000U;
    memory mem;
    mem.put(world+0x4cU,uint32_t(2));
    mem.put(world+0x50U,uint32_t(sectors));
    mem.put(sectors,uint32_t(7));
    mem.put(sectors+0x98U,uint32_t(8));
    auto face=[&](uint32_t index,uint32_t sector,uint32_t lights) {
        uintptr_t const addr=surfaces+uintptr_t(index)*0x5cU;
        mem.put(addr+0x08U,sector);
        mem.put(addr+0x1cU,uint32_t(4)); // textured
        mem.put(addr+0x20U,uint32_t(3)); // Gouraud
        mem.put(addr+0x44U,1.5f); // ignored face ExtraLight
        mem.put(addr+0x54U,lights);
    };
    face(0,uint32_t(sectors),uint32_t(intensities));
    face(1,uint32_t(sectors+0x98U),uint32_t(intensities+0x100U));
    face(2,uint32_t(sectors),uint32_t(intensities+0x200U));
    mem.put(intensities,0.0f);
    mem.put(intensities+4U,1.0f);
    mem.put(intensities+8U,0.0f);
    for(unsigned i=0;i<3;++i) {
        mem.put(intensities+0x100U+i*4U,0.7f);
        mem.put(intensities+0x200U+i*4U,2.0f);
    }
    jkgm::world_cache_mesh mesh{};
    mesh.vertices.resize(9);
    mesh.indices={0,1,2,3,4,5,6,7,8};
    for(size_t i=0;i<mesh.vertices.size();++i)
        mesh.vertices[i].position={{float(i%3U),0.0f,0.0f}};
    for(uint32_t i=0;i<3;++i) {
        jkgm::world_cache_surface_range range{};
        range.surface_index=i;
        range.first_index=i*3U;
        range.count=3U;
        range.surface_flags=i==2U?0x200U:0U; // skip sky light
        mesh.surfaces.push_back(range);
    }
    auto const read=[&](uintptr_t addr,void *dst,size_t size) {
        return mem.read(addr,dst,size);
    };
    jkgm::volumetric_ambient_stats stats{};
    assert(jkgm::capture_volumetric_ambient(
        read,world,surfaces,mesh,false,field,stats));
    assert(stats.surfaces_seen==3U && stats.surfaces_sampled==2U);
    assert(stats.surfaces_skipped==1U && stats.vertices_sampled==6U);
    assert(stats.sectors==2U && field.sector_count()==2U);
    assert(field.sample(8,{{1,0,0}},brightness));
    assert(near(brightness,0.7f,0.02f));
    assert(field.sample(7,{{0,0,0}},brightness));
    assert(brightness<0.15f); // no face ExtraLight added!
    assert(jkgm::capture_volumetric_ambient(
        read,world,surfaces,mesh,true,field,stats));
    assert(stats.sectors==2U && field.sectors()[0].range==2.0f);
    // Invalid native table fails closed and resets old samples.
    assert(!jkgm::capture_volumetric_ambient(
        read,0U,surfaces,mesh,false,field,stats));
    assert(!field.ready() && field.sector_count()==0U);
    return 0;
}
