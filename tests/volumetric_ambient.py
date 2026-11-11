"""2x2x2 volumetric ambient contracts: CPU Batch A and opt-in GPU Batch B.

These checks do not replace an in-game visual comparison, but they ensure
the disabled production renderer remains unaffected and live ExtraLight
cannot leak into the permanently quantized field.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
header=(root/"renderer/volumetric_ambient.hpp").read_text(encoding="utf-8")
tests=(root/"tests/volumetric_ambient.cpp").read_text(encoding="utf-8")
wiring=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
assert "struct volumetric_ambient_sector {" in header
assert "std::array<uint8_t,8> light{};" in header
assert "float range=1.0f;" in header
assert "float const range=authored_light_limit(overbright);" in header
assert "for(unsigned corner=0;corner<8;++corner)" in header
assert "brightness+=weight*(float(light[corner])/255.0f)*range;" in header
assert "std::clamp((position[d]-lo[d])/span,0.0f,1.0f)" in header
assert "std::lround(" in header
assert "std::sort(sectors_.begin(),sectors_.end()" in header
assert "building_.clear(); // Startup-only source samples" in header
assert "bool capture_volumetric_ambient(" in header
assert "mesh.vertices[first+i].position" in header
assert "clamp_authored_light(intensity,overbright)" in header
assert "range.surface_flags&0x600U" in header
assert "range.passable_adjoin" in header
assert "geo<3U || mode<3U" in header
# The new field must capture ONLY the authored Gouraud vertices:
# sector AmbientLight is Thing-only and dynamic/face ExtraLight must not
# become a permanent part of the startup volume.
assert "read(face+0x44U" not in header
assert "read(sector+0x08U" not in header
assert "read(uintptr_t(sector)+0x08U" not in header
assert "read(sector+0x04U" not in header
assert "capture_world_vertex_ambient(" not in header
assert '#include "volumetric_ambient.hpp"' in wiring
assert "void stage_volumetric_ambient()" in wiring
assert "stage_volumetric_ambient();" in wiring
assert "the_config->experimental_volumetric_ambient" in wiring
assert "the_config->enable_sector_crossing_lighting ||" in wiring
assert "volumetric_ambient_world!=cache.loaded_world" in wiring
assert "volumetric_ambient_revision!=cache.revision" in wiring
assert "volumetric_ambient_overbright!=overbright" in wiring
assert "clamp_authored_extra_floor(" in wiring
assert "sector.native_sector+0x08U" in wiring
assert "v.samples=s.light;" in wiring
assert "prog.set_volumetric_ambient(" in wiring
assert "inst.volumetric_ambient_neighbor_weight);" in wiring
assert "experimental_world_vertex_ambient" in wiring  # retained for A/B
config=(root/"common/config.hpp").read_text(encoding="utf-8")
parser=(root/"common/config.cpp").read_text(encoding="utf-8")
sample=(root/"assets/jkgm.json").read_text(encoding="utf-8")
docs=(root/"doc/config.md").read_text(encoding="utf-8")
instance=(root/"renderer/gpu_3do_preview.hpp").read_text(encoding="utf-8")
vertex=(root/"assets/shaders/gpu_3do_preview.vert").read_text(encoding="utf-8")
api=(root/"renderer/shaders.cpp").read_text(encoding="utf-8")
assert "bool experimental_volumetric_ambient = false;" in config
assert 'j.contains("experimental_volumetric_ambient")' in parser
assert '"experimental_volumetric_ambient": false' in sample
assert "| **experimental_volumetric_ambient** |" in docs
assert "std::optional<gpu_3do_ambient_volume> volumetric_ambient;" in instance
assert "std::array<uint8_t,8> samples{};" in instance
assert "uniform int volumetric_ambient_enabled;" in vertex
assert "float volumetric_ambient_at(vec3 view_position)" in vertex
assert "volumetric_ambient_live_floor" in vertex
assert "volumetric_ambient_enabled!=0" in vertex
assert "max(raw_light+dynamic_added,ambient_floor)" in vertex
assert "prog.set_sector_crossing_camera_origin(cam.position);" in wiring
assert "gpu_3do_preview_shader::set_volumetric_ambient(" in api
assert "gl::set_uniform_integer(volumetric_ambient_enabled,volume?1:0);" in api
assert "float(volume->samples[i])/255.0f" in api
assert "it==sectors.end()" in wiring
# Regression: this flag must be checked in the native rdModel3 ownership
# hook, not only in the renderer. Without this branch every instance has
# owning_sector_valid=false and silently retains vanilla ambient.
native=(root/"renderer/main.cpp").read_text(encoding="utf-8")
assert ("if(the_config->enable_sector_crossing_lighting ||\n"
        "               the_config->experimental_world_vertex_ambient ||\n"
        "               the_config->experimental_volumetric_ambient ||\n"
        "               (the_config->experimental_continuous_ambient_cpu ||\n"
        "                the_config->experimental_continuous_ambient_gpu))") in native
assert "own_sector_id_valid=read(sector,&own_sector_id,4U);" in native
assert "inst.owning_sector_valid=active_native_model_sector_id_valid;" in native
assert "!inst.owning_sector_valid" in wiring
assert "inst.skybox_vertex_lighting || inst.world_lighting_exempt ||" in wiring
assert "mem.put(addr+0x44U,1.5f)" in tests
assert "assert(field.sectors()[0].range==1.0f);" in tests
assert "assert(field.sectors()[0].range==2.0f);" in tests
assert "assert(field.sectors()[0].id==7U && field.sectors()[1].id==8U);" in tests
assert "assert(!field.sample(9," in tests
assert "field.sample(7,{{0.25f,0.0f,0.0f}}" in tests
assert "stats.surfaces_seen==3U && stats.surfaces_sampled==2U" in tests
# The 8-corner GLSL trilinear math must match the CPU reference and
# respect a live sector floor independent of all 8 quantized bytes.
def cpu_sample(q, xyz, intensity_range, floor):
    raw=0.0
    for c in range(8):
        w=1.0
        for d in range(3):
            w*=xyz[d] if (c>>d)&1 else 1.0-xyz[d]
        raw+=w*(q[c]/255.0)*intensity_range
    return max(raw,floor)

def vertex_sample(q, xyz, intensity_range, floor):
    x,y,z=xyz
    a=[v/255.0 for v in q]
    mix=lambda a,b,t:a+(b-a)*t
    low=mix(mix(a[0],a[1],x),mix(a[2],a[3],x),y)
    high=mix(mix(a[4],a[5],x),mix(a[6],a[7],x),y)
    return max(mix(low,high,z)*intensity_range,floor)

for byte_values in ((0,255,0,255,0,255,0,255),
                    (0,50,100,150,200,225,240,255)):
    for xyz in ((0.,0.,0.),(1.,1.,1.),(.25,.5,.75),(.7,.1,.9)):
        for rng in (1.,2.):
            for floor in (0.,.5,1.):
                expected=cpu_sample(byte_values,xyz,rng,floor)
                gpu=vertex_sample(byte_values,xyz,rng,floor)
                assert abs(expected-gpu)<1e-6,(xyz,rng,floor)
assert "float low=mix(mix(bottom.x,bottom.y,t.x)," in vertex
assert "float high=mix(mix(top.x,top.y,t.x)," in vertex
assert "return max(mix(low,high,t.z)*quantized_range,live_floor);" in vertex

# Portal-aware blending is a symmetric floor-preserving mix of two
# independently evaluated sectors. Never combine quantized bytes before
# their live ExtraLight floors; doing that would break SetSectorLight fades.
for weight in (0.0,0.125,0.25,0.5):
    for xyz in ((0.,0.,0.),(.33,.45,.67),(1.,1.,1.)):
        own=vertex_sample((0,10,70,90,150,120,200,255),xyz,1.,.1)
        other=vertex_sample((255,200,210,175,80,30,50,0),xyz,1.,.55)
        combined=own*(1.-weight)+other*weight
        assert min(own,other)-1e-6<=combined<=max(own,other)+1e-6
        if weight==.5:
            reverse=other*(1.-weight)+own*weight
            assert abs(combined-reverse)<1e-6
assert "world_vertex_ambient_grid volumetric_ambient_portals;" in wiring
assert "capture_world_vertex_ambient_portals(" in wiring
assert "volumetric_ambient_portals.reset(1.0f);" in wiring
assert "volumetric_ambient_portals.reset();" in wiring
assert "volumetric_ambient_portals.nearest_portal(" in wiring
assert "inst.volumetric_ambient_neighbor_weight=weight;" in wiring
# Portal polygons are world-space; the rdMesh placement at transform[9..11]
# is camera-space AND node-dependent. Only SithThing +0xb8 can select
# the same neighboring portal for every animated mesh node.
assert "uintptr_t(hud_owner)+0xb8U" in native
assert "own_world_position_valid=read(" in native
assert "active_native_model_world_position=own_world_position;" in native
assert "active_native_model_world_position=previous_world_position;" in native
assert "inst.owning_thing_world_position=active_native_model_world_position;" in native
assert "inst.owning_thing_world_position_valid=" in native
assert "std::array<float,3> owning_thing_world_position{};" in instance
assert "bool owning_thing_world_position_valid=false;" in instance
assert "if(!inst.owning_thing_world_position_valid)" in wiring
assert "inst.owning_sector_id,inst.owning_thing_world_position," in wiring
portal_stage=wiring.split("void stage_volumetric_ambient()",1)[1].split(
    "void stage_sector_crossing()",1)[0]
assert "inst.transform[9]" not in portal_stage
assert "missing_thing_origin=" in portal_stage
assert "if(!get_floor(*other,neighbor_floor))" in wiring
assert "std::optional<gpu_3do_ambient_volume> volumetric_ambient_neighbor;" in instance
assert "uniform float volumetric_neighbor_weight;" in vertex
assert "if(volumetric_neighbor_weight<=0.0)return own;" in vertex
assert "return mix(own,neighbor,volumetric_neighbor_weight);" in vertex
assert "gl::set_uniform_float(volumetric_neighbor_weight,mix_weight);" in api
assert "assert(grid.nearest_portal(7," in (root/"tests/world_vertex_ambient.cpp").read_text(encoding="utf-8")
assert "assert(neighbor==0U && std::fabs(weight-0.5f)<0.00001f);" in (
    root/"tests/world_vertex_ambient.cpp").read_text(encoding="utf-8")

print("Volumetric ambient CPU/GPU/portal source contracts passed")
