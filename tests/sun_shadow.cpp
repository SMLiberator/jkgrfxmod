#include "renderer/sun_shadow.hpp"
#include "renderer/sun_skybox_thing.hpp"
#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>
#include <unordered_set>

using namespace jkgm;

int main()
{
    std::unordered_set<uint64_t> owners{0x1111222233334444ULL};
    assert(!sun_shadow_caster_in_static_cascades(
        0x1111222233334444ULL,owners));
    assert(sun_shadow_caster_in_static_cascades(
        0x1111222233334445ULL,owners));
    assert(sun_shadow_caster_in_static_cascades(0,owners));
    owners.clear();
    assert(sun_shadow_caster_in_static_cascades(
        0x1111222233334444ULL,owners));
    // Static architectural Things are reusable, but movers, unknown owners,
    // disabled caching and missing FBOs always use the live 3DO pass.
    assert(sun_shadow_cache_stationary_model(1234,false,true,true));
    assert(!sun_shadow_cache_stationary_model(1234,true,true,true));
    assert(!sun_shadow_cache_stationary_model(0,false,true,true));
    assert(!sun_shadow_cache_stationary_model(1234,false,false,true));
    assert(!sun_shadow_cache_stationary_model(1234,false,true,false));
    // Exact transformed bounds for translated, rotated, and mirrored models.
    auto const xform=sun_shadow_transformed_aabb(
        {{-2.0f,-1.0f,-3.0f}},{{4.0f,5.0f,7.0f}},
        {{0.0f,-2.0f,0.0f,1.0f,0.0f,0.0f,0.0f,0.0f,-1.0f,
          10.0f,20.0f,30.0f}});
    assert(std::fabs(xform.low[0]-9.0f)<0.0001f);
    assert(std::fabs(xform.high[0]-15.0f)<0.0001f);
    assert(std::fabs(xform.low[1]-12.0f)<0.0001f);
    assert(std::fabs(xform.high[1]-24.0f)<0.0001f);
    assert(std::fabs(xform.low[2]-23.0f)<0.0001f);
    assert(std::fabs(xform.high[2]-33.0f)<0.0001f);
    // Empty-world data is a valid shadow shell for 3DO-only levels.
    constexpr uintptr_t world_addr=0x120000U;
    std::array<unsigned char,0xb0> header{};
    decal_native_read read=[&](uintptr_t addr,void *out,size_t size) {
        if(addr<world_addr || addr-world_addr>header.size() ||
           size>header.size()-(addr-world_addr))return false;
        std::memcpy(out,header.data()+(addr-world_addr),size);
        return true;
    };
    // rdThing.pThing -> SithThing.flags must be checked on the current
    // owner, not on rdMesh (whose geometry can be shared between Things).
    std::array<unsigned char,0x120> thing{};
    uint32_t const type=5,identity=1234,skybox=thingflag_skybox;
    uint32_t const owner=uint32_t(world_addr);
    std::memcpy(thing.data(),&skybox,4);
    std::memcpy(thing.data()+0x0c,&type,4);
    std::memcpy(thing.data()+0x74,&identity,4);
    std::memcpy(thing.data()+0xc4+0x44,&owner,4);
    auto native_read=[&](uintptr_t addr,void *out,size_t n) {
        if(addr<world_addr || addr-world_addr>thing.size() ||
           n>thing.size()-(addr-world_addr))return false;
        std::memcpy(out,thing.data()+(addr-world_addr),n);
        return true;
    };
    uint32_t flags=0;
    assert(read_native_model_thing_flags(native_read,world_addr+0xc4,flags));
    assert(flags==thingflag_skybox && !thing_casts_sun_shadows(flags));
    flags=~0U;
    assert(!read_native_model_thing_flags(
        native_read,world_addr+0xc8,flags));
    assert(flags==0U && thing_casts_sun_shadows(flags));
    // Runtime COG bit changes take effect without resnapshotting rdMesh.
    uint32_t ordinary_flags=0;
    std::memcpy(thing.data(),&ordinary_flags,4);
    assert(read_native_model_thing_flags(native_read,world_addr+0xc4,flags));
    assert(thing_casts_sun_shadows(flags));

    decal_world_geometry world{};
    assert(world.load(read,world_addr,250000U,true));
    assert(world.valid_world && world.receivers.empty());
    assert(world.matches(read,world_addr));
    spotlight_shadow_static_mesh mesh{};
    assert(build_spotlight_shadow_static_mesh(world,mesh));
    assert(mesh.indices.empty());
    decal_world_geometry decals{};
    assert(!decals.load(read,world_addr,250000U,false));
    // A model-built level can have no usable native world receivers.
    decal_volume_aabb architecture{};
    architecture.low={{100.0f,200.0f,0.0f}};
    architecture.high={{120.0f,220.0f,0.0f}};
    sun_shadow_projection projection{};
    assert(make_sun_shadow_projection(world,{{0.0f,0.0f,1.0f}},
                                      projection,{architecture}));
    auto near=sun_shadow_clip(projection,{{110.0f,210.0f,0.0f}});
    assert(near.x>-1.0f && near.x<1.0f);
    assert(near.y>-1.0f && near.y<1.0f);
    assert(near.z>-1.0f && near.z<1.0f);

    // Nearby view coverage stays compact even with a distant level shell.
    assert(fit_sun_shadow_to_camera(projection,{{105.0f,205.0f,2.0f}},
        {{1.0f,0.0f,0.0f}},{{0.0f,0.0f,1.0f}},
        {{0.0f,1.0f,0.0f}},0.5f,0.4f,20.0f));
    assert(projection.width<50.0f && projection.height<50.0f);
    assert(stabilize_sun_shadow_projection(projection,512U));
    assert(!stabilize_sun_shadow_projection(projection,0U));
    // Near and far maps share caster depth but fit different screen regions.
    auto near_projection=projection;
    auto far_projection=projection;
    assert(fit_sun_shadow_to_camera(near_projection,{{105.0f,205.0f,2.0f}},
        {{1.0f,0.0f,0.0f}},{{0.0f,0.0f,1.0f}},
        {{0.0f,1.0f,0.0f}},0.5f,0.4f,sun_shadow_near_distance));
    assert(fit_sun_shadow_to_camera(far_projection,{{105.0f,205.0f,2.0f}},
        {{1.0f,0.0f,0.0f}},{{0.0f,0.0f,1.0f}},
        {{0.0f,1.0f,0.0f}},0.5f,0.4f,sun_shadow_far_distance));
    assert(sun_shadow_projection_covers_camera(
        near_projection,{{105.0f,205.0f,2.0f}},
        {{1.0f,0.0f,0.0f}},{{0.0f,0.0f,1.0f}},
        {{0.0f,1.0f,0.0f}},0.5f,0.4f,sun_shadow_near_distance));
    assert(!sun_shadow_projection_covers_camera(
        near_projection,{{205.0f,305.0f,2.0f}},
        {{1.0f,0.0f,0.0f}},{{0.0f,0.0f,1.0f}},
        {{0.0f,1.0f,0.0f}},0.5f,0.4f,sun_shadow_near_distance));
    assert(near_projection.width<far_projection.width);
    assert(near_projection.height<far_projection.height);
    assert(near_projection.range==far_projection.range);
    // The near cascade gets four times the previous linear shadow density
    // over the same 1024x1024 map area, and blends over its final two units.
    assert(sun_shadow_near_distance==16.0f);
    assert(sun_shadow_blend_start==13.0f);
    assert(sun_shadow_blend_end==15.0f);
    assert(sun_shadow_blend_start<sun_shadow_blend_end);
    assert(sun_shadow_blend_end<sun_shadow_near_distance);
    // A world-space texel is far smaller in the local cascade even though
    // both textures have the same pixel dimensions.
    assert(near_projection.width/1024.0f < far_projection.width/1024.0f);
    assert(near_projection.height/1024.0f < far_projection.height/1024.0f);
    assert(stabilize_sun_shadow_projection(near_projection,512U));
    assert(stabilize_sun_shadow_projection(far_projection,512U));
    // Reusing a cached depth layer requires matching every projection
    // component: even a rotated light with the same origin/extent is wrong.
    assert(sun_shadow_same_projection(near_projection,near_projection));
    auto altered_projection=near_projection;
    altered_projection.width+=0.01f;
    assert(!sun_shadow_same_projection(near_projection,altered_projection));
    altered_projection=near_projection;
    altered_projection.forward[0]+=0.01f;
    assert(!sun_shadow_same_projection(near_projection,altered_projection));
    altered_projection=near_projection;
    altered_projection.origin[1]+=0.01f;
    assert(!sun_shadow_same_projection(near_projection,altered_projection));
    assert(!sun_shadow_same_projection(
        near_projection,near_projection,-0.01f));
    auto visible=sun_shadow_clip(projection,{{105.0f,215.0f,2.0f}});
    assert(visible.x>-1.0f && visible.x<1.0f);
    assert(visible.y>-1.0f && visible.y<1.0f);

    decal_volume_aabb actor{};
    actor.low={{107.0f,208.0f,1.0f}};
    actor.high={{108.0f,209.0f,3.0f}};
    sun_shadow_projection object_shadow{};
    assert(make_sun_dynamic_shadow_projection(projection,actor,object_shadow));
    assert(object_shadow.width>0.0f && object_shadow.height>0.0f);
    assert(object_shadow.range==projection.range);
    auto actor_point=sun_shadow_clip(object_shadow,{{107.5f,208.5f,2.0f}});
    assert(actor_point.x>=-1.0f && actor_point.x<=1.0f);
    assert(actor_point.y>=-1.0f && actor_point.y<=1.0f);
    assert(!make_sun_dynamic_shadow_projection(projection,actor,object_shadow,-1.0f));
    // A small native shell must not crop the remote 3DO architecture.
    decal_world_surface_geometry shell{};
    shell.polygon={{{0.0f,0.0f,0.0f}},{{1.0f,0.0f,0.0f}},
                   {{0.0f,1.0f,0.0f}}};
    world.receivers.push_back(shell);
    assert(make_sun_shadow_projection(world,{{0.0f,0.0f,1.0f}},
                                      projection,{architecture}));
    near=sun_shadow_clip(projection,{{110.0f,210.0f,0.0f}});
    assert(near.x>-1.0f && near.x<1.0f);
    assert(near.y>-1.0f && near.y<1.0f);
    assert(near.z>-1.0f && near.z<1.0f);
}
