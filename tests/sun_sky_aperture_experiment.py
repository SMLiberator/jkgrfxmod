"""Regression guards: opt-in sunlight only after sky polygon intersection."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
def read(path):return (r/path).read_text(encoding="utf-8")
cfg=read("common/config.hpp")
parser=read("common/config.cpp")
h=read("renderer/sun_sky_aperture_gpu.hpp")
render=read("renderer/renderer.cpp")
sh=read("renderer/shaders.hpp")
sc=read("renderer/shaders.cpp")
opaque=read("assets/shaders/post_opaque_composite.frag")
shared=read("assets/shaders/translucent_sun_sampling.glsl")
assert "bool experimental_sun_sky_aperture = false;" in cfg
assert 'j.contains("experimental_sun_sky_aperture")' in parser
assert "**experimental_sun_sky_aperture**" in read("doc/config.md")
assert "GL_TEXTURE_2D_ARRAY" in h
assert "GL_DEPTH_COMPONENT24" in h
assert "GL_TEXTURE_COMPARE_MODE,GL_NONE" in h
assert "GL_TEXTURE_BORDER_COLOR,white" in h
assert "sun_shadow_same_projection(view,gate.views[cascade])" in render
assert "sky_aperture_depth_transform" in render
assert "gate.expanded_sky_view(shadow_view)" in render
assert "gate.signatures[cascade]==signature" in render
assert "gate.valid[cascade]=true;" in render
assert "world.load(read,native_world,250000U,true,true,true)" in h
assert "surface.surface_flags&0x600U" in h
assert "sun_sky_surface_eligible(flags,adjoin)" in render
assert "sun_sky_surface_eligible(flags,adjoin)" in render
assert "uint32_t flags=0,adjoin=0;" in render
# Sky eligibility is stable within a level: avoid scanning all native
# surfaces every frame, but re-audit on sun activation, world changes and
# periodically for rare COG sky/adjoin mutations.
assert "void refresh_sun_level_sky(bool force_audit)" in render
assert "refresh_sun_level_sky(!sun_level_sky_sun_active);" in render
assert "constexpr uint64_t audit_interval_frames=120U;" in render
assert "uintptr_t(world)==sun_level_sky_world" in render
assert "sun_level_sky_last_audit_frame=gpu_3do_present_serial;" in render
assert "else sun_level_sky_sun_active=false;" in render
assert "sun_level_sky_known=false;" in render
assert "get_sun_settings().intensity.value()[0]>0.0f" in render
assert "if(!the_config->enable_sun_shadows || !sun_level_has_sky)return;" in render
assert "sun_level_has_sky &&" in render
assert "flags&0x600U,adjoin" in h
assert "sun_sky_surface.hpp" in read("tests/sun_shadow.cpp")
assert "sun_sky_surface_eligible(0x200U,0U)" in read("tests/sun_shadow.cpp")
assert "sun_sky_surface_eligible(0x200U,0x123400U)" in read("tests/sun_shadow.cpp")
assert "void draw_sun_sky_aperture(int cascade," in render
assert "draw_sun_sky_aperture(cascade,view,safe_read);" in render
assert "sun_sky_aperture_ready()" in render
assert "glBindTexture(GL_TEXTURE_2D_ARRAY," in render
assert "texture_units::forward_next_extended_required_units" in render
assert "texture_units::forward_next_extended_unit" in render
assert "glActiveTexture(GL_TEXTURE0+19)" not in render
assert "#ifdef JKGM_SUN_SKY_APERTURE" in opaque
assert "#ifdef JKGM_SUN_SKY_APERTURE" in shared
assert "set_sun_sky_aperture" in sh and "set_sun_sky_aperture" in sc
assert "set_sky_aperture" in sh and "set_sky_aperture" in sc
assert "sun_sky_aperture_active==0)return 1.0;" in opaque
assert "float sun_sky_aperture(vec3 position) { return 1.0; }" in opaque
assert "float sky_access=sun_sky_aperture(world_position);" in opaque
assert "float direct_visibility=visibility*sky_access;" in opaque
assert "float sun_applied=incidence*sun_fade;" in opaque
assert "sprite ? 0.75*sun_fade" in opaque
assert "0.75*sun_fade*sky_access" not in opaque
assert "1.0-direct_visibility" in opaque
assert "sun_applied*direct_visibility" in opaque
assert "float sun_sky_aperture_sample(vec3 uv,int layer)" in opaque
assert "translucent_sun_sky_aperture_active==0)return 1.0;" in shared
assert "float translucent_sun_sky_aperture(vec3 world) { return 1.0; }" in shared
assert "float translucent_sun_sky_sample(vec3 uv,int layer)" in shared
world=read("assets/shaders/world_cache_direct_specular.glsl")
assert "#ifdef JKGM_SUN_SKY_APERTURE" in world
assert "uniform sampler2DArray translucent_sun_sky_aperture_map;" in world
assert "world_sun_sky_aperture(cached_world_position)" in world
assert "float world_sun_sky_aperture(vec3 world) { return 1.0; }" in world
for path in ("game_opaque_pass.frag","game_trns_pass.frag","gpu_3do_preview.frag"):
    frag=read("assets/shaders/"+path)
    assert "uniform sampler2DArray translucent_sun_sky_aperture_map;" in frag
    assert "#ifdef JKGM_SUN_SKY_APERTURE" in frag
    assert "fade*translucent_sun_sky_aperture(world)" in frag
for path in ("game_trns_pass.frag","gpu_3do_preview.frag"):
    frag=read("assets/shaders/"+path)
    assert "float sky_access=translucent_sun_sky_aperture(world);" in frag
    assert "if(sky_access<=0.0)return baked;" not in frag
    assert "float direct_visibility=visibility*sky_access;" in frag
    assert "(1.0-direct_visibility)" in frag
    assert "incidence*direct_visibility*opacity*range_fade;" in frag
# Aperture visibility returns to exactly one nearest depth comparison
# per pixel. Dilation belongs to the cached raster pass, not the receiver.
assert not (r/"assets/shaders/sun_sky_aperture_filter.glsl").exists()
for shader in (opaque,shared,world):
    assert "sun_sky_aperture_filtered_coverage" not in shader
    assert "sun_sky_aperture_filter.glsl" not in shader
    assert "sky_z+0.00001<receiver_z" in shader
    assert shader.count("sky_z=texture(")==1
# Multi-draw four shifted projections plus the unshifted sky polygons.
assert "constexpr float sky_expansion_texels=0.75f;" in render
assert "draw_sky_offset(0.0f,0.0f);" in render
assert "for(float dx:{-1.0f,1.0f})" in render
assert "for(float dy:{-1.0f,1.0f})" in render
assert "draw_sky_offset(dx,dy);" in render
assert "glMultiDrawElements(GL_TRIANGLES,sky_counts.data()," in render
assert "sun_shadow_target->far_resolution" in render
assert "gate.signatures[cascade]==signature" in render
assert "gl::clear_buffer_depth(1.0f);" in render

def aperture(sky,receiver):
    return sky<0.999999 and sky+0.00001<receiver
assert not aperture(1.0,0.9)
assert not aperture(0.8,0.5)
assert aperture(0.1,0.5)
assert not aperture(0.5,0.5)
for path in ("renderer/renderer.vcxproj","renderer/renderer.vcxproj.filters"):
    assert "sun_sky_aperture_gpu.hpp" in read(path)
print("Opt-in sunlight sky-aperture source contracts passed")

# A sky-blocked receiver should remain unlit by DIRECT sun, but its
# baked brightness is no longer protected from sun shadow contrast.
def shaded_baked(base,intensity,visibility,sky_access,baseline):
    direct_visibility=visibility*sky_access
    contrast=baseline*min(max(intensity,0.0),1.0)
    return base*(1.0-contrast*(1.0-direct_visibility))

for baseline in (0.6,0.75):
    for intensity in (0.0,0.001,0.5,1.0,2.0):
        assert shaded_baked(0.8,intensity,1.0,1.0,baseline)==0.8
        assert abs(shaded_baked(0.8,intensity,1.0,0.0,baseline) -
                   shaded_baked(0.8,intensity,0.0,1.0,baseline))<1e-9
        if intensity==0.0:
            assert shaded_baked(0.8,intensity,1.0,0.0,baseline)==0.8
        if intensity>=1.0:
            assert abs(shaded_baked(0.8,intensity,1.0,0.0,baseline) -
                       0.8*(1.0-baseline))<1e-9

# The atlas stores nearest depth, not interpolated depths.
gpu_resource=read("renderer/sun_sky_aperture_gpu.hpp")
assert "GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST" in gpu_resource
assert "GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST" in gpu_resource

# Phase 2 static-world pruning is separately opt-in and fail-open.
tiles=read("renderer/sun_sky_caster_coverage.hpp")
assert "struct sun_sky_caster_fast_mask" in tiles
assert "static constexpr int side=128;" in tiles
assert "static constexpr int padding_tiles=2;" in tiles
assert "sky_prune_mask.may_overlap(surface.polygon)" in render
assert "eligible_in_snapshot!=sun_level_sky_faces" in render
assert "snapshot_identity!=sun_level_sky_identity" in render
assert "identity!=sun_level_sky_identity" in render
assert "sun_sky_aperture_target->valid[0] &&" in render
assert "sun_sky_aperture_target->valid[1] &&" in render
assert "cascade_resolution>=256U" in render
assert "double const halo=double(padding_tiles)/double(side);" in tiles
assert "hash_u32(sky_prune_ready?1U:0U);" in render
assert "experimental_sun_sky_caster_pruning = false;" in cfg
assert 'j.contains("experimental_sun_sky_caster_pruning")' in parser
assert "SUN sky-prune cascade=" in render
# Phase 1 sky-first conservative occupancy is a measurement only: it
# MUST NOT alter caster selection, depth rendering, or cache signatures.
tiles=read("renderer/sun_sky_caster_coverage.hpp")
assert "static constexpr int grid_side=32;" in tiles
assert "static constexpr int max_grid_side=128;" in tiles
assert "std::vector<uint8_t> bbox_tiles" in tiles
assert "convex_hull(" in tiles
assert "hull_hits_tile(" in tiles
assert "overlap_result evaluate_polygon(" in tiles
assert "overlap_result evaluate_aabb(" in tiles
assert "int requested_side=grid_side" in tiles
assert "void include_polygon(" in tiles
assert "bool intersects_polygon(" in tiles
assert "bool intersects_aabb(" in tiles
assert "void mark_all()" in tiles
assert "int padding_tiles=1" in tiles
assert "bool fail_open=false;" in tiles
assert "bool experimental_sun_sky_caster_culling = false;" in cfg
assert 'j.contains("experimental_sun_sky_caster_culling")' in parser
assert "**experimental_sun_sky_caster_culling**" in read("doc/config.md")
assert '#include "sun_sky_caster_coverage.hpp"' in render
assert "sky_coverages[g].reset(view,side);" in render
assert "sky_coverages[g].include_polygon(face.polygon);" in render
assert "mask.evaluate_polygon(" in render
assert "mask.evaluate_aabb(" in render
assert "measure_models(static_models,0);" in render
assert "measure_models(models,1);" in render
assert "sky_world_candidates.push_back({&surface,range.count});" in render
assert "overlap_eval_cpu_us_3grids=" in render
assert "world_rect_large=" in render
assert "culling_applied=0" in render
assert "gate.world.receivers" in render
assert 'culling_applied=0' in render
assert "map.selected.push_back(map.mesh.surfaces[id]);" in render
assert "counts.push_back(static_cast<GLsizei>(range.count));" in render
assert "(stable?static_models:models).push_back(" in render
assert "sun_sky_caster_coverage.hpp" in read("renderer/renderer.vcxproj")
assert "sun_sky_caster_coverage.hpp" in read("renderer/renderer.vcxproj.filters")
assert '#include "renderer/sun_sky_caster_coverage.hpp"' in read("tests/sun_shadow.cpp")
