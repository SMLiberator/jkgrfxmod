#pragma once
// Development-only native 3DO GPU bypass. Each immutable mesh is copied once,
// including UV seams and per-face material references. Transforms are dynamic.
#include "texture_units.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace jkgm {
    struct gpu_3do_vertex {
        float x,y,z;
        float u,v; // source MAT UVs, in original texture pixels
        float nx,ny,nz; // mesh-local face normal (G-buffer)
        float static_light; // immutable .3DO vertex intensity
        float lnx,lny,lnz; // mesh-local vertex normal for Gouraud illumination
        float fax,fay,faz; // face's first vertex: JK diffuse light anchor
        float face_extra; // .3DO per-face extra light
        float light_mode; // 0=fully lit, 1=not lit, 2=diffuse, 3=Gouraud
        float cr,cg,cb; // immutable RGB vertex tint from patched JK .rgbx mesh
    };
    static_assert(sizeof(gpu_3do_vertex)==80,"Unexpected GPU vertex stride");

    constexpr unsigned gpu_3do_max_lights=16;
    // Samplers in the 3DO fragment program must not alias different
    // texture types. MATs/decals occupy 0..11, spotlight cookies 12..15,
    // and translucent sun shadow maps 16..18. The sector plane buffer is
    // the next unshared unit, even when the JSON option is disabled.
    constexpr int gpu_3do_sector_plane_texture_unit=
        texture_units::forward_sector_buffer;
    struct gpu_3do_light {
        std::array<float,3> local_position{};
        float radius=0.0f;
        float intensity=0.0f;
        uint32_t native_id=0; // rdCamera light index for SideMat tint lookup
    };

    struct gpu_3do_face_plane {
        std::array<float,3> anchor{};
        std::array<float,3> normal{};
    };
    struct gpu_3do_group {
        uint32_t offset=0, count=0; // index-buffer range
        uintptr_t material=0;       // native JK rdMaterial pointer
        int32_t cel=-1;             // -1 means follow current material cel
        uint32_t face_type=0; // RD_FF_DOUBLE_SIDED=0x1, translucent=0x2
        std::array<float,3> centroid{}; // face-local center for transparency sorting
        uint32_t plane_start=0, plane_count=0; // planes used for CPU backface rejection
        // Conservative local-space bounds of all faces in this draw range.
        // Used to reject entirely offscreen material groups before GL calls.
        std::array<float,3> bounds_min{},bounds_max{};
        bool nonplanar=false; // keep per-face JK culling, bypass GL triangle culling
        bool solid_color=false; // face GeoMode 3: palette-indexed MAT, no texture
        bool saber_trail_join=false; // last composite triangle of a procedural saber, after tip caps
        bool saber_trail_tip_bridge=false; // SideMat strip connecting past/live tip rims, just before the 3D fan
    };
    // Immutable draw-range and key metadata shared by primary and offscreen
    // cameras. Live MAT state, Thing opacity and view visibility stay separate.
    struct gpu_3do_group_submission {
        using capture_key=std::tuple<uintptr_t,int32_t,bool,bool>;
        using opaque_key=std::tuple<uintptr_t,int32_t,uint32_t,bool,bool>;
        opaque_key opaque_sort_key{};
        // 0 = authored solid flag, 1 = instance GeoMode forces solid.
        std::array<capture_key,2> capture_keys{};
        uint64_t main_material_key=0;
        uint32_t group_index=0,index_count=0;
        uintptr_t index_offset_bytes=0;
        bool native_translucent=false;
    };
    struct gpu_3do_geometry {
        std::vector<gpu_3do_face_plane> face_planes;
        std::vector<gpu_3do_vertex> vertices;
        std::vector<uint32_t> indices;
        // A few authored 3DOs contain true zero-width faces. These are
        // invisible to filled hardware rendering and safe to skip while
        // keeping every valid face of their mesh GPU cached.
        uint32_t zero_area_faces_skipped=0;
        uint32_t first_zero_area_face=0;
        // Untriangulable self-intersections omit only their own polygon.
        uint32_t self_intersecting_faces_skipped=0;
        uint32_t first_self_intersecting_face=0;
        uint32_t repaired_zero_normals=0;
        uint32_t trimmed_repeated_prefix_faces=0;
        std::vector<gpu_3do_group> groups;
        // Unique material/cel pairs: checked once per mesh per frame.
        std::vector<std::pair<uintptr_t,int32_t>> material_refs;
        std::vector<std::pair<uintptr_t,int32_t>> solid_material_refs;

        // Material grouping belongs to the immutable cached mesh, not each
        // Thing. Compute once on first opaque submission for native meshes,
        // sprites and procedural geometries. Keep groups[] in authored order
        // for transparent depth-sort ties and other cached geometry consumers.
        // Cache state lives behind a copy-resettable wrapper. Decal follow
        // intentionally copies and move-assigns gpu_3do_geometry while
        // transforming vertices/bounds. A std::once_flag directly on the
        // geometry would delete its implicitly generated copy/move operators.
        // Copies get a fresh lazy cache, preventing stale ordering if their
        // group keys are subsequently edited. Moves transfer the cache.
        struct opaque_order_cache {
            std::once_flag once;
            std::vector<size_t> indices;
            std::once_flag submission_once;
            std::vector<gpu_3do_group_submission> submissions;
            // This same immutable geometry may be displayed in the normal
            // view, planar, shadows, and many 2-/6-view reflection probes.
            // Union its groups' local bounds once, not once per camera.
            std::once_flag bounds_once;
            bool bounds_complete=false;
            std::array<float,3> bounds_min{},bounds_max{};
        };
        struct opaque_order_cache_owner {
            std::shared_ptr<opaque_order_cache> state=
                std::make_shared<opaque_order_cache>();
            opaque_order_cache_owner()=default;
            opaque_order_cache_owner(opaque_order_cache_owner const &)
                : state(std::make_shared<opaque_order_cache>()) {}
            opaque_order_cache_owner &operator=(
                opaque_order_cache_owner const &) {
                state=std::make_shared<opaque_order_cache>();
                return *this;
            }
            opaque_order_cache_owner(opaque_order_cache_owner &&)=default;
            opaque_order_cache_owner &operator=(
                opaque_order_cache_owner &&)=default;
        };
        std::vector<gpu_3do_group_submission> const &
        cached_group_submissions() const {
            auto &cache=*opaque_order_state.state;
            std::call_once(cache.submission_once,[this,&cache] {
                auto &out=cache.submissions;
                out.reserve(groups.size());
                for(size_t i=0;i<groups.size();++i) {
                    auto const &group=groups[i];
                    gpu_3do_group_submission meta{};
                    meta.group_index=uint32_t(i);
                    meta.opaque_sort_key=std::make_tuple(
                        group.material,group.cel,group.face_type,
                        group.nonplanar,group.solid_color);
                    bool const no_cull=group.nonplanar ||
                        (group.face_type&1U)!=0U;
                    meta.capture_keys[0]=std::make_tuple(
                        group.material,group.cel,group.solid_color,no_cull);
                    meta.capture_keys[1]=std::make_tuple(
                        group.material,group.cel,true,no_cull);
                    // Preserve the normal renderer's established 32-bit
                    // native material/cel key representation.
                    meta.main_material_key=
                        (uint64_t(group.material)<<32) |
                        uint32_t(group.cel);
                    meta.index_count=group.count;
                    meta.index_offset_bytes=
                        uintptr_t(group.offset)*sizeof(uint32_t);
                    meta.native_translucent=(group.face_type&0x2U)!=0U;
                    out.push_back(meta);
                }
            });
            return cache.submissions;
        }
        std::vector<size_t> const &cached_opaque_group_order() const {
            auto &cache=*opaque_order_state.state;
            auto const &meta=cached_group_submissions();
            std::call_once(cache.once,[&cache,&meta] {
                auto &opaque_order=cache.indices;
                opaque_order.resize(meta.size());
                std::iota(opaque_order.begin(),opaque_order.end(),size_t(0));
                std::sort(opaque_order.begin(),opaque_order.end(),
                    [&meta](size_t a,size_t b) {
                        if(meta[a].opaque_sort_key!=meta[b].opaque_sort_key)
                            return meta[a].opaque_sort_key<
                                   meta[b].opaque_sort_key;
                        return a<b; // identical keys retain authored order
                    });
            });
            return cache.indices;
        }
        // Valid only if EVERY drawable group has finite, ordered bounds.
        // A missing or invalid bound fails open: never reject potentially
        // visible geometry from an incomplete whole-mesh enclosure.
        // This lives beside cached_opaque_group_order(), so normal-camera
        // instances and independent reflection snapshots reuse it whenever
        // they reference the same immutable gpu_3do_geometry object.
        bool cached_group_bounds(std::array<float,3> &out_min,
                                 std::array<float,3> &out_max) const {
            auto &cache=*opaque_order_state.state;
            std::call_once(cache.bounds_once,[this,&cache] {
                std::array<float,3> lo{},hi{};
                lo.fill(std::numeric_limits<float>::infinity());
                hi.fill(-std::numeric_limits<float>::infinity());
                bool any=false;
                for(auto const &group:groups) {
                    if(group.count==0U)continue;
                    for(unsigned axis=0;axis<3U;++axis)
                        if(!std::isfinite(group.bounds_min[axis]) ||
                           !std::isfinite(group.bounds_max[axis]) ||
                           group.bounds_min[axis]>group.bounds_max[axis])
                            return; // no partial bounds allowed
                    for(unsigned axis=0;axis<3U;++axis) {
                        lo[axis]=std::min(lo[axis],group.bounds_min[axis]);
                        hi[axis]=std::max(hi[axis],group.bounds_max[axis]);
                    }
                    any=true;
                }
                if(!any)return;
                cache.bounds_min=lo;
                cache.bounds_max=hi;
                cache.bounds_complete=true;
            });
            if(!cache.bounds_complete)return false;
            out_min=cache.bounds_min;
            out_max=cache.bounds_max;
            return true;
        }
    private:
        mutable opaque_order_cache_owner opaque_order_state;
    };

    // Allocated only for cached models that overlap multiple Adjoin-linked
    // sectors. No per-instance vector allocation on the ordinary fast path.
    struct gpu_3do_crossing_sector {
        std::array<float,3> bounds_min{},bounds_max{};
        std::array<float,3> interior_min{},interior_max{};
        std::array<float,3> cmp_tint{{1.0f,1.0f,1.0f}};
        std::array<float,3> sector_tint{};
        float ambient=1.0f;
        uint32_t plane_start=0,plane_count=0;
        bool interior_valid=false;
    };
    struct gpu_3do_crossing_state {
        uint32_t count=0;
        std::array<gpu_3do_crossing_sector,4> sectors{};
    };
    // Snapshot the selected sector volume PER instance, including its live
    // ExtraLight floor. Deferred 3DO passes never hold pointers into a
    // mutable world cache or rely on the currently selected native sector.
    struct gpu_3do_ambient_volume {
        std::array<float,3> bounds_min{},bounds_max{};
        std::array<uint8_t,8> samples{};
        float range=1.0f;
        float live_floor=0.0f;
    };
    struct gpu_3do_instance {
        uint64_t id=0;
        std::shared_ptr<gpu_3do_geometry const> geometry;
        std::array<float,12> transform{}; // 3DO node matrix
        // JK's rdCamera light list is Thing-dependent: snapshot when the mesh
        // hook runs, NOT at present(), after another Thing changes it.
        float ambient=1.0f, attenuation=0.0f;
        // Lighting from scaled/sheared R/L/U nodes uses transformed world
        // distances and inverse-transposed normals, not local distances.
        bool nonrigid_light_transform=false;
        bool is_projected_decal=false; // type-3 SPR, world- or receiver-local
        bool is_sprite_quad=false; // ordinary SPR with shared GPU quad
        bool transient_procedural=false; // particles/polylines: evict after this frame
        bool saber_trail_active=false; // diagnostics: procedural blade fan was appended
        float sprite_opacity=1.0f; // SetSpriteInfo opacity
        float thing_opacity=1.0f; // GetThingOpacity; per-Thing, not mesh state
        bool thing_dither=false; // opaque-pass screen-door fade
        uintptr_t sprite_occlusion_resource=0; // native rdSprite pointer; 0 when not a Thing-owned SPR
        // Stable identity of the model mesh this instance came from.
        // Used by moving decals; zero when not captured or not an eligible
        // moving receiver. Never infer ownership from shared mesh pointers.
        uint64_t decal_receiver_thing=0;
        uintptr_t decal_receiver_model=0;
        uintptr_t decal_receiver_mesh=0;
        uint32_t decal_receiver_geoset=0xffffffffU; // exact GeoSet chosen by JK for this draw
        uint64_t decal_receiver_revision=0;
        uint32_t decal_receiver_occurrence=0;
        bool shadow_only=false; // native 3DO remains JK-rendered; cache is for depth only
        uint32_t effective_geometry_mode=4; // min(Thing/mesh), face mode per group
        bool camera_hud=false; // SithThing type=camera: composite unlit after post
        bool camera_hud_world_depth=false; // Camera + ThingFlag 0x20: occlude behind opaque world
        bool skybox_vertex_lighting=false; // non-Camera ThingFlag 0x20: JK-style vertex point lights, not pixel
        bool casts_sun_shadows=true; // writes depth into sun/spotlight shadow maps
        bool world_lighting_exempt=false; // receives no sunlight/shadows/SSAO; independent of casting
        uint64_t shadow_owner=0; // pointer + signature, not shared rdMesh id
        // Attachment-root owner for spotlight self-shadow suppression only.
        // Keep shadow_owner unchanged for sun atlas grouping.
        uint64_t spotlight_shadow_family=0;
        bool dynamic_sun_caster=false;
        float decal_opacity=1.0f; // separate from the native MAT's alpha
        float effective_light_mode=3.0f; // JK's effective per-Thing lighting cap
        std::array<float,3> colormap_tint{{1.0f,1.0f,1.0f}};
        // Live SithSector tint of the owning SithThing (not camera).
        // Invalid/missing owner safely preserves native global effects.
        std::array<float,3> sector_tint{{0.0f,0.0f,0.0f}};
        bool sector_tint_valid=false;
        // Only native rdModel3 Things have an authoritative owning sector.
        uint32_t owning_sector_id=0;
        bool owning_sector_valid=false;
        // Native SithThing +0xb8 WORLD position, common across all its
        // animated 3DO nodes. Do not use transform[9..11]: rdMesh's 3x4
        // matrix is view-space and differs from one hierarchy node to another.
        std::array<float,3> owning_thing_world_position{};
        bool owning_thing_world_position_valid=false;
        // Scaled rdModel3 authored radius, captured once from rdModel3_Draw
        // and inherited by every hierarchy mesh regardless of animation.
        float owning_thing_lod_radius=0.0f;
        bool owning_thing_lod_radius_valid=false;
        std::shared_ptr<gpu_3do_crossing_state const> crossing_lighting;
        // Independent sparse continuous-ambient GPU experiment. Store the
        // owning sector's eight-corner SPARSE-MISS fallback and live floor
        // once per Thing; the vertex shader uses world-aligned sparse nodes
        // first and falls back only when a key is absent.
        std::optional<gpu_3do_ambient_volume> continuous_ambient_fallback;
        unsigned num_lights=0;
        std::array<gpu_3do_light,gpu_3do_max_lights> lights{};
    };
    struct gpu_3do_native_stats {
        uint64_t attempted=0,culled=0,queued=0,snapshots=0,ready_checks=0;
        uint64_t source_checks=0,source_ns=0,source_invalidations=0;
        uint64_t failed_snapshot_reuses=0;
        // Primary + independent planar camera share one CPU snapshot cache.
        // Entry count/peak are gauges; other fields are window event counts.
        uint64_t cache_entries=0,cache_peak=0;
        uint64_t cache_hits=0,cache_misses=0;
        uint64_t cache_evictions=0,cache_trim_deferred=0;
        uint64_t cache_hash_new=0,cache_hash_header=0;
        uint64_t cache_hash_revision=0;
        uint64_t cache_hash_fallback=0;
        // On-demand face-material checks for USED cached meshes only.
        // COG mutations never scan the whole cache.
        uint64_t mutation_scans=0,mutation_scanned=0;
        uint64_t mutation_invalidated=0,mutation_ns=0;
        uint64_t geometry_native=0,snapshot_native=0;
        // Diagnostic taxonomy for the native 3DO fallback hot path.
        uint64_t geo_thing_not_full=0,geo_mesh_not_full=0;
        uint64_t snapshot_source_rejected=0,snapshot_face_rejected=0;
        uint64_t snapshot_triangulation_rejected=0,snapshot_other_rejected=0;
        uint64_t empty_mesh_native=0; // mesh callback with no drawable vertices/faces
        uint64_t native_span_cache_hits=0,native_span_queries=0;
        uint64_t material_native=0,lighting_native=0;
        uint64_t nonrigid_lit_cached=0; // visible GPU mesh with transformed lights
        uint64_t ready_material_refs=0,ready_ns=0,ready_cache_hits=0;
    };
    gpu_3do_native_stats consume_gpu_3do_native_stats();
    // force_shadow_only keeps the native visible draw while capturing a
    // depth-only caster, including when the normal GPU render path falls back.
    bool queue_gpu_3do_mesh(void const *mesh,void const *matrix,
                            bool force_shadow_only=false);
    std::vector<gpu_3do_instance> consume_gpu_3do_instances();
    void reset_gpu_3do_instances();
    // Called after any D3D texture upload, including native MAT cel warm-up.
    void notify_native_mat_texture_uploaded();
}
