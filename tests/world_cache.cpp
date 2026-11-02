#include "renderer/world_cache.hpp"
#include "renderer/world_cache_alpha.hpp"
#include "renderer/world_cache_sky.hpp"
#include "renderer/world_cache_portals.hpp"
#include "renderer/world_cache_lighting.hpp"
#include "renderer/world_cache_uv_animation.hpp"
#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace jkgm;

static decal_world_surface_geometry make_surface(uint32_t index,float x)
{
    decal_world_surface_geometry s{};
    s.surface_index=index;
    s.material=0x10000U+index;
    s.cel=3;
    s.normal={{0.0f,-1.0f,0.0f}};
    s.polygon={{{x,0.0f,0.0f}},
               {{x+2.0f,0.0f,0.0f}},
               {{x+2.0f,0.0f,2.0f}},
               {{x,0.0f,2.0f}}};
    s.has_shadow_uv=true;
    s.shadow_uv={{{0,0}},{{64,0}},{{64,64}},{{0,64}}};
    return s;
}

int main()
{
    decal_world_geometry missing_world{};
    world_cache_mesh invalid{};
    assert(!build_world_cache_mesh(missing_world,invalid));
    assert(invalid.vertices.empty());

    // A valid model-only level has a valid but empty world snapshot.
    missing_world.valid_world=true;
    assert(build_world_cache_mesh(missing_world,invalid));
    assert(invalid.indices.empty() && invalid.surfaces.empty());

    decal_world_geometry world{};
    world.valid_world=true;
    world.receivers.push_back(make_surface(2U,0.0f));
    world.receivers.push_back(make_surface(7U,10.0f));
    world.receivers[1].solid_adjoin=true;
    world.receivers[1].has_shadow_uv=false; // safe opaque fallback is native
    world_cache_mesh mesh{};
    assert(build_world_cache_mesh(world,mesh));
    assert(mesh.vertices.size()==8 && mesh.indices.size()==12);
    assert(mesh.surfaces.size()==2U);
    assert(mesh.surfaces[0].surface_index==2U);
    assert(mesh.sky_surfaces==0U);
    assert(mesh.surfaces[0].first_index==0U);
    assert(mesh.surfaces[0].count==6U);
    assert(mesh.surfaces[1].surface_index==7U);
    assert(mesh.surfaces[1].first_index==6U);
    assert(mesh.surfaces[1].count==6U);
    assert(mesh.surfaces[1].solid_adjoin);
    assert(mesh.uv_ready_surfaces==1U && mesh.missing_uv_surfaces==1U);
    assert(mesh.indices[0]==0U && mesh.indices[1]==1U &&
           mesh.indices[2]==2U && mesh.indices[3]==0U &&
           mesh.indices[4]==2U && mesh.indices[5]==3U);
    assert(mesh.indices[6]==4U && mesh.indices[11]==7U);
    assert(mesh.vertices[1].position[0]==2.0f);
    assert(mesh.vertices[2].uv[1]==64.0f);
    assert(mesh.vertices[5].uv[0]==0.0f); // missing source UV not fabricated
    assert(mesh.vertices[3].normal[1]==-1.0f);
    // Backface culling is mandatory, including SithSurface flag 0x1.
    // A clockwise authored polygon must be corrected at triangulation,
    // so its visible front still matches the native face normal.
    decal_world_geometry reversed_world{};
    reversed_world.valid_world=true;
    auto reverse_face=make_surface(9U,20.0f);
    reverse_face.surface_flags=0x1U;
    std::reverse(reverse_face.polygon.begin(),reverse_face.polygon.end());
    assert(world_cache_reverse_fan_triangle(
        reverse_face.polygon[0],reverse_face.polygon[1],
        reverse_face.polygon[2],reverse_face.normal));
    reversed_world.receivers.push_back(reverse_face);
    world_cache_mesh reverse_mesh{};
    assert(build_world_cache_mesh(reversed_world,reverse_mesh));
    assert(reverse_mesh.surfaces[0].surface_flags==0x1U);
    assert(reverse_mesh.indices[0]==0U &&
           reverse_mesh.indices[1]==2U &&
           reverse_mesh.indices[2]==1U);
    assert(!world_cache_reverse_fan_triangle(
        world.receivers[0].polygon[0],
        world.receivers[0].polygon[1],
        world.receivers[0].polygon[2],
        world.receivers[0].normal));


    // GPU JK world lighting follows the live surface's Gouraud array,
    // native world-vertex dynamic contributions, face extra and ambient.
    constexpr uintptr_t light_base=0x400000U;
    std::array<uint8_t,0x4000> light_memory{};
    auto put_light=[&](uintptr_t addr,void const *src,size_t n) {
        assert(addr>=light_base &&
               addr-light_base+n<=light_memory.size());
        std::memcpy(light_memory.data()+(addr-light_base),src,n);
    };
    auto lu32=[&](uintptr_t addr,uint32_t v){put_light(addr,&v,4);};
    auto lf32=[&](uintptr_t addr,float v){put_light(addr,&v,4);};
    decal_native_read light_read=[&](uintptr_t addr,void *dst,size_t n) {
        if(addr<light_base || addr-light_base+n>light_memory.size())
            return false;
        std::memcpy(dst,light_memory.data()+(addr-light_base),n);
        return true;
    };
    constexpr uintptr_t light_surfaces=light_base+0x400U;
    constexpr uintptr_t light_sector=light_base+0x800U;
    constexpr uintptr_t light_world=light_base+0xc00U;
    constexpr uintptr_t light_indices=light_base+0x1000U;
    constexpr uintptr_t light_intensities=light_base+0x1100U;
    constexpr uintptr_t light_dynamic=light_base+0x1200U;
    lu32(light_surfaces+2U*0x5cU+0x08U,uint32_t(light_sector));
    lu32(light_surfaces+2U*0x5cU+0x20U,3U);
    lu32(light_surfaces+2U*0x5cU+0x2cU,uint32_t(light_indices));
    lu32(light_surfaces+2U*0x5cU+0x54U,uint32_t(light_intensities));
    // SithSurface +0x58 is lastTouchedMs, NOT the light pointer.
    lu32(light_surfaces+2U*0x5cU+0x58U,0xDEADBEEFU);
    lf32(light_surfaces+2U*0x5cU+0x44U,0.10f);
    lf32(light_sector+0x08U,0.20f);
    lf32(light_sector+0x10U,0.60f);
    lf32(light_sector+0x14U,0.15f);
    lf32(light_sector+0x18U,0.05f);
    for(uint32_t i=0;i<4;++i) {
        lu32(light_indices+i*4U,i+1U);
        lf32(light_intensities+i*4U,0.10f*float(i));
    }
    lu32(light_world+0x88U,8U);
    lu32(light_world+0x98U,uint32_t(light_dynamic));
    for(uint32_t i=0;i<8;++i)
        lf32(light_dynamic+i*4U,0.05f);
    std::vector<float> dynamic_lights;
    assert(read_world_cache_dynamic_lights(
        light_read,light_world,8U,dynamic_lights));
    // Both GPU pixel renderers MUST share the same dynamic-light model.
    // Explicitly make attenuationMin differ from attenuationMax and
    // minRadius differ from maxRadius to catch the previous regression.
    // Zero lights must also preserve the pixel path (stable baked colors).
    constexpr uintptr_t cam_global=light_base+0x1800U;
    constexpr uintptr_t cam=light_base+0x1900U;
    constexpr uintptr_t native_light=light_base+0x2200U;
    lu32(cam_global,uint32_t(cam));
    lf32(cam+0x54U,0.20f);
    lu32(cam+0x58U,0U);
    lf32(cam+0x45cU,0.20f); // same attenuationMin as cached 3DOs
    lf32(cam+0x460U,0.50f); // native Gouraud/world value, NOT pixel path
    std::vector<world_cache_point_light> radial_lights;
    float cam_ambient=0.0f,cam_attenuation=0.0f;
    assert(read_world_cache_camera_lights(
        light_read,cam_global,radial_lights,
        cam_ambient,cam_attenuation));
    assert(radial_lights.empty() && cam_attenuation==0.20f);
    assert(world_cache_use_pixel_lights(true,true));
    assert(!world_cache_use_pixel_lights(false,true));
    assert(!world_cache_use_pixel_lights(true,false));
    assert(!world_cache_use_pixel_lights(false,false));
    lu32(cam+0x58U,1U);
    lu32(cam+0x5cU,uint32_t(native_light));
    lu32(native_light,0U); // position index into rdCamera
    lu32(native_light+4U,0U); // valid point light
    lu32(native_light+8U,1U); // enabled
    lf32(native_light+0x18U,0.8f);
    lf32(native_light+0x28U,4.0f); // same minRadius as cached 3DOs
    lf32(native_light+0x2cU,1.6f); // native Gouraud/world cutoff
    lf32(cam+0x15cU,0.0f);
    lf32(cam+0x160U,0.0f);
    lf32(cam+0x164U,0.0f);
    assert(read_world_cache_camera_lights(
        light_read,cam_global,radial_lights,
        cam_ambient,cam_attenuation));
    assert(radial_lights.size()==1U);
    assert(radial_lights[0].radius==4.0f &&
           radial_lights[0].intensity==0.8f);
    assert(cam_attenuation==0.20f);
    assert(world_cache_use_pixel_lights(true,true));
    assert(!world_cache_use_pixel_lights(false,true));
    assert(!world_cache_use_pixel_lights(true,false));
    assert(!world_cache_use_pixel_lights(false,false));
    // Exact attenuation/radius/incidence cases now exercise the actual
    // shared GLSL source through tests/gpu_pixel_lighting.py; do not
    // duplicate an independently maintained C++ lighting formula.
    std::vector<std::array<float,3>> rgb(mesh.vertices.size(),{{1,1,1}});
    world_cache_draw_candidate lit_face{2U,mesh.surfaces[0].first_index,
        mesh.surfaces[0].count,mesh.surfaces[0].material,3};
    world_cache_light_sample lit_sample{};
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,
        dynamic_lights,rgb,&lit_sample));
    assert(lit_sample.mode==3U && lit_sample.vertices==4U);
    assert(std::fabs(rgb[0][0]-std::pow(0.20f,1.8f))<0.0001f);
    assert(std::fabs(rgb[3][2]-std::pow(0.45f,1.8f))<0.0001f);
    // Each surface uses its owning sector tint, independent of the camera.
    std::vector<std::array<float,3>> face_tint(mesh.vertices.size());
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,
        dynamic_lights,rgb,nullptr,nullptr,false,0.0f,&face_tint));
    assert(face_tint[0][0]==0.60f && face_tint[3][1]==0.15f);
    constexpr uintptr_t other_sector=light_base+0x900U;
    lu32(light_surfaces+7U*0x5cU+0x08U,uint32_t(other_sector));
    lu32(light_surfaces+7U*0x5cU+0x20U,0U);
    lf32(other_sector+0x10U,0.0f);
    lf32(other_sector+0x14U,0.0f);
    lf32(other_sector+0x18U,0.90f);
    world_cache_draw_candidate other_face{7U,
        mesh.surfaces[1].first_index,mesh.surfaces[1].count,
        mesh.surfaces[1].material,0};
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,other_face,8U,
        dynamic_lights,rgb,nullptr,nullptr,false,0.0f,&face_tint));
    assert(face_tint[4][2]==0.90f && face_tint[4][0]==0.0f);
    assert(face_tint[0][0]==0.60f); // adjacent sector unchanged
    // The tint is live (COG SetSectorTint), not baked into the VBO.
    lf32(light_sector+0x10U,0.25f);
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,
        dynamic_lights,rgb,nullptr,nullptr,false,0.0f,&face_tint));
    assert(face_tint[0][0]==0.25f && face_tint[4][2]==0.90f);
    // Sector Ambient is only for Things; changing it must not touch faces.
    lf32(light_sector+0x04U,0.98f);
    std::vector<std::array<float,3>> pixel_state(mesh.vertices.size());
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,
        dynamic_lights,rgb,nullptr,&pixel_state,true));
    assert(std::fabs(rgb[0][0]-std::pow(0.20f,1.8f))<0.0001f);
    assert(std::fabs(rgb[3][2]-std::pow(0.40f,1.8f))<0.0001f);
    assert(pixel_state[3][0]>0.39f && pixel_state[3][0]<0.41f);
    assert(pixel_state[0][1]==0.20f && pixel_state[3][2]==3.0f);
    // Gouraud levels are authored independently for each world surface.
    // Changing a single local intensity must affect its own vertex only.
    lf32(light_intensities+2U*4U,0.85f);
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,
        dynamic_lights,rgb,nullptr,&pixel_state,true));
    assert(std::fabs(pixel_state[2][0]-0.95f)<0.00001f);
    assert(std::fabs(pixel_state[1][0]-0.20f)<0.00001f);
    lf32(light_intensities+2U*4U,0.20f);
    // Sector Extra must update live, including the fragment floor.
    lf32(light_sector+0x08U,0.55f);
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,
        dynamic_lights,rgb,nullptr,&pixel_state,true));
    assert(std::fabs(rgb[0][0]-std::pow(0.55f,1.8f))<0.0001f);
    assert(pixel_state[3][1]==0.55f);
    lf32(light_sector+0x08U,0.20f);
    // Global Extra Light raises the sector minimum, independent of
    // sector Ambient. Surface Extra is live and additive before the floor.
    lf32(light_surfaces+2U*0x5cU+0x44U,0.30f);
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,
        dynamic_lights,rgb,nullptr,&pixel_state,true,0.10f));
    assert(std::fabs(pixel_state[0][1]-0.30f)<0.00001f);
    assert(std::fabs(pixel_state[0][0]-0.30f)<0.00001f);
    assert(std::fabs(rgb[0][0]-std::pow(0.30f,1.8f))<0.0001f);
    lf32(light_surfaces+2U*0x5cU+0x44U,0.10f);
    lu32(light_surfaces+2U*0x5cU+0x20U,0U);
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,dynamic_lights,rgb));
    assert(rgb[0][0]==1.0f && rgb[3][2]==1.0f);
    lu32(light_surfaces+2U*0x5cU+0x20U,1U);
    assert(sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,dynamic_lights,rgb));
    assert(std::fabs(rgb[0][0]-std::pow(0.20f,1.8f))<0.0001f);
    lu32(light_surfaces+2U*0x5cU+0x20U,3U);
    lu32(light_surfaces+2U*0x5cU+0x2cU,0U);
    assert(!sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,dynamic_lights,rgb));
    lu32(light_surfaces+2U*0x5cU+0x2cU,uint32_t(light_indices));
    lu32(light_indices,99U);
    assert(!sample_world_cache_surface_light(
        light_read,light_surfaces,mesh,lit_face,8U,dynamic_lights,rgb));
    assert(!read_world_cache_dynamic_lights(
        light_read,light_world,9U,dynamic_lights));

    // Cache sky geometry while keeping it out of ordinary world draws.
    auto sky_world=world;
    sky_world.receivers[0].surface_flags=0x200U;
    sky_world.receivers[1].surface_flags=0x400U;
    world_cache_mesh sky_mesh{};
    assert(build_world_cache_mesh(sky_world,sky_mesh));
    assert(sky_mesh.sky_surfaces==2U);
    assert(sky_mesh.surfaces[0].surface_flags==0x200U);
    assert(sky_mesh.surfaces[1].surface_flags==0x400U);
    assert(sky_mesh.indices.size()==12U);

    // A snapshot is immutable, but COG-adjusted GeoMode/cel are read live.
    constexpr uintptr_t base=0x100000U;
    std::array<uint8_t,0x10000U> storage{};
    auto set_u32=[&](uint32_t index,size_t offset,uint32_t value) {
        std::memcpy(storage.data()+size_t(index)*0x5cU+offset,
                    &value,sizeof(value));
    };
    set_u32(2,0x1c,4U); // visible
    set_u32(2,0x34,uint32_t(world.receivers[0].material));
    set_u32(2,0x38,3U);
    // Native rdFace.texVertOffset is +0x3c/+0x40 in pixel units.
    float const scroll_x=8.0f,scroll_y=-4.0f;
    std::memcpy(storage.data()+2U*0x5cU+0x3cU,&scroll_x,4U);
    std::memcpy(storage.data()+2U*0x5cU+0x40U,&scroll_y,4U);
    set_u32(7,0x1c,0U); // hidden by COG
    set_u32(7,0x34,uint32_t(world.receivers[1].material));
    set_u32(7,0x38,3U);
    decal_native_read read=[&](uintptr_t addr,void *dst,size_t n) {
        if(addr<base || addr-base>storage.size() ||
           n>storage.size()-(addr-base))return false;
        std::memcpy(dst,storage.data()+(addr-base),n);
        return true;
    };
    auto state=inspect_world_cache_live_state(read,base,mesh);
    assert(state.drawn_candidates==1U && state.hidden==1U &&
           state.missing_uv==0U && state.unreadable==0U);

    set_u32(7,0x1c,4U); // now visible, but missing UV remains native
    set_u32(2,0x38,4U); // animated MAT cel
    set_u32(2,0x34,0x20000U); // changed native material
    state=inspect_world_cache_live_state(read,base,mesh);
    assert(state.drawn_candidates==1U && state.hidden==0U);
    assert(state.missing_uv==1U && state.changed_cel==1U &&
           state.changed_material==1U);
    state=inspect_world_cache_live_state(
        decal_native_read{},base,mesh);
    assert(state.unreadable==2U);

    // OpenJKDF2 sector-list membership: validate entire pointers and
    // contiguous SithSurface spans before filtering any candidate.
    auto put32=[&](uintptr_t addr,uint32_t value) {
        assert(addr>=base && addr-base+4U<=storage.size());
        std::memcpy(storage.data()+(addr-base),&value,4U);
    };
    constexpr uintptr_t native_world=base+0x1000U;
    constexpr uintptr_t sector_table=base+0x2000U;
    constexpr uintptr_t list_addr=base+0x8000U;
    constexpr uintptr_t count_addr=base+0xa000U;
    put32(native_world+0x4cU,2U);
    put32(native_world+0x50U,uint32_t(sector_table));
    put32(sector_table+0x24U,1U);
    put32(sector_table+0x28U,uint32_t(base+2U*0x5cU));
    put32(sector_table+0x98U+0x24U,1U);
    put32(sector_table+0x98U+0x28U,uint32_t(base+7U*0x5cU));
    put32(count_addr,1U);
    put32(list_addr,uint32_t(sector_table));
    decal_world_geometry snapshot{};
    snapshot.valid_world=true;
    snapshot.world=native_world;
    snapshot.surface_count=9U;
    snapshot.surfaces_addr=base;
    std::vector<uint8_t> membership;
    std::vector<world_cache_draw_candidate> possible{{
        2U,0U,6U,0x10000U,0
    },{
        7U,6U,6U,0x10001U,0
    }};
    auto portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(portal.valid && portal.visible_sector_count==1U);
    assert(portal.member_surfaces==1U && portal.matched_candidates==1U);
    assert(portal.rejected_candidates==1U);
    assert(membership.size()==9U && membership[2U] && !membership[7U]);
    // A stale/corrupt visible-sector pointer must fail closed, not return
    // a partial membership mask.
    put32(list_addr,uint32_t(sector_table+1U));
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(!portal.valid && membership.empty());
    put32(list_addr,uint32_t(sector_table+0x98U));
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(portal.valid && portal.matched_candidates==1U &&
           membership[7U] && !membership[2U]);

    // The patched JKGM limit is 7000, not vanilla's 128 sectors.
    put32(native_world+0x4cU,129U);
    put32(count_addr,129U);
    for(uint32_t i=0;i<129U;++i)
        put32(list_addr+uintptr_t(i)*4U,
              uint32_t(sector_table+uintptr_t(i)*0x98U));
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(portal.valid && portal.visible_sector_count==129U);
    assert(portal.member_surfaces==2U && portal.matched_candidates==2U);
    put32(native_world+0x4cU,7002U);
    put32(count_addr,7001U);
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,membership);
    assert(!portal.valid && membership.empty());
    assert(std::strcmp(portal.reason,"count_or_table_out_of_range")==0);


    // Sector-driven staging must agree with the whole-world collector
    // after reading live GeoMode, material, and cel state.
    put32(native_world+0x4cU,2U);
    put32(count_addr,1U);
    put32(list_addr,uint32_t(sector_table));
    std::vector<world_cache_sector_span> spans;
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,
        membership,&spans);
    assert(portal.valid && spans.size()==1U);
    assert(spans[0].first_surface==2U && spans[0].surface_count==1U);
    // Callers can retain storage for the native list across frames.
    std::vector<uint32_t> visible_scratch;
    portal=inspect_world_cache_portal_membership(
        read,native_world,snapshot,possible,count_addr,list_addr,
        membership,&spans,&visible_scratch);
    assert(portal.valid && visible_scratch.size()==1U &&
           visible_scratch[0]==sector_table && spans.size()==1U);
    std::vector<uint32_t> lookup,epochs;
    assert(build_world_cache_surface_lookup(
        mesh,snapshot.surface_count,lookup));
    assert(lookup.size()==9U && lookup[2]==0U && lookup[7]==1U &&
           lookup[0]==std::numeric_limits<uint32_t>::max());
    epochs.assign(lookup.size(),0U);
    uint32_t epoch=0;
    std::vector<world_cache_draw_candidate> full,sector;
    auto full_stats=collect_world_cache_draw_candidates(read,base,mesh,full);
    assert(full_stats.candidates==1U && full[0].surface_index==2U);
    assert(full[0].uv_offset[0]==8.0f && full[0].uv_offset[1]==-4.0f);
    // Per-face offset repeats on every vertex of that single polygon;
    // neighboring surfaces remain unchanged.
    std::vector<std::array<float,2>> frame_uv(mesh.vertices.size());
    assert(write_world_cache_uv_animation(mesh,lookup,full[0],frame_uv));
    for(size_t i=0;i<4U;++i)
        assert(frame_uv[i][0]==8.0f && frame_uv[i][1]==-4.0f);
    for(size_t i=4U;i<frame_uv.size();++i)
        assert(frame_uv[i][0]==0.0f && frame_uv[i][1]==0.0f);
    // MaterialAnim follows rdMaterial.curCelNum only for cel=-1.
    // SurfaceAnim / explicit SetSurfaceCel may override the face cel.
    set_u32(2,0x38,0xffffffffU);
    collect_world_cache_draw_candidates(read,base,mesh,full);
    assert(full.size()==1U && full[0].cel==-1);
    set_u32(2,0x38,4U);
    // A subsequent native SlideSurface tick can reverse scroll direction.
    float const next_x=-16.0f,next_y=2.0f;
    std::memcpy(storage.data()+2U*0x5cU+0x3cU,&next_x,4U);
    std::memcpy(storage.data()+2U*0x5cU+0x40U,&next_y,4U);
    collect_world_cache_draw_candidates(read,base,mesh,full);
    assert(full[0].cel==4 && full[0].uv_offset[0]==-16.0f &&
           full[0].uv_offset[1]==2.0f);
    assert(write_world_cache_uv_animation(mesh,lookup,full[0],frame_uv));
    assert(frame_uv[0][0]==-16.0f && frame_uv[3][1]==2.0f);
    assert(mesh.vertices[0].uv[0]==0.0f &&
           mesh.vertices[2].uv[0]==64.0f); // authored UVs immutable
    auto stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector);
    assert(stage.valid && stage.faces.candidates==1U &&
           stage.faces.triangles==2U && stage.unique_surfaces==1U);
    assert(sector[0].material==0x20000U && sector[0].cel==4);
    assert(sector[0].uv_offset[0]==-16.0f &&
           sector[0].uv_offset[1]==2.0f);
    auto parity=compare_world_cache_candidate_sets(full,membership,sector);
    assert(parity.exact && parity.matched==1U);
    // Visiting the same sector twice must not duplicate draw ranges.
    spans.push_back(spans[0]);
    stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector);
    assert(stage.valid && stage.faces.candidates==1U &&
           stage.repeated_surfaces==1U);
    // COG can hide faces without forcing a re-upload of the VBO.
    set_u32(2,0x1c,0U);
    collect_world_cache_draw_candidates(read,base,mesh,full);
    stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector);
    parity=compare_world_cache_candidate_sets(full,membership,sector);
    assert(parity.exact && stage.faces.candidates==0U &&
           stage.faces.hidden==1U);
    set_u32(2,0x1c,4U);
    // A native translucent face must not enter the ordinary opaque draw
    // candidates just because its MAT has no translucent pixels.
    std::vector<world_cache_draw_candidate> special;
    set_u32(2,0x18,0x2U); // rdFace.type: RD_FF_TEX_TRANSLUCENT
    auto alpha_stats=collect_world_cache_draw_candidates(
        read,base,mesh,full);
    assert(alpha_stats.candidates==0U &&
           alpha_stats.translucent_face==1U &&
           alpha_stats.special_candidates==1U);
    stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector,
        nullptr,nullptr,&special);
    assert(stage.valid && sector.empty() &&
           special.size()==1U && special[0].face_type==0x2U &&
           stage.faces.translucent_face==1U);
    parity=compare_world_cache_candidate_sets(full,membership,sector);
    assert(parity.exact);
    // The 0x20 face flag is not confirmed for JK. Unlike the 0x2
    // translucency flag, it must not change our draw classification.
    set_u32(2,0x18,0x20U);
    stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector,
        nullptr,nullptr,&special);
    assert(stage.valid && sector.size()==1U && special.empty());
    // 0x4 is likewise not confirmed to affect rendering in JK.
    set_u32(2,0x18,0x4U);
    stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector,
        nullptr,nullptr,&special);
    assert(stage.valid && sector.size()==1U && special.empty());
    // Opaque-textured, impassable adjoins also need separate validation.
    set_u32(2,0x18,0U);
    // Live native pointer/flags, not snapshot metadata, classify adjoins.
    set_u32(2,0x0c,uint32_t(base+0xf000U));
    put32(base+0xf000U,0x5U); // solid, rendered adjoin
    auto special_mesh=mesh;
    special_mesh.surfaces[0].solid_adjoin=true;
    stage=collect_world_cache_sector_candidates(
        read,base,special_mesh,lookup,spans,epochs,epoch,sector,
        nullptr,nullptr,&special);
    assert(stage.valid && sector.empty() &&
           special.size()==1U && special[0].solid_adjoin);
    // Turning on passability keeps this face renderable as an opaque
    // portal candidate, without changing cached geometry.
    put32(base+0xf000U,0x7U);
    stage=collect_world_cache_sector_candidates(
        read,base,special_mesh,lookup,spans,epochs,epoch,sector,
        nullptr,nullptr,&special);
    assert(stage.valid && stage.faces.passable_adjoin==1U &&
           sector.size()==1U && sector[0].passable_adjoin &&
           !sector[0].solid_adjoin && special.empty());
    // Cached MAT policy must not confuse movement passability with alpha.
    world_cache_material_info opaque_portal_mat{};
    opaque_portal_mat.albedo_source=1;
    opaque_portal_mat.has_image=true;
    opaque_portal_mat.width=64.0f;opaque_portal_mat.height=64.0f;
    assert(choose_world_cache_draw_policy(sector[0],opaque_portal_mat)==
           world_cache_draw_policy::textured);
    opaque_portal_mat.alpha=true;
    assert(choose_world_cache_draw_policy(sector[0],opaque_portal_mat)==
           world_cache_draw_policy::mixed);
    assert(world_cache_draws_in_pass(world_cache_draw_policy::mixed,
                                    world_cache_pass::opaque));
    assert(world_cache_draws_in_pass(world_cache_draw_policy::mixed,
                                    world_cache_pass::transparent));
    assert(world_cache_alpha_mode(world_cache_draw_policy::mixed)==1);
    opaque_portal_mat.alpha=false;
    opaque_portal_mat.masked=true;
    assert(choose_world_cache_draw_policy(sector[0],opaque_portal_mat)==
           world_cache_draw_policy::masked);
    assert(!world_cache_draws_in_pass(world_cache_draw_policy::masked,
                                     world_cache_pass::transparent));
    opaque_portal_mat.masked=false;
    opaque_portal_mat.dithered=true;
    assert(choose_world_cache_draw_policy(sector[0],opaque_portal_mat)==
           world_cache_draw_policy::dithered);
    opaque_portal_mat.dithered=false;
    opaque_portal_mat.opacity=0.5f;
    assert(choose_world_cache_draw_policy(sector[0],opaque_portal_mat)==
           world_cache_draw_policy::blended);
    assert(!world_cache_draws_in_pass(world_cache_draw_policy::blended,
                                     world_cache_pass::opaque));
    assert(world_cache_draws_in_pass(world_cache_draw_policy::blended,
                                    world_cache_pass::transparent));
    opaque_portal_mat.opacity=1.0f;
    // JK's translucent *face* flag forces alpha even with opaque MATs.
    set_u32(2,0x18,0x2U);
    stage=collect_world_cache_sector_candidates(
        read,base,special_mesh,lookup,spans,epochs,epoch,sector,
        nullptr,nullptr,&special);
    assert(stage.valid && sector.empty() &&
           special.size()==1U && special[0].passable_adjoin);
    assert(choose_world_cache_draw_policy(
        special[0],opaque_portal_mat)==world_cache_draw_policy::blended);
    set_u32(2,0x18,0U);
    set_u32(2,0x0c,0U);

    // Conservative solid-adjoin material policy: binary alpha masks
    // write precise G-buffer depth, mixed/translucent faces stay native.
    world_cache_draw_candidate grate{};
    grate.geometry_mode=4U;
    grate.solid_adjoin=true;
    world_cache_material_info grate_mat{};
    grate_mat.albedo_source=1;
    grate_mat.has_image=true;
    grate_mat.width=64.0f;
    grate_mat.height=64.0f;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::textured);
    grate_mat.alpha=true;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::mixed);
    grate_mat.masked=true;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::masked);
    grate.face_type=0x2U;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::blended);
    grate.face_type=0U;
    grate_mat.transition=0.25f;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::native);
    grate_mat.transition=0.0f;
    grate_mat.alpha_cutoff=0.0f;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::native);
    grate_mat.alpha_cutoff=0.5f;
    grate_mat.special=true;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::native); // unsupported unknown special
    grate_mat.blend_mode=1;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::blended); // additive
    grate_mat.blend_mode=2;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::blended); // multiply
    grate_mat.blend_mode=3;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::native); // fluid needs scene depth
    grate_mat.special=false;
    grate_mat.opacity=0.5f;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::blended);
    grate_mat.opacity=1.0f;
    grate.solid_adjoin=false;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::masked); // masked world now general
    grate.solid_adjoin=true;
    grate.geometry_mode=3U;
    grate_mat.albedo_source=2;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::solid);
    grate.geometry_mode=4U;
    grate_mat.albedo_source=0;
    assert(choose_world_cache_draw_policy(grate,grate_mat)==
           world_cache_draw_policy::native);
    // Geometry-stage special routing survives: only material policy
    // decides whether a solid-adjoin candidate is submitted to GL.
    // Fail-closed invalid spans clear both candidate lists.
    spans.push_back({9U,1U});
    stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector,
        nullptr,nullptr,&special);
    assert(!stage.valid && sector.empty() && special.empty());
    spans.pop_back();
    set_u32(2,0x18,0U);
    // Epoch wrap must not mistake old visits for current ones.
    epoch=std::numeric_limits<uint32_t>::max();
    stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector);
    assert(stage.valid && epoch==1U && stage.faces.candidates==1U);
    // A malformed sector span or broken lookup must fail without output.
    spans.push_back({9U,1U});
    stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector);
    assert(!stage.valid && sector.empty());
    spans.pop_back();
    lookup[2]=99U;
    stage=collect_world_cache_sector_candidates(
        read,base,mesh,lookup,spans,epochs,epoch,sector);
    assert(!stage.valid && sector.empty());
    // Duplicate native indices in the immutable mesh cannot be mapped.
    auto duplicate=mesh;
    duplicate.surfaces.push_back(mesh.surfaces.front());
    assert(!build_world_cache_surface_lookup(
        duplicate,snapshot.surface_count,lookup) && lookup.empty());

    // GeoMode 3 retains native MAT/CMP solid RGB even without UVs.
    // The same UV-less surface remains native if changed back to Mode 4.
    auto solid_mesh=mesh;
    solid_mesh.surfaces[1].solid_adjoin=false;
    assert(!solid_mesh.surfaces[1].has_uv);
    assert(build_world_cache_surface_lookup(
        solid_mesh,snapshot.surface_count,lookup));
    set_u32(7,0x1c,3U);
    set_u32(7,0x18,0U);
    auto solid_stats=collect_world_cache_draw_candidates(
        read,base,solid_mesh,full);
    assert(solid_stats.candidates==2U && solid_stats.solid_color==1U &&
           solid_stats.missing_uv==0U);
    assert(full.size()==2U && full[0].geometry_mode==4U &&
           full[1].surface_index==7U && full[1].geometry_mode==3U);
    std::vector<world_cache_sector_span> solid_spans{{7U,1U}};
    auto solid_stage=collect_world_cache_sector_candidates(
        read,base,solid_mesh,lookup,solid_spans,epochs,epoch,sector);
    assert(solid_stage.valid && solid_stage.faces.solid_color==1U &&
           sector.size()==1U && sector[0].geometry_mode==3U);
    std::vector<uint8_t> solid_membership(snapshot.surface_count,0U);
    solid_membership[7U]=1U;
    assert(compare_world_cache_candidate_sets(
        full,solid_membership,sector).exact);
    // UV offsets are irrelevant to GeoMode 3, even if no UV storage
    // exists and the stale native offset contains nonfinite values.
    float const invalid_uv=std::numeric_limits<float>::quiet_NaN();
    std::memcpy(storage.data()+7U*0x5cU+0x3cU,&invalid_uv,4U);
    solid_stats=collect_world_cache_draw_candidates(read,base,solid_mesh,full);
    assert(solid_stats.candidates==2U && solid_stats.unreadable==0U &&
           full[1].uv_offset[0]==0.0f && full[1].uv_offset[1]==0.0f);
    std::memset(storage.data()+7U*0x5cU+0x3cU,0,8U);
    // GeoMode is live metadata: COG switches do not rebuild the VBO.
    set_u32(7,0x1c,4U);
    auto textured_stats=collect_world_cache_draw_candidates(
        read,base,solid_mesh,full);
    assert(textured_stats.candidates==1U &&
           textured_stats.missing_uv==1U);
    set_u32(7,0x1c,2U);
    auto wire_stats=collect_world_cache_draw_candidates(
        read,base,solid_mesh,full);
    assert(wire_stats.candidates==1U && wire_stats.not_textured==1U);
    set_u32(7,0x1c,3U);
    set_u32(7,0x18,0x2U);
    std::vector<world_cache_draw_candidate> solid_special;
    solid_stage=collect_world_cache_sector_candidates(
        read,base,solid_mesh,lookup,solid_spans,epochs,epoch,sector,
        nullptr,nullptr,&solid_special);
    assert(solid_stage.valid && sector.empty() &&
           solid_special.size()==1U &&
           solid_special[0].geometry_mode==3U);
    set_u32(7,0x18,0U);

    // Malformed/nonfinite geometry may not poison persistent GL buffers.
    world.receivers[0].polygon[0][0]=std::numeric_limits<float>::infinity();
    assert(!build_world_cache_mesh(world,mesh));
    assert(mesh.indices.empty());
    world.receivers[0].polygon[0][0]=0.0f;
    world.receivers[0].shadow_uv[0][0]=
        std::numeric_limits<float>::quiet_NaN();
    assert(build_world_cache_mesh(world,mesh));
    assert(mesh.uv_ready_surfaces==0U && mesh.missing_uv_surfaces==2U);

    // Exercise the actual native-memory loader, not just a manually
    // constructed mesh: the render cache includes sky even without a MAT,
    // while the decal/shadow reader MUST continue excluding it by default.
    constexpr uintptr_t sky_world_addr=base+0xb000U;
    constexpr uintptr_t sky_vertices_addr=base+0xc000U;
    constexpr uintptr_t sky_faces_addr=base+0xd000U;
    constexpr uintptr_t sky_indices_addr=base+0xe000U;
    put32(sky_world_addr+0x88U,4U);
    put32(sky_world_addr+0x8cU,uint32_t(sky_vertices_addr));
    put32(sky_world_addr+0xa0U,0U);
    put32(sky_world_addr+0xa4U,0U);
    put32(sky_world_addr+0xa8U,1U);
    put32(sky_world_addr+0xacU,uint32_t(sky_faces_addr));
    put32(sky_faces_addr+0x0cU,0U); // no adjoin
    put32(sky_faces_addr+0x10U,0x200U);
    put32(sky_faces_addr+0x1cU,4U);
    put32(sky_faces_addr+0x28U,4U);
    put32(sky_faces_addr+0x2cU,uint32_t(sky_indices_addr));
    put32(sky_faces_addr+0x34U,0U); // sky needn't have a MAT
    const float sky_quad[12]={0,0,0, 2,0,0, 2,0,2, 0,0,2};
    const float sky_normal[3]={0,-1,0};
    std::memcpy(storage.data()+(sky_vertices_addr-base),sky_quad,
                sizeof(sky_quad));
    std::memcpy(storage.data()+(sky_faces_addr-base)+0x48U,sky_normal,
                sizeof(sky_normal));
    for(uint32_t i=0;i<4U;++i)
        put32(sky_indices_addr+i*4U,i);
    decal_world_geometry sky_native{};
    assert(sky_native.load(read,sky_world_addr,100U,true));
    assert(sky_native.skipped_sky==1U &&
           sky_native.receivers.empty());
    assert(sky_native.load(read,sky_world_addr,100U,true,true));
    assert(sky_native.cached_sky==1U && sky_native.skipped_sky==0U);
    assert(sky_native.receivers.size()==1U &&
           sky_native.receivers[0].surface_flags==0x200U);
    world_cache_mesh cached_sky{};
    assert(build_world_cache_mesh(sky_native,cached_sky));
    assert(cached_sky.sky_surfaces==1U &&
           cached_sky.surfaces[0].surface_flags==0x200U);
    std::vector<world_cache_draw_candidate> no_sky_draw;
    auto const sky_stats=collect_world_cache_draw_candidates(
        read,sky_native.surfaces_addr,cached_sky,no_sky_draw);
    assert(sky_stats.sky==1U && sky_stats.candidates==0U &&
           no_sky_draw.empty());
    // Sky draws are staged separately, even without native UV geometry.
    // The same visible sector / material / cel metadata drives this path.
    world_cache_frame_stats sky_frame{};
    std::vector<world_cache_draw_candidate> sky_draws;
    // The prior loader test deliberately used a sky without a MAT.
    // A draw candidate, unlike the geometry snapshot, needs a live MAT.
    put32(sky_faces_addr+0x34U,uint32_t(base+0x1880U));
    append_world_cache_candidate(read,sky_native.surfaces_addr,
        cached_sky.surfaces[0],no_sky_draw,sky_frame,
        nullptr,nullptr,nullptr,&sky_draws);
    assert(no_sky_draw.empty() && sky_draws.size()==1U &&
           sky_draws[0].sky_flags==0x200U &&
           sky_frame.sky_horizon==1U && sky_frame.sky_ceiling==0U);
    assert(world_cache_sky_type(0x200U)==world_cache_sky_kind::horizon);
    assert(world_cache_sky_type(0x400U)==world_cache_sky_kind::ceiling);
    assert(world_cache_sky_type(0x600U)==world_cache_sky_kind::horizon);
    assert(world_cache_sky_type(0U)==world_cache_sky_kind::none);
    // Each field is verified by the actual JKGM executable's JKL loader,
    // not assumed from the OpenJKDF2 SithWorld declaration.
    static_assert(world_cache_sky_fields_offset==0xDCU);
    static_assert(world_cache_sky_fields_bytes==0x1cU);
    float const native_sky_fields[7]{
        100.0f,50.0f,768.0f,4.0f,5.0f,10.0f,11.0f};
    std::memcpy(storage.data()+(sky_world_addr-base)+0xdcU,
                native_sky_fields,sizeof(native_sky_fields));
    world_cache_sky_info sky_info{};
    assert(read_world_cache_sky_info(read,sky_world_addr,sky_info));
    assert(sky_info.horizon_valid && sky_info.ceiling_valid);
    assert(sky_info.horizon_distance==50.0f &&
           sky_info.ceiling_height==100.0f &&
           sky_info.pixels_per_revolution==768.0f &&
           sky_info.ceiling_offset[1]==11.0f);
    float invalid_sky_fields[7]{
        std::numeric_limits<float>::quiet_NaN(),
        -1.0f,0.0f,0.0f,0.0f,0.0f,0.0f};
    std::memcpy(storage.data()+(sky_world_addr-base)+0xdcU,
                invalid_sky_fields,sizeof(invalid_sky_fields));
    assert(!read_world_cache_sky_info(read,sky_world_addr,sky_info));
    assert(sky_info.horizon_valid && sky_info.ceiling_valid);
    engine_camera sky_camera{};
    sky_camera.valid=true;
    std::array<float,3> sky_pyr{};
    assert(world_cache_sky_angles(sky_camera,sky_pyr));
    for(float angle:sky_pyr)assert(std::fabs(angle)<0.0001f);
    sky_camera.valid=false;
    assert(!world_cache_sky_angles(sky_camera,sky_pyr));
    put32(sky_faces_addr+0x10U,0x400U);
    assert(sky_native.load(read,sky_world_addr,100U,true,true));
    assert(sky_native.cached_sky==1U &&
           sky_native.receivers[0].surface_flags==0x400U);
    sky_frame={};
    sky_draws.clear();
    append_world_cache_candidate(read,sky_native.surfaces_addr,
        cached_sky.surfaces[0],no_sky_draw,sky_frame,
        nullptr,nullptr,nullptr,&sky_draws);
    assert(sky_draws.size()==1U &&
           sky_draws[0].sky_flags==0x400U &&
           sky_frame.sky_ceiling==1U && sky_frame.sky_horizon==0U);
    put32(sky_faces_addr+0x10U,0x600U);
    sky_frame={};
    sky_draws.clear();
    append_world_cache_candidate(read,sky_native.surfaces_addr,
        cached_sky.surfaces[0],no_sky_draw,sky_frame,
        nullptr,nullptr,nullptr,&sky_draws);
    assert(sky_draws.size()==1U && sky_draws[0].sky_flags==0x600U &&
           sky_frame.sky_horizon==1U && sky_frame.sky_ceiling==0U);
    return 0;
}
