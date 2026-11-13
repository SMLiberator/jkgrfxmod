// Verify the same model->mesh sphere hierarchy JK uses in both camera
// paths, independent of native renderer state or GL device availability.
#include "renderer/planar_reflection_culling.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>

int main() {
    using namespace jkgm;
    gpu_3do_native_frustum f{};
    f.clip_far=1;f.near_plane=1.0f;f.far_plane=100.0f;
    f.far_top=1.0f;f.bottom=-1.0f;
    f.far_left=-1.0f;f.right=1.0f;
    engine_camera main{};
    main.valid=true;
    auto const view=planar_camera_native_view(main);
    assert(gpu_3do_matrix_finite(view));
    std::unordered_map<uintptr_t,uint8_t> memory;
    auto put=[&](uintptr_t address,auto const &value) {
        uint8_t const *p=reinterpret_cast<uint8_t const*>(&value);
        for(size_t i=0;i<sizeof(value);++i)memory[address+i]=p[i];
    };
    auto read=[&](uintptr_t address,void *dst,size_t count) {
        uint8_t *out=static_cast<uint8_t*>(dst);
        for(size_t i=0;i<count;++i) {
            auto const found=memory.find(address+i);
            if(found==memory.end())return false;
            out[i]=found->second;
        }
        return true;
    };
    planar_reflection_model_thing thing{};
    thing.thing=0x40000U;
    thing.model=0x70000U;
    thing.position={{0.0f,10.0f,0.0f}};
    std::array<float,9> identity{{1,0,0,0,1,0,0,0,1}};
    put(thing.thing+0x88U,identity);
    put(thing.model+0x5cU,1.0f);
    assert(!planar_reflection_thing_outside_frustum(read,thing,f,view));
    // Whole Thing rejected before animation/mesh acquisition.
    thing.position={{80.0f,10.0f,0.0f}};
    assert(planar_reflection_thing_outside_frustum(read,thing,f,view));
    // Model radius must include the entire animated hierarchy.
    put(thing.model+0x5cU,100.0f);
    assert(!planar_reflection_thing_outside_frustum(read,thing,f,view));
    put(thing.model+0x5cU,1.0f);
    // Scaled models remain conservatively visible near frustum sides.
    std::array<float,9> scaled{{30,0,0,0,1,0,0,0,1}};
    put(thing.thing+0x88U,scaled);
    thing.position={{35.0f,10.0f,0.0f}};
    assert(!planar_reflection_thing_outside_frustum(read,thing,f,view));
    // Missing/invalid model radius or orientation NEVER falsely rejects.
    put(thing.model+0x5cU,0.0f);
    assert(!planar_reflection_thing_outside_frustum(read,thing,f,view));
    put(thing.model+0x5cU,std::numeric_limits<float>::quiet_NaN());
    assert(!planar_reflection_thing_outside_frustum(read,thing,f,view));
    put(thing.model+0x5cU,1.0f);
    memory.erase(thing.thing+0x88U);
    assert(!planar_reflection_thing_outside_frustum(read,thing,f,view));
    put(thing.thing+0x88U,identity);
    // The reflected view must NOT use the original camera's results.
    main.position={{0.0f,0.0f,10.0f}};
    auto const original_view=planar_camera_native_view(main);
    planar_reflection_plane plane{};
    auto const mirrored=make_planar_reflection_camera(main,plane);
    assert(mirrored.valid);
    auto const reflected_view=planar_camera_native_view(mirrored);
    thing.position={{0.0f,10.0f,-5.0f}};
    assert(planar_reflection_thing_outside_frustum(
        read,thing,f,original_view)); // z=-15, outside original
    assert(!planar_reflection_thing_outside_frustum(
        read,thing,f,reflected_view)); // z=-5, inside reflected
    // Mesh sphere culling uses each *posed* joint translation, before
    // source hashing; a visible Thing can still reject remote meshes.
    gpu_3do_matrix34 mesh{{1,0,0,0,1,0,0,0,1,80,10,0}};
    assert(planar_reflection_mesh_outside_frustum(f,view,mesh,1.0f));
    mesh[9]=9.0f;
    assert(!planar_reflection_mesh_outside_frustum(f,view,mesh,1.0f));
    mesh[9]=80.0f;
    mesh[0]=100.0f;
    assert(!planar_reflection_mesh_outside_frustum(f,view,mesh,1.0f));
    assert(!planar_reflection_mesh_outside_frustum(f,view,mesh,0.0f));
    assert(!planar_reflection_mesh_outside_frustum(
        f,view,mesh,std::numeric_limits<float>::infinity()));
    // Nonorthogonal/sheared placements must have a conservative upper
    // bound, rather than treating maximum column length as exact.
    std::array<float,9> sheared{{1,0,0,1,1,0,0,0,1}};
    assert(planar_reflection_radius_scale(sheared)>1.5f);
    return 0;
}
