#include "renderer/sun_shadow.hpp"
#include "renderer/sun_shadow_scroll.hpp"
#include "renderer/sun_skybox_thing.hpp"
#include <cassert>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_set>

using namespace jkgm;

int main()
{
    // Fixed light-space units are independent of any camera FOV changes:
    // changing zoom must not reset a previously populated static atlas.
    assert(sun_shadow_scroll_fixed_tile_units(4.0f)==2.0);
    assert(sun_shadow_scroll_fixed_tile_units(40.0f)==16.0);
    sun_shadow_scroll_grid stable(16);
    auto stable_dirty=stable.update(0.0,0.0,
        sun_shadow_scroll_fixed_tile_units(40.0f),1);
    assert(stable_dirty.size()==256);
    for(auto const &tile:stable.pending_tiles())
        assert(stable.mark_rendered(tile));
    assert(stable.update(1.0,0.0,
        sun_shadow_scroll_fixed_tile_units(40.0f),1).empty());
    assert(stable.pending_tiles().empty());
    auto strip=stable.update(16.1,0.0,
        sun_shadow_scroll_fixed_tile_units(40.0f),1);
    assert(strip.size()==16);
    assert(stable.pending_tiles().size()==16);
    // Circular tile addressing: 32x32 JK units, 2x2-unit tiles.
    sun_shadow_scroll_grid grid(16);
    auto dirty=grid.update(0.0,0.0,2.0,10);
    assert(dirty.size()==256);
    assert(grid.pending_tiles().size()==256);
    assert(grid.pending_tiles(10).size()==10);
    // Planning isn't rendering; all tiles stay pending until acknowledged.
    assert(grid.update(0.8,0.5,2.0,10).empty());
    assert(grid.pending_tiles().size()==256);
    for(auto const &tile:grid.pending_tiles())assert(grid.mark_rendered(tile));
    assert(grid.pending_tiles().empty());
    assert(grid.origin_x==-8 && grid.origin_y==-8);
    assert(grid.physical_slot(-8,-8).atlas_x==0);
    assert(grid.physical_slot(7,7).atlas_x==15);
    assert(grid.update(0.8,0.5,2.0,10).empty());
    dirty=grid.update(2.01,0.0,2.0,10);
    assert(dirty.size()==16);
    assert(grid.origin_x==-7 && grid.ring_x==1);
    for(auto const &tile:dirty) {
        assert(tile.world_x==8);
        assert(tile.atlas_x==0);
    }
    assert(grid.pending_tiles().size()==16);
    assert(!grid.mark_rendered({-8,0,0,8})); // old world address
    // Sub-tile static geometry changes invalidate only nearby ray columns.
    auto overlap=grid.invalidate_light_rect(0.25,0.75,0.25,0.75);
    assert(!overlap.empty() && overlap.size()<=9);
    assert(grid.pending_tiles().size()==16+overlap.size());
    assert(grid.invalidate_light_rect(0.25,0.75,0.25,0.75).empty());
    for(auto const &tile:grid.pending_tiles())assert(grid.mark_rendered(tile));
    assert(grid.pending_tiles().empty());
    assert(grid.physical_slot(-7,0).atlas_x==1);
    dirty=grid.update(2.01,2.01,2.0,10);
    assert(dirty.size()==16);
    for(auto const &tile:dirty)assert(tile.world_y==8);
    // Diagonal shift has 31 unique new cells, not 32 duplicates.
    dirty=grid.update(4.01,4.01,2.0,10);
    assert(dirty.size()==31);
    dirty=grid.update(0.0,0.0,2.0,10);
    assert(dirty.size()==60); // two columns + two rows, no duplication
    assert(grid.physical_slot(-8,-8).atlas_x==0);
    assert(grid.physical_slot(-8,-8).atlas_y==0);
    // A camera teleport or a sun/caster revision needs every tile refreshed.
    dirty=grid.update(100.0,100.0,2.0,10);
    assert(dirty.size()==256);
    assert(grid.update(100.0,100.0,2.0,11).size()==256);
    assert(grid.update(100.0,100.0,4.0,11).size()==256);
    grid.invalidate();
    assert(grid.update(-0.1,-0.1,2.0,11).size()==256);
    assert(grid.origin_x==-9 && grid.origin_y==-9);
    assert(sun_shadow_scroll_grid::wrap(-1,16)==15);
    // Unknown/NaN geometry conservatively invalidates the full cache.
    for(auto const &tile:grid.pending_tiles())assert(grid.mark_rendered(tile));
    assert(grid.pending_tiles().empty());
    auto bad_bounds=grid.invalidate_light_rect(
        std::numeric_limits<double>::quiet_NaN(),0.0,0.0,0.0);
    assert(bad_bounds.size()==256);
    assert(grid.pending_tiles().size()==256);
    // Depth atlas reconstruction: no tile wrap may reinterpret an unrelated
    // physical slot, and the destination region remains wholly covered.
    assert(sun_shadow_scroll_depth_contains(0.0f,100.0f,3.0f,90.0f));
    assert(sun_shadow_scroll_depth_contains(0.0f,100.0f,0.0f,100.0f));
    assert(!sun_shadow_scroll_depth_contains(0.0f,100.0f,-1.0f,100.0f));
    assert(!sun_shadow_scroll_depth_contains(0.0f,100.0f,1.0f,101.0f));
    sun_shadow_scroll_grid reconstruct(16);
    assert(reconstruct.update(0.0,0.0,2.0,1).size()==256);
    for(auto const &tile:reconstruct.pending_tiles())
        assert(reconstruct.mark_rendered(tile));
    auto blits=sun_shadow_scroll_blits(reconstruct,-4.0,-4.0,8.0,8.0,512);
    assert(blits.size()>=1 && blits.size()<=4);
    int area=0;
    for(auto const &blit:blits) {
        assert(blit.source_x0>=0 && blit.source_x1<=512);
        assert(blit.source_y0>=0 && blit.source_y1<=512);
        area+=(blit.target_x1-blit.target_x0)*
              (blit.target_y1-blit.target_y0);
    }
    assert(area==512*512);
    reconstruct.update(2.1,0.0,2.0,1);
    for(auto const &tile:reconstruct.pending_tiles())
        assert(reconstruct.mark_rendered(tile));
    blits=sun_shadow_scroll_blits(reconstruct,-2.0,-4.0,8.0,8.0,512);
    assert(blits.size()>=1 && blits.size()<=4);
    // Cross BOTH wrap boundaries; the circular atlas must require exactly
    // four physical regions, regardless of its 256 logical tiles.
    reconstruct.update(2.1,2.1,2.0,1);
    for(auto const &tile:reconstruct.pending_tiles())
        assert(reconstruct.mark_rendered(tile));
    blits=sun_shadow_scroll_blits(reconstruct,14.0,14.0,4.0,4.0,512);
    assert(blits.size()==4);
    assert(sun_shadow_scroll_blits(reconstruct,-50.0,-4.0,8.0,8.0,512).empty());
    assert(sun_shadow_scroll_blits(reconstruct,-2.0,-4.0,8.0,8.0,500).empty());
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
