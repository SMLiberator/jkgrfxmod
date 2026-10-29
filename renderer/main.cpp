#include "base/default_logger.hpp"
#include "base/log.hpp"
#include "base/span.hpp"
#include "common/config.hpp"
#include "common/error_reporter.hpp"
#include "fog.hpp"
#include "postfx.hpp"
#include "renderer.hpp"
#include "runtime_material.hpp"
#include "native_3do_profile.hpp"
#include "native_sprite_profile.hpp"
#include "gpu_3do_preview.hpp"
#include "camera_hud_thing.hpp"
#include "gpu_mat_ready_cache.hpp"
#include "gpu_3do_modelview.hpp"
#include "thing_node_native.hpp"
#include "thing_node_attachment.hpp"
#include "spotlight.hpp"
#include "thing_attachment_normal.hpp"
#include "native_thingface_friction.hpp"
#include "native_earth_hit_sound.hpp"
#include "mots_native_collision_bridge.hpp"
#include "mots_collision_snapshot_cache.hpp"
#include "cog_math.hpp"
#include "sprite_orientation.hpp"
#include "sprite_type_registry.hpp"
#include "sprite_variants.hpp"
#include "sprite_instance_info.hpp"
#include "sprite_quad_transform.hpp"
#include "sprite_decal_world.hpp"
#include "sprite_decal_world_cache.hpp"
#include "sprite_decal_static_mesh.hpp"
#include "sprite_decal_auto_align.hpp"
#include "sprite_decal_pose_cache.hpp"
#include "sprite_decal_resource.hpp"
#include "sprite_decal_attachment.hpp"
#include "sprite_alpha_tag.hpp"
#include "gpu_3do_frustum.hpp"
#include "gpu_3do_compat.hpp"
#include "gpu_3do_triangulation.hpp"
#include "gpu_3do_cache_signature.hpp"
#include "gpu_3do_colormap.hpp"
#include "jk_colormap_layout.hpp"
#include "native_mat_mips.hpp"
#include "engine_material_debug.hpp"
#include "sun.hpp"
#include "sun_skybox_thing.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <cstddef>
#include <string>
#include <sstream>
#include <cstdio>
#include <random>
#include <detours/detours.h>

static std::unique_ptr<jkgm::renderer> the_renderer;
static std::unique_ptr<jkgm::config> the_config;
static bool renderer_hooks_active = false;

static bool native_3do_hooks_active = false;

    // The user's JK.dll exposes mesh/material mutations through registered
    // COG verbs. Wrap the verb *registration*, not DLL machine-code offsets:
    // this preserves original arguments/results and detects modifications
    // immediately after the verb handler returns.
    namespace mesh_revision {
        using handler = void(__cdecl *)(void *);
        using registrar = void(__cdecl *)(void *,handler,char const *);
        constexpr size_t mutator_count=16U;
        constexpr std::array<char const *,mutator_count> names{{
            "setmodelmesh","clonemodel","changemodelmaterial",
            "replacemodelmaterial","appendthingmaterial","appendmodelmaterial",
            "appendthingemptymesh","appendmodelemptymesh","appendmodelnode",
            "replacemodelmeshmaterial","clearmodel","clearmodelmesh",
            "overwritemodel","reloadmodel","setmodelnodemesh","swapmodelmesh"
        }};
        std::array<handler,mutator_count> originals{};
        registrar original_registrar=nullptr;
        uint32_t registered_mask=0U;
        uint64_t generation=1U;
        bool complete=false;
        bool untracked_write=false;

        template<size_t I>
        void __cdecl invoke(void *ctx)
        {
            // Call the original JK.dll COG handler exactly once, without
            // touching the interpreter's stack or its returned result.
            originals[I](ctx);
            ++generation; // global invalidation; only actual verb calls do this
        }
        constexpr std::array<handler,mutator_count> wrappers{{
            invoke<0>,invoke<1>,invoke<2>,invoke<3>,
            invoke<4>,invoke<5>,invoke<6>,invoke<7>,
            invoke<8>,invoke<9>,invoke<10>,invoke<11>,
            invoke<12>,invoke<13>,invoke<14>,invoke<15>
        }};

        void __cdecl register_mutator(void *ctx,handler fn,char const *name)
        {
            if(fn && name) {
                for(size_t i=0;i<mutator_count;++i) {
                    if(std::strcmp(name,names[i])==0) {
                        originals[i]=fn;
                        registered_mask|=uint32_t(1U<<i);
                        original_registrar(ctx,wrappers[i],name);
                        return;
                    }
                }
            }
            original_registrar(ctx,fn,name);
        }

        void begin(registrar fn)
        {
            original_registrar=fn;
            registered_mask=0U;
            originals.fill(nullptr);
            complete=false;
            untracked_write=false;
        }
        void finish()
        {
            complete=registered_mask==((1U<<mutator_count)-1U);
            if(complete) {
                LOG_INFO("GPU 3DO revision tracking: ",mutator_count,
                         " JK.dll mesh/material COG mutation verbs wrapped; "
                         "periodic signature audit every 32 frames");
            } else {
                LOG_WARNING("GPU 3DO revision tracking unavailable: missing JK.dll "
                            "mesh/material verb registrations; full hashing retained");
            }
        }
        bool active() {return complete && !untracked_write;}
        uint64_t current() {return generation;}
        void report_untracked()
        {
            if(untracked_write)return;
            untracked_write=true;
            LOG_WARNING("GPU 3DO revision audit detected a mesh write outside "
                        "tracked JK.dll COG verbs; disabling fast path, "
                        "restoring full per-frame hashing for this session");
        }
    }

static bool native_mat_mips_hook_active = false;
static bool native_mesh_collision_hook_active = false;
static bool native_node_attachment_hooks_active = false;
static bool native_thingface_friction_hook_active = false;
static bool native_earth_hit_sound_hook_active = false;

// Function boundaries and prologues independently verified in both the
// user's SHA-locked jk23.exe and standalone jkgm.exe. Not portable to other JKs.
namespace {
    using native_model_func = int (__cdecl *)(void *, void *);
    using native_mesh_func = void (__cdecl *)(void *, void *);
    using native_face_func = int (__cdecl *)(void *, int);
    constexpr uintptr_t native_model_addr = 0x00446060U;
    constexpr uintptr_t native_mesh_addr = 0x00446360U;
    constexpr uintptr_t native_face_addr = 0x004467D0U;
    constexpr unsigned char native_model_bytes[] =
        {0x83,0xec,0x18,0x8b,0x0d,0x18,0xa4,0x73,0x00};
    constexpr unsigned char native_mesh_bytes[] =
        {0x81,0xec,0x90,0x00,0x00,0x00,0x53,0x55};
    constexpr unsigned char native_face_bytes[] =
        {0x83,0xec,0x0c,0x53,0x55,0x56,0x57,0xe8};
    native_model_func RealNativeModel =
        reinterpret_cast<native_model_func>(native_model_addr);
    // Set for the duration of a single rdModel3_Draw invocation. Nested
    // model calls restore their parent Thing's flags on return.
    thread_local uint32_t active_native_model_thing_flags=0U;
    // Camera is SithThing type 1, distinct from the active rdCamera.
    // This is a per-Thing choice, never a shared rdMesh/MAT property.
    thread_local bool active_native_model_camera_hud=false;
    thread_local uint64_t active_native_shadow_owner=0;
    thread_local uint64_t active_native_spotlight_family=0;
    thread_local bool active_native_shadow_dynamic=false;

    // Defined below by the per-frame native memory-query cache. The
    // attachment-family resolver runs earlier in this translation unit.
    bool gpu_span_readable(void const *p,size_t len);

    // Resolve validated SITH_ATTACH_THING ancestry. Use this exact helper
    // for both spotlight registrations and rdModel3 casters, so headlights
    // attached to a node and other separately drawn attached car parts
    // share the same shadow family. The original per-Thing sun owner remains
    // unchanged (sun atlas allocation must not merge different Things).
    uint64_t spotlight_shadow_family_key(uint64_t key)
    {
        uintptr_t owner=uintptr_t(uint32_t(key>>32));
        uint32_t signature=uint32_t(key);
        if(owner<0x10000U || signature==0U)return key;
        uintptr_t visited[16]{};
        unsigned visited_count=0;
        for(unsigned depth=0;depth<16U;++depth) {
            if(!gpu_span_readable(reinterpret_cast<void const *>(owner),0x78U))
                break;
            uint32_t kind=0,current_signature=0,flags=0,parent=0;
            auto const *p=reinterpret_cast<uint8_t const *>(owner);
            std::memcpy(&kind,p+0x0cU,4);
            std::memcpy(&current_signature,p+0x74U,4);
            if(kind==0U || kind>15U || current_signature!=signature)
                break;
            if(visited_count<16U)visited[visited_count++]=owner;
            std::memcpy(&flags,p+0x34U,4);
            if((flags&jkgm::native_attach_thing)==0U)break;
            std::memcpy(&parent,p+0x58U,4);
            if(parent<0x10000U || uintptr_t(parent)==owner ||
               std::find(visited,visited+visited_count,uintptr_t(parent))!=
                   visited+visited_count ||
               !gpu_span_readable(reinterpret_cast<void const *>(
                   uintptr_t(parent)),0x78U))
                break;
            uint32_t parent_kind=0,parent_signature=0;
            std::memcpy(&parent_kind,reinterpret_cast<void const *>(
                uintptr_t(parent)+0x0cU),4);
            std::memcpy(&parent_signature,reinterpret_cast<void const *>(
                uintptr_t(parent)+0x74U),4);
            if(parent_kind==0U || parent_kind>15U ||
               parent_signature==0U)break;
            owner=uintptr_t(parent);
            signature=parent_signature;
        }
        return (uint64_t(uint32_t(owner))<<32)|signature;
    }
    // Pose-based shadow classification: moveType describes how a Thing CAN
    // move, not whether it currently moves. Keep per-Thing poses independently
    // of the currently selected rendering/cascade mode.
    struct native_shadow_pose_state {
        std::array<float,12> pose{};
        uint64_t last_seen=0;
        uint64_t last_moved=0;
    };
    std::unordered_map<uint64_t,native_shadow_pose_state> native_shadow_poses;
    native_mesh_func RealNativeMesh =
        reinterpret_cast<native_mesh_func>(native_mesh_addr);
    native_face_func RealNativeFace =
        reinterpret_cast<native_face_func>(native_face_addr);
    jkgm::native_3do_sample native_totals;

    // Verified native jk23.exe and jkgm.exe rdroid_SetMipDistances.
    // Four floats at 0x548258 control native MAT mip selection in rdCache.
    using native_set_mip_distances_fn=int (__cdecl *)(float const *);
    constexpr uintptr_t native_set_mip_distances_addr=0x0043AE30U;
    constexpr uintptr_t native_mip_distances_addr=0x00548258U;
    constexpr unsigned char native_set_mip_distances_bytes[]={
        0x8b,0x44,0x24,0x04, 0x8b,0x08,0x89,0x0d,0x58,0x82,0x54,0x00
    };
    native_set_mip_distances_fn RealNativeSetMipDistances=
        reinterpret_cast<native_set_mip_distances_fn>(native_set_mip_distances_addr);

    void force_native_mat_mip_zero()
    {
        jkgm::prefer_native_mat_mip_zero(
            reinterpret_cast<float *>(native_mip_distances_addr));
    }

    int __cdecl HookedNativeSetMipDistances(float const *requested)
    {
        int const result=RealNativeSetMipDistances(requested);
        // Engine level/video changes may reset the mip thresholds.
        force_native_mat_mip_zero();
        return result;
    }


    // Match the current patched JK23 executable at rdModel3_DrawMesh (0x446360).
    struct native_mesh {
        char name[32];
        int mesh_index, geometry_mode, lighting_mode, texture_mode;
        float const *vertices;
        void const *uvs, *vertex_lights, *dynamic_lights;
        uint8_t const *faces;
        void const *normals;
        int num_vertices, num_uvs, num_faces;
        float radius;
    };
    struct native_face {
        uint32_t num, type, geo, light, tex, count;
        int const *pos_indices, *uv_indices;
        void const *material;
        uint32_t cel;
        float uv_x, uv_y, extra_light, normal[3];
    };
    static_assert(sizeof(native_mesh)==0x58 && offsetof(native_mesh,vertices)==0x30 &&
                  offsetof(native_mesh,faces)==0x40 && offsetof(native_mesh,num_faces)==0x50,
                  "Wrong patched JK 3DO mesh layout");
    static_assert(sizeof(native_face)==0x40,"Wrong JK 3DO face stride");

    struct cached_gpu_mesh {
        float const *vertices = nullptr;
        uint8_t const *faces = nullptr;
        float const *rgb = nullptr;
        void const *uvs=nullptr,*normals=nullptr,*vertex_lights=nullptr;
        int num_vertices=0,num_faces=0,num_uvs=0;
        int lighting_mode=0,texture_mode=0;
        uint64_t source_signature=0,source_checked_frame=0;
        uint64_t source_checked_generation=0,last_audit_frame=0;
        char const *snapshot_failure=nullptr; // memoized failed snapshots
        uint64_t id=0;
        std::shared_ptr<jkgm::gpu_3do_geometry const> geometry;
        uint64_t ready_checked_frame=0;
        bool ready_for_frame=false;
    };
    std::unordered_map<void const *,cached_gpu_mesh> gpu_mesh_cache;
    std::vector<jkgm::gpu_3do_instance> pending_gpu_instances;
    uint64_t next_gpu_id=1;
    uint64_t gpu_frame_serial=1;
    jkgm::gpu_3do_native_stats gpu_native_stats;
    jkgm::gpu_mat_ready_cache gpu_mat_residency;
    std::atomic<uint64_t> native_mat_upload_generation{1};
    // Debug-only fallback names, capped to avoid filling the performance log
    // with transient meshes. This never runs in normal gameplay.
    std::unordered_set<std::string> gpu_logged_fallbacks;
    void gpu_log_fallback(native_mesh const &m,std::string const &reason)
    {
        if(!the_config || !the_config->profile_renderer ||
           gpu_logged_fallbacks.size()>=80) return;
        auto const end=std::find(m.name,m.name+sizeof(m.name),'\0');
        std::string const name(m.name,end);
        if(name.empty()) return;
        if(gpu_logged_fallbacks.insert(name+"|"+reason).second)
            LOG_INFO("GPU 3DO fallback mesh=",name," reason=",reason,
                     " vertices=",m.num_vertices," faces=",m.num_faces);
    }

    bool gpu_span_readable(void const *p,size_t len) {
        if(!p || len==0 || len>4*1024*1024) return false;
        uintptr_t const start=reinterpret_cast<uintptr_t>(p);
        if(start<0x10000U || start>0x7fffffffU ||
           start+len<start) return false;
        // Native 3DOs repeatedly reference the same readable allocations.
        // Cache only positively validated regions within the current GPU
        // frame; never cache failures and never reuse across frames.
        // Direct-mapped slots bound memory and avoid a map allocation.
        struct readable_region {
            uint64_t frame=0;
            uintptr_t page=0,begin=0,end=0;
        };
        static thread_local std::array<readable_region,2048> regions{};
        uintptr_t const page=start>>12;
        readable_region *entry=&regions[(page*2654435761U)&(regions.size()-1)];
        if(entry->frame==gpu_frame_serial && entry->page==page &&
           start>=entry->begin && start<entry->end) {
            if(the_config && the_config->profile_renderer)
                ++gpu_native_stats.native_span_cache_hits;
            return start+len<=entry->end;
        }
        MEMORY_BASIC_INFORMATION mbi{};
        if(the_config && the_config->profile_renderer)
            ++gpu_native_stats.native_span_queries;
        if(VirtualQuery(p,&mbi,sizeof(mbi))==0 || mbi.State!=MEM_COMMIT ||
           (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        auto const begin=reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        auto const end=begin+mbi.RegionSize;
        if(entry && end>begin) {
            *entry={gpu_frame_serial,start>>12,begin,end};
        }
        return start+len<=end;
    }

    // An unflagged 3DO face can still have translucency in its MAT cel.
    // Build a separate draw range for those faces so alpha ordering can be
    // corrected later without ever changing the persistent index buffer.
    bool gpu_3do_face_may_have_alpha(native_face const &face)
    {
        if((face.type&0x2U)!=0) return true;
        if(!face.material) return false;
        using namespace jkgm::engine_material_debug;
        // The snapshot builder runs this for EVERY face. Use the same
        // per-frame native-memory validation as ordinary GPU mesh reads:
        // native MAT/texinfo/texture objects often share memory regions.
        // Preserve the original full-struct readability requirements and
        // conservative alpha fallback on invalid data.
        auto const read_cached=[](uintptr_t ptr,auto &out) {
            if(!gpu_span_readable(reinterpret_cast<void const *>(ptr),
                                  sizeof(out)))return false;
            std::memcpy(&out,reinterpret_cast<void const *>(ptr),sizeof(out));
            return true;
        };
        native_material mat{};
        if(!read_cached(reinterpret_cast<uintptr_t>(face.material),mat) ||
           mat.num_texinfo==0 || mat.num_texinfo>16)
            return true; // uncertain: don't join its faces
        int32_t cel=static_cast<int32_t>(face.cel);
        if(cel<0) cel=static_cast<int32_t>(mat.current_cel);
        cel=std::max(0,std::min(cel,static_cast<int32_t>(mat.num_texinfo)-1));
        native_texinfo info{};
        if(!read_cached(mat.texinfo[cel],info) || !info.texture)
            return true;
        // Only the first native_texture header word is needed for alpha,
        // but validate the complete struct just as read_object did.
        if(!gpu_span_readable(reinterpret_cast<void const *>(
                uintptr_t(info.texture)),sizeof(native_texture)))return true;
        uint32_t texture_flags=0;
        std::memcpy(&texture_flags,reinterpret_cast<void const *>(
                        uintptr_t(info.texture)),sizeof(texture_flags));
        return (texture_flags&1U)!=0;
    }

    // JK23 rdColormap_SetCurrent (0x442760) stores current/identity CMP
    // pointers in 0x73A410/0x73A414. A non-identity CMP uses the three
    // float header tint values at rdColormap+0x24 (32-byte name prefix).
    std::array<float,3> read_gpu_3do_colormap_tint()
    {
        constexpr std::array<float,3> white{{1.0f,1.0f,1.0f}};
        static bool const supported=[] {
            constexpr uint8_t bytes[]{0x8b,0x44,0x24,0x04,0x8b,0x0d,
                0x10,0xa4,0x73,0x00,0x3b,0xc8,0x74,0x05};
            auto const *p=reinterpret_cast<void const *>(0x00442760U);
            return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))==
                       0x00400000U && gpu_span_readable(p,sizeof(bytes)) &&
                   std::memcmp(p,bytes,sizeof(bytes))==0;
        }();
        if(!supported) return white;
        uint32_t current=0,identity=0;
        std::memcpy(&current,reinterpret_cast<void const *>(0x0073A410U),4);
        std::memcpy(&identity,reinterpret_cast<void const *>(0x0073A414U),4);
        if(!current || current==identity) return white;
        if(!gpu_span_readable(reinterpret_cast<void const *>(uintptr_t(current)),
                              jkgm::jk_cmp_colors_offset))
            return white;
        std::array<float,3> tint{};
        std::memcpy(tint.data(),
                    reinterpret_cast<uint8_t const *>(uintptr_t(current))+
                        jkgm::jk_cmp_tint_offset,12);
        return jkgm::gpu_3do_cmp_tint(false,tint);
    }

    // When bypassing rdModel3_DrawMesh, its built-in sphere/frustum cull
    // would otherwise disappear: this previously queued *every* mesh,
    // including thousands well outside the camera view.
    bool gpu_mesh_outside_native_frustum(native_mesh const &m,void const *matrix,
                                         jkgm::engine_camera const &camera)
    {
        if(!gpu_span_readable(matrix,12*sizeof(float))) return false;
        constexpr uintptr_t camera_global=0x0073A418U;
        if(!gpu_span_readable(reinterpret_cast<void const *>(camera_global),
                              sizeof(uintptr_t))) return false;
        uintptr_t cam_ptr=0;
        std::memcpy(&cam_ptr,reinterpret_cast<void const *>(camera_global),
                    sizeof(cam_ptr));
        if(cam_ptr<0x10000U ||
           !gpu_span_readable(reinterpret_cast<void const *>(cam_ptr),0x4c))
            return false;
        uintptr_t frustum_ptr=0;
        int32_t project_type=0;
        std::memcpy(&project_type,reinterpret_cast<void const *>(cam_ptr),4);
        std::memcpy(&frustum_ptr,
                    reinterpret_cast<void const *>(cam_ptr+0x48U),sizeof(frustum_ptr));
        if(frustum_ptr<0x10000U ||
           !gpu_span_readable(reinterpret_cast<void const *>(frustum_ptr),
                              sizeof(jkgm::gpu_3do_native_frustum)))
            return false;
        jkgm::gpu_3do_native_frustum f{};
        std::memcpy(&f,reinterpret_cast<void const *>(frustum_ptr),sizeof(f));
        float world[12];
        std::memcpy(world,matrix,sizeof(world));
        float const dx=world[9]-camera.position[0];
        float const dy=world[10]-camera.position[1];
        float const dz=world[11]-camera.position[2];
        float const x=dx*camera.right[0]+dy*camera.right[1]+dz*camera.right[2];
        float const y=dx*camera.forward[0]+dy*camera.forward[1]+dz*camera.forward[2];
        float const z=dx*camera.up[0]+dy*camera.up[1]+dz*camera.up[2];
        // JK's mesh radius is in local units. Account conservatively for
        // scaled placement matrices and avoid rejecting borderline meshes.
        float max_axis_scale=1.0f;
        for(int axis=0;axis<3;axis++) {
            int const i=axis*3;
            float const scale=std::sqrt(world[i]*world[i]+
                                        world[i+1]*world[i+1]+
                                        world[i+2]*world[i+2]);
            if(std::isfinite(scale)) max_axis_scale=std::max(max_axis_scale,scale);
        }
        return jkgm::gpu_3do_sphere_outside(
            f,project_type==1,x,y,z,m.radius*max_axis_scale+0.02f);
    }

    // Local positions are used for per-vertex illumination, so they travel
    // with the cached mesh while the current Thing's lights are uniforms.
    // The inverse works for the reflected/mirrored R/F/U bases JK permits.
    bool snapshot_gpu_3do_lighting(native_mesh const &m,
                                   float const *placement,
                                   jkgm::gpu_3do_instance &instance)
    {
        constexpr uintptr_t camera_global=0x0073A418U;
        if(!gpu_span_readable(reinterpret_cast<void const *>(camera_global),4))
            return false;
        uintptr_t camera=0;
        std::memcpy(&camera,reinterpret_cast<void const *>(camera_global),4);
        // Original x86 rdCamera: ambient=0x54, count=0x58,
        // 64 light pointers=0x5c, 64 world positions=0x15c,
        // attenuationMin=0x45c.
        if(camera<0x10000U ||
           !gpu_span_readable(reinterpret_cast<void const *>(camera),0x464U))
            return false;
        float ambient=0.0f, attenuation=0.0f;
        int32_t count=0;
        std::memcpy(&ambient,reinterpret_cast<void const *>(camera+0x54),4);
        std::memcpy(&count,reinterpret_cast<void const *>(camera+0x58),4);
        std::memcpy(&attenuation,reinterpret_cast<void const *>(camera+0x45c),4);
        if(!std::isfinite(ambient) || !std::isfinite(attenuation) ||
           count<0 || count>64 || attenuation<0.0f || attenuation>100.0f)
            return false;
        instance.ambient=std::max(0.0f,std::min(ambient,1.0f));
        instance.attenuation=attenuation;
        // With no dynamic point lights the shader uses the authored JK
        // vertex intensity/ambient only. There is no distance attenuation
        // to miscompute, so a nonuniform or mirrored node is safe to cache.
        if(count==0)return true;

        float const rx=placement[0],ry=placement[1],rz=placement[2];
        float const fx=placement[3],fy=placement[4],fz=placement[5];
        float const ux=placement[6],uy=placement[7],uz=placement[8];
        float const cfx=fy*uz-fz*uy, cfy=fz*ux-fx*uz, cfz=fx*uy-fy*ux;
        float const determinant=rx*cfx+ry*cfy+rz*cfz;
        // A nonrigid node is safe to cache with dynamic lights as long as
        // its affine matrix is invertible. The preview shader evaluates
        // light distances in transformed camera/world space for such nodes.
        // Singular transforms must still use native JK lighting.
        bool const matrix_invertible=std::isfinite(determinant) &&
            std::fabs(determinant)>1.e-6f;
        bool nonrigid_lighting=!matrix_invertible ||
            std::fabs(std::fabs(determinant)-1.0f)>0.05f;
        float max_axis_scale=1.0f;
        for(int axis=0;axis<3;axis++) {
            int const i=axis*3;
            float const len=placement[i]*placement[i]+
                            placement[i+1]*placement[i+1]+
                            placement[i+2]*placement[i+2];
            if(!std::isfinite(len))return false;
            if(std::fabs(len-1.0f)>0.05f)nonrigid_lighting=true;
            max_axis_scale=std::max(max_axis_scale,std::sqrt(len));
        }

        for(int i=0;i<count;i++) {
            uintptr_t light=0;
            std::memcpy(&light,reinterpret_cast<void const *>(camera+0x5c+4*i),4);
            if(light<0x10000U ||
               !gpu_span_readable(reinterpret_cast<void const *>(light),0x30))
                return false;
            uint32_t id=0,enabled=0;
            int32_t type=0;
            float strength=0.0f,radius=0.0f;
            std::memcpy(&id,reinterpret_cast<void const *>(light),4);
            std::memcpy(&type,reinterpret_cast<void const *>(light+4),4);
            std::memcpy(&enabled,reinterpret_cast<void const *>(light+8),4);
            std::memcpy(&strength,reinterpret_cast<void const *>(light+0x18),4);
            std::memcpy(&radius,reinterpret_cast<void const *>(light+0x28),4);
            if(id>=64 || !std::isfinite(strength) || !std::isfinite(radius))
                return false;
            if(!enabled || strength<=0.0f || radius<=0.0f) continue;
            float pos[3];
            std::memcpy(pos,reinterpret_cast<void const *>(camera+0x15c+id*12),12);
            if(!std::isfinite(pos[0]) || !std::isfinite(pos[1]) ||
               !std::isfinite(pos[2])) return false;
            float const dx=pos[0]-placement[9];
            float const dy=pos[1]-placement[10];
            float const dz=pos[2]-placement[11];
            float const reach=radius+std::max(0.0f,m.radius)*
                max_axis_scale;
            if(dx*dx+dy*dy+dz*dz>reach*reach) continue;
            // Transform into the mesh's local basis as before; for
            // nonrigid nodes the shader transforms the delta back into
            // rigid camera space before measuring distance/angle.
            if(!matrix_invertible)return false;
            if(nonrigid_lighting)instance.nonrigid_light_transform=true;
            // Spot/other light types need the original cone/angle logic.
            // Do not misrender them as omnidirectional point lights.
            if(type<0 || type>2) return false;
            if(instance.num_lights==instance.lights.size())
                return false; // Native path for light-heavy scenes; never drop lights.
            auto &out=instance.lights[instance.num_lights++];
            out.local_position={
                (dx*cfx+dy*cfy+dz*cfz)/determinant,
                (rx*(dy*uz-dz*uy)+ry*(dz*ux-dx*uz)+
                 rz*(dx*uy-dy*ux))/determinant,
                (rx*(fy*dz-fz*dy)+ry*(fz*dx-fx*dz)+
                 rz*(fx*dy-fy*dx))/determinant
            };
            out.radius=radius;
            out.intensity=strength;
        }
        return true;
    }

    // Memoize readable virtual-memory regions while hashing a mesh. All face
    // corner index arrays generally live in one/few allocation regions; avoid
    // two expensive VirtualQuery calls per face on every animation frame.
    struct gpu_source_readable_regions {
        struct region { uintptr_t begin=0,end=0; };
        std::array<region,16> regions{};
        size_t next=0;
        bool readable(void const *ptr,size_t length) {
            if(!ptr || length==0 || length>4*1024*1024) return false;
            uintptr_t const address=reinterpret_cast<uintptr_t>(ptr);
            if(address<0x10000U || address>0x7fffffffU ||
               address+length<address) return false;
            for(auto const &r:regions) {
                if(address>=r.begin && address+length<=r.end) return true;
            }
            MEMORY_BASIC_INFORMATION info{};
            if(VirtualQuery(ptr,&info,sizeof(info))==0 ||
               info.State!=MEM_COMMIT ||
               (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
            uintptr_t const begin=reinterpret_cast<uintptr_t>(info.BaseAddress);
            uintptr_t const end=begin+info.RegionSize;
            if(end<begin || address+length>end) return false;
            regions[next++%regions.size()]={begin,end};
            return true;
        }
    };

    // A full source signature once per *unique mesh per frame* detects
    // in-place COG/JK.dll changes without relying on a particular verb list.
    // All array reads are guarded; the old cache never noticed writes when
    // allocations, face/vertex counts, and pointers stayed identical.
    bool gpu_mesh_source_signature(native_mesh const &m,
                                   float const *vertex_rgb,uint64_t &value) {
        if(m.num_vertices<3 || m.num_vertices>65536 ||
           m.num_faces<1 || m.num_faces>65536 ||
           !gpu_span_readable(m.vertices,size_t(m.num_vertices)*3*sizeof(float)) ||
           !gpu_span_readable(m.faces,size_t(m.num_faces)*sizeof(native_face)) ||
           (vertex_rgb && !gpu_span_readable(
               vertex_rgb,size_t(m.num_vertices)*3*sizeof(float))))
            return false;

        jkgm::gpu_3do_byte_signature h;
        h.add(m.vertices);h.add(m.faces);h.add(m.uvs);
        h.add(m.vertex_lights);h.add(m.normals);h.add(vertex_rgb);
        h.add(m.num_vertices);h.add(m.num_faces);h.add(m.num_uvs);
        h.add(m.geometry_mode);h.add(m.lighting_mode);h.add(m.texture_mode);
        h.add_bytes(m.vertices,size_t(m.num_vertices)*3*sizeof(float));
        if(vertex_rgb)
            h.add_bytes(vertex_rgb,size_t(m.num_vertices)*3*sizeof(float));

        bool const has_uvs=m.uvs && m.num_uvs>0 && m.num_uvs<=65536;
        if(has_uvs) {
            size_t const bytes=size_t(m.num_uvs)*2*sizeof(float);
            if(!gpu_span_readable(m.uvs,bytes)) return false;
            h.add_bytes(m.uvs,bytes);
        }
        if(m.vertex_lights) {
            size_t const bytes=size_t(m.num_vertices)*sizeof(float);
            if(!gpu_span_readable(m.vertex_lights,bytes)) return false;
            h.add_bytes(m.vertex_lights,bytes);
        }
        if(m.normals) {
            size_t const bytes=size_t(m.num_vertices)*3*sizeof(float);
            if(!gpu_span_readable(m.normals,bytes)) return false;
            h.add_bytes(m.normals,bytes);
        }

        gpu_source_readable_regions readable;
        for(int i=0;i<m.num_faces;i++) {
            native_face f{};
            std::memcpy(&f,m.faces+size_t(i)*sizeof(f),sizeof(f));
            h.add(f.num);h.add(f.type);h.add(f.geo);h.add(f.light);
            h.add(f.tex);h.add(f.count);h.add(f.pos_indices);
            h.add(f.uv_indices);h.add(f.material);h.add(f.cel);
            h.add(f.uv_x);h.add(f.uv_y);h.add(f.extra_light);
            h.add_bytes(f.normal,sizeof(f.normal));
            if(f.count<3 || f.count>128) return false;
            size_t const bytes=size_t(f.count)*sizeof(int);
            if(!readable.readable(f.pos_indices,bytes)) return false;
            h.add_bytes(f.pos_indices,bytes);
            if(has_uvs && f.uv_indices) {
                if(!readable.readable(f.uv_indices,bytes)) return false;
                h.add_bytes(f.uv_indices,bytes);
            }
        }
        value=h.value;
        return true;
    }

    std::shared_ptr<jkgm::gpu_3do_geometry const>
    snapshot_gpu_mesh(native_mesh const &m, float const *vertex_rgb,
                      char const **failure) {
        auto const reject=[&](char const *reason)
            ->std::shared_ptr<jkgm::gpu_3do_geometry const> {
            if(failure) *failure=reason;
            return {};
        };
        if(m.num_vertices<3 || m.num_vertices>65536 || m.num_faces<1 ||
           m.num_faces>65536 ||
           !gpu_span_readable(m.vertices,size_t(m.num_vertices)*12) ||
           !gpu_span_readable(m.faces,size_t(m.num_faces)*sizeof(native_face)) ||
           (vertex_rgb &&
            !gpu_span_readable(vertex_rgb,size_t(m.num_vertices)*3*sizeof(float))))
            return reject("invalid_mesh_counts_or_memory");
        // Sith stores positions and UVs in separate arrays. Faces index them
        // independently, so split each face corner rather than indexing
        // positions directly: otherwise textured seams get corrupted.
        bool const has_uvs=m.uvs && m.num_uvs>0 &&
            m.num_uvs<=65536 &&
            gpu_span_readable(m.uvs,size_t(m.num_uvs)*2*sizeof(float));
        // The patched JK pipeline forwards per-vertex smooth normals to its
        // G-buffer even when a face does not use Gouraud lighting. The GPU
        // path should use those same normals for SSAO; face normals make AO
        // artificially dark along low-poly boundaries.
        bool const has_smooth_normals=m.normals &&
            gpu_span_readable(m.normals,size_t(m.num_vertices)*12);
        float const *uv_src=has_uvs
            ? static_cast<float const *>(m.uvs) : nullptr;
        auto g=std::make_shared<jkgm::gpu_3do_geometry>();
        for(int f=0;f<m.num_faces;f++) {
            native_face face{};
            std::memcpy(&face,m.faces+size_t(f)*sizeof(face),sizeof(face));
            if(face.count<3 || face.count>128 ||
               !jkgm::gpu_3do_supports_face_mode(static_cast<int>(face.geo)) ||
               !gpu_span_readable(face.pos_indices,size_t(face.count)*4) ||
               !std::isfinite(face.uv_x) || !std::isfinite(face.uv_y))
                return reject("invalid_face_mode_or_face_indices");
            bool const face_uv=uv_src && face.uv_indices &&
                gpu_span_readable(face.uv_indices,size_t(face.count)*4);
            // Fail open when a textured polygon lacks readable UVs.
            // Avoid suppressing JK geometry that we cannot reproduce.
            if(face.material && face.geo>=4 && !face_uv)
                return reject("textured_face_missing_uv");
            // The native engine's per-face mode is capped by the mesh mode.
            // Faces with unknown modes fall back instead of rendering unlit.
            if(m.lighting_mode<0 || m.lighting_mode>3 ||
               face.light>3 || !std::isfinite(face.extra_light))
                return reject("invalid_mesh_or_face_light_mode");
            // Preserve force-unlit faces as a shader sentinel (4), since
            // they override even a FullyLit per-Thing lighting cap.
            int const mode=(face.type&0x10U)!=0 ? 4
                            : std::min(m.lighting_mode,static_cast<int>(face.light));
            if(mode==3 && (!m.vertex_lights || !has_smooth_normals ||
               !gpu_span_readable(m.vertex_lights,size_t(m.num_vertices)*4)))
                return reject("missing_gouraud_normals_or_lights");
            uint32_t const base=static_cast<uint32_t>(g->vertices.size());
            int const anchor_index=face.pos_indices[0];
            if(anchor_index<0 || anchor_index>=m.num_vertices)
                return reject("invalid_anchor_index");
            auto const anchor=m.vertices+size_t(anchor_index)*3;
            for(uint32_t v=0;v<face.count;v++) {
                int const pi=face.pos_indices[v];
                if(pi<0 || pi>=m.num_vertices)
                    return reject("invalid_position_index");
                auto const i=size_t(pi)*3;
                float u=0,vtex=0;
                if(face_uv) {
                    int const uv_idx=face.uv_indices[v];
                    if(uv_idx<0 || uv_idx>=m.num_uvs)
                        return reject("invalid_uv_index");
                    u=uv_src[size_t(uv_idx)*2]+face.uv_x;
                    vtex=uv_src[size_t(uv_idx)*2+1]+face.uv_y;
                }
                // Reuse the saved smooth normal for the AO G-buffer in
                // every lighting mode, while the lighting shader continues
                // to use the flat face normal for diffuse/non-Gouraud modes.
                auto const light_normal=has_smooth_normals
                    ? static_cast<float const *>(m.normals)+i : face.normal;
                float const static_light=mode==3 ? static_cast<float const *>(m.vertex_lights)[pi] : 0.0f;
                // jkgm.exe's .rgbx extension stores one immutable float3
                // per source vertex at mesh+0x6c. Native D3D packs each
                // channel as floor(clamp(x,0,1)*255+0.5). Match that
                // rounding before shader interpolation and multiplication.
                float rgb[3]={1.0f,1.0f,1.0f};
                if(vertex_rgb) {
                    for(int channel=0;channel<3;channel++) {
                        float const value=vertex_rgb[i+size_t(channel)];
                        if(!std::isfinite(value))
                            return reject("nonfinite_vertex_rgb");
                        rgb[channel]=std::floor(
                            std::clamp(value,0.0f,1.0f)*255.0f+0.5f)/255.0f;
                    }
                }
                jkgm::gpu_3do_vertex out{
                    m.vertices[i],m.vertices[i+1],m.vertices[i+2],
                    u,vtex,face.normal[0],face.normal[1],face.normal[2],
                    static_light,light_normal[0],light_normal[1],light_normal[2],
                    anchor[0],anchor[1],anchor[2],face.extra_light,float(mode),
                    rgb[0],rgb[1],rgb[2]
                };
                for(float vcheck : {out.x,out.y,out.z,out.u,out.v,
                                    out.nx,out.ny,out.nz,out.static_light,
                                    out.lnx,out.lny,out.lnz,
                                    out.fax,out.fay,out.faz,out.face_extra,
                                    out.cr,out.cg,out.cb})
                    if(!std::isfinite(vcheck))
                        return reject("nonfinite_vertex_attribute");
                g->vertices.push_back(out);
            }
            uint32_t const first=static_cast<uint32_t>(g->indices.size());
            // Native rdModel3 can handle outlines that are not convex (and
            // sometimes not perfectly planar). A triangle fan loses those
            // faces. Clip ears in a stable 2D projection instead, preserving
            // each corner's original material UV and native vertex lighting.
            // For invalid outlines use the entire native mesh draw, rather
            // than silently losing geometry from the cached GPU path.
            std::vector<std::array<float,3>> outline;
            outline.reserve(face.count);
            for(uint32_t v=0;v<face.count;v++) {
                auto const &p=g->vertices[base+v];
                outline.push_back({p.x,p.y,p.z});
            }
            std::vector<std::array<uint32_t,3>> face_triangles;
            bool nonplanar=false;
            if(!jkgm::gpu_3do_triangulate_face(
                   outline,{{face.normal[0],face.normal[1],face.normal[2]}},
                   face_triangles,nonplanar))
                return reject("non_simple_or_degenerate_face");
            for(auto const &tri:face_triangles) {
                g->indices.push_back(base+tri[0]);
                g->indices.push_back(base+tri[1]);
                g->indices.push_back(base+tri[2]);
            }
            uint32_t const added=static_cast<uint32_t>(g->indices.size())-first;
            std::array<float,3> center{};
            for(uint32_t v=0;v<face.count;v++) {
                auto const &point=g->vertices[base+v];
                center[0]+=point.x/float(face.count);
                center[1]+=point.y/float(face.count);
                center[2]+=point.z/float(face.count);
            }
            // 0x1 means JK must draw both sides. 0x2 enables translucency.
            // Keep translucent faces separately to sort them back-to-front.
            auto const ptr=reinterpret_cast<uintptr_t>(face.material);
            int32_t const cel=static_cast<int32_t>(face.cel);
            uint32_t const flags=face.type&0x3U;
            bool const translucent=gpu_3do_face_may_have_alpha(face);
            uint32_t const plane_start=static_cast<uint32_t>(g->face_planes.size());
            g->face_planes.push_back({{anchor[0],anchor[1],anchor[2]},
                                      {face.normal[0],face.normal[1],face.normal[2]}});
            std::array<float,3> bounds_lo{{g->vertices[base].x,
                                            g->vertices[base].y,
                                            g->vertices[base].z}};
            auto bounds_hi=bounds_lo;
            for(uint32_t v=1;v<face.count;v++) {
                auto const &p=g->vertices[base+v];
                for(int axis=0;axis<3;axis++) {
                    float const value=axis==0?p.x:(axis==1?p.y:p.z);
                    bounds_lo[axis]=std::min(bounds_lo[axis],value);
                    bounds_hi[axis]=std::max(bounds_hi[axis],value);
                }
            }
            // Warped faces use JK's one-plane visibility rule with GL culling
            // disabled. Keep each warped face in a distinct range so the CPU
            // can reject it individually rather than draw unrelated backsides.
            if(!translucent && !nonplanar && !g->groups.empty() &&
               g->groups.back().material==ptr &&
               g->groups.back().cel==cel &&
               g->groups.back().face_type==flags &&
               g->groups.back().nonplanar==nonplanar) {
                auto &group=g->groups.back();
                group.count+=added;
                ++group.plane_count;
                for(int axis=0;axis<3;axis++) {
                    group.bounds_min[axis]=std::min(group.bounds_min[axis],
                                                    bounds_lo[axis]);
                    group.bounds_max[axis]=std::max(group.bounds_max[axis],
                                                    bounds_hi[axis]);
                }
            } else {
                g->groups.push_back({first,added,ptr,cel,flags,center,
                                     plane_start,1,bounds_lo,bounds_hi,nonplanar});
            }
        }
        // Avoid revalidating the same native MAT thousands of times in one
        // frame merely because its faces form many distinct draw groups.
        g->material_refs.reserve(g->groups.size());
        for(auto const &group:g->groups)
            if(group.material) g->material_refs.emplace_back(group.material,group.cel);
        std::sort(g->material_refs.begin(),g->material_refs.end());
        g->material_refs.erase(
            std::unique(g->material_refs.begin(),g->material_refs.end()),
            g->material_refs.end());
        return g->indices.empty() ? reject("no_drawable_indices")
                                  : g;
    }

    // Native JK must warm MATs before a GPU-cached 3DO can draw them.
    // The experimental in-frame rdMaterial_AddToTextureCache call crashed
    // inside the std3D texture uploader during level loading.
    bool gpu_mesh_materials_ready(jkgm::gpu_3do_geometry const &g)
    {
        using namespace jkgm::engine_material_debug;
        if(!matches_tested_executable()) return false;
        auto const upload_generation=
            native_mat_upload_generation.load(std::memory_order_relaxed);
        for(auto const &ref:g.material_refs) {
            native_material mat{};
            if(!read_object(ref.first,mat)) return false;
            if((mat.tex_type&2U)==0) continue; // pure solid color
            if(mat.num_texinfo==0 || mat.num_texinfo>16) return false;
            int32_t cel=ref.second<0
                ? static_cast<int32_t>(mat.current_cel) : ref.second;
            cel=std::max(0,std::min(cel,static_cast<int32_t>(mat.num_texinfo)-1));
            // Re-read current_cel on every query: SetMatCel and animated MATs
            // must never reuse the previous cel's resident handle.
            if(gpu_mat_residency.contains(gpu_frame_serial,upload_generation,
                                          ref.first,static_cast<uint32_t>(cel))) {
                if(the_config && the_config->profile_renderer)
                    ++gpu_native_stats.ready_cache_hits;
                continue;
            }
            native_texinfo info{};
            if(!read_object(mat.texinfo[cel],info)) return false;
            if((info.header[0]&8U)==0) {
                gpu_mat_residency.mark_ready(gpu_frame_serial,upload_generation,
                                              ref.first,static_cast<uint32_t>(cel));
                continue; // no texture for this cel
            }
            native_texture tex{};
            if(!read_object(info.texture,tex)) return false;
            unsigned const mips=std::min(tex.header[6],4U);
            if(mips==0) return false;
            bool present=false;
            for(unsigned i=0;i<mips;i++) {
                auto const &a=tex.alpha_mips[i],&o=tex.opaque_mips[i];
                present|=(a.texture_loaded && a.texture_handle>0) ||
                         (o.texture_loaded && o.texture_handle>0);
            }
            if(!present) return false; // native mesh fallback warms MAT
            // Never cache a negative result: JK may make a cel resident
            // later in this SAME frame via native drawing or D3D texture load.
            gpu_mat_residency.mark_ready(gpu_frame_serial,upload_generation,
                                          ref.first,static_cast<uint32_t>(cel));
        }
        return true;
    }

    uint64_t elapsed_native_ns(std::chrono::steady_clock::time_point start)
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-start).count());
    }
    int __cdecl HookedNativeModel(void *thing, void *placement)
    {
        auto const profiling=the_config && the_config->profile_renderer;
        auto const start=profiling ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
        uint32_t flags=0;
        uint64_t owner_key=0;
        // rdThing.pThing is +0x44. Read the linked SithThing TYPE even
        // when sun/spotlight shadows are disabled. A type=camera Thing
        // with a 3DO render type is our post-composited HUD layer.
        bool camera_hud=false;
        uintptr_t const native_rd=reinterpret_cast<uintptr_t>(thing);
        uint32_t hud_owner=0;
        if(gpu_span_readable(reinterpret_cast<void const *>(
                native_rd+0x44U),4)) {
            std::memcpy(&hud_owner,reinterpret_cast<void const *>(
                native_rd+0x44U),4);
            if(hud_owner>=0x10000U &&
               gpu_span_readable(reinterpret_cast<void const *>(hud_owner),0xc8U)) {
                uint32_t native_type=0,native_render_type=0;
                std::memcpy(&native_type,reinterpret_cast<void const *>(
                    uintptr_t(hud_owner)+0x0cU),4);
                std::memcpy(&native_render_type,reinterpret_cast<void const *>(
                    uintptr_t(hud_owner)+0xc4U),4);
                camera_hud=jkgm::is_camera_hud_model(
                    native_type,native_render_type);
                // A HUD with ThingFlag 0x20 uses world-depth occlusion
                // even if sun and spotlight shadows are disabled. The
                // owner memory was already validated above; do not add
                // per-frame VirtualQuery work for ordinary model Things.
                if(camera_hud &&
                   uintptr_t(hud_owner)+0xc4U==native_rd)
                    std::memcpy(&flags,reinterpret_cast<void const *>(
                                    uintptr_t(hud_owner)),sizeof(flags));
            }
        }
        bool moving=true; // Unverified owners cannot safely enter static cache.
        bool const shadow_enabled=the_config &&
            (the_config->enable_sun_shadows ||
             (the_config->enable_spotlights &&
              the_config->enable_spotlight_shadows));
        if(shadow_enabled) {
            auto const read=[](uintptr_t address,void *dst,size_t n) {
                if(!gpu_span_readable(reinterpret_cast<void const *>(address),n))
                    return false;
                std::memcpy(dst,reinterpret_cast<void const *>(address),n);
                return true;
            };
            uintptr_t const rd=reinterpret_cast<uintptr_t>(thing);
            if(jkgm::read_native_model_thing_flags(read,rd,flags)) {
                uint32_t owner=0,signature=0;
                if(read(rd+0x44U,&owner,4) &&
                   read(uintptr_t(owner)+0x74U,&signature,4)) {
                    owner_key=(uint64_t(owner)<<32)|signature;
                    // Verified JK23 SithThing layout: orient +0x88, position
                    // +0xb8, linked animation class +0x128, puppet +0x12c.
                    // rdThing.puppet is +0x14 relative to embedded rdThing.
                    std::array<uint8_t,0xa8> state{};
                    if(read(uintptr_t(owner)+0x88U,
                            state.data(),state.size())) {
                        std::array<float,12> pose{};
                        std::memcpy(pose.data(),state.data(),9*sizeof(float));
                        std::memcpy(pose.data()+9,state.data()+0x30U,
                                    3*sizeof(float));
                        uint32_t animation_class=0,puppet=0,rd_puppet=0;
                        std::memcpy(&animation_class,state.data()+0xa0U,4);
                        std::memcpy(&puppet,state.data()+0xa4U,4);
                        std::memcpy(&rd_puppet,state.data()+0x50U,4);
                        bool finite=true;
                        for(float v:pose)finite=finite && std::isfinite(v);
                        if(finite) {
                            auto const stamp=gpu_frame_serial;
                            auto found=native_shadow_poses.find(owner_key);
                            bool changed=false;
                            if(found==native_shadow_poses.end()) {
                                native_shadow_pose_state record{};
                                record.pose=pose;
                                record.last_seen=stamp;
                                found=native_shadow_poses.emplace(
                                    owner_key,std::move(record)).first;
                            } else if(found->second.last_seen!=stamp) {
                                for(size_t n=0;n<pose.size();++n)
                                    if(std::fabs(found->second.pose[n]-pose[n])>
                                       0.0001f) {
                                        changed=true;
                                        break;
                                    }
                                found->second.pose=pose;
                                found->second.last_seen=stamp;
                                if(changed)found->second.last_moved=stamp;
                            }
                            // A one-frame tail prevents rapidly changing
                            // static/dynamic membership on tiny pose jitter.
                            bool const recent_motion=changed ||
                                (found->second.last_moved!=0 &&
                                 stamp-found->second.last_moved<=1U);
                            moving=animation_class!=0 || puppet!=0 ||
                                   rd_puppet!=0 || recent_motion;
                            // Bound long sessions with rapidly recycled Things.
                            if((stamp%1024U)==0U &&
                               native_shadow_poses.size()>4096) {
                                for(auto it=native_shadow_poses.begin();
                                    it!=native_shadow_poses.end();) {
                                    if(stamp>it->second.last_seen+1024U)
                                        it=native_shadow_poses.erase(it);
                                    else ++it;
                                }
                            }
                        }
                    }
                }
            }
        }
        uint32_t const previous_flags=active_native_model_thing_flags;
        bool const previous_camera_hud=active_native_model_camera_hud;
        uint64_t const previous_owner=active_native_shadow_owner;
        uint64_t const previous_family=active_native_spotlight_family;
        bool const previous_moving=active_native_shadow_dynamic;
        active_native_model_thing_flags=flags;
        active_native_model_camera_hud=camera_hud;
        active_native_shadow_owner=owner_key;
        active_native_spotlight_family=owner_key &&
            the_config && the_config->enable_spotlights &&
            the_config->enable_spotlight_shadows
            ? spotlight_shadow_family_key(owner_key) : owner_key;
        active_native_shadow_dynamic=moving;
        auto const engine_start=profiling ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point{};
        uint64_t const mesh_before=profiling?native_totals.mesh_ns:0;
        int const result=RealNativeModel(thing,placement);
        uint64_t const engine_ns=profiling?elapsed_native_ns(engine_start):0;
        active_native_model_thing_flags=previous_flags;
        active_native_model_camera_hud=previous_camera_hud;
        active_native_shadow_owner=previous_owner;
        active_native_spotlight_family=previous_family;
        active_native_shadow_dynamic=previous_moving;
        if(profiling) {
            // The native model timer includes nested mesh-hook work.
            // Report that overlap explicitly rather than attributing it
            // to hierarchy traversal. This is approximate if a model
            // recursively calls another model, but does not change rendering.
            uint64_t const nested_mesh_ns=native_totals.mesh_ns-mesh_before;
            uint64_t const total_ns=elapsed_native_ns(start);
            native_totals.model_engine_ns+=engine_ns;
            native_totals.model_engine_self_ns+=
                engine_ns>nested_mesh_ns?engine_ns-nested_mesh_ns:0;
            native_totals.model_hook_ns+=
                total_ns>engine_ns?total_ns-engine_ns:0;
            native_totals.model_ns+=total_ns;
            ++native_totals.model_calls;
            if(result!=0)++native_totals.model_drawn;
        }
        return result;
    }
    void capture_static_sprite_decal_mesh(void *mesh,void *placement);
    void __cdecl HookedNativeMesh(void *mesh, void *placement)
    {
        auto const profiling=the_config && the_config->profile_renderer;
        auto const start=profiling ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
        if(the_config && the_config->enable_decals) {
            capture_static_sprite_decal_mesh(mesh,placement);
            if(profiling)
                native_totals.mesh_decal_ns+=elapsed_native_ns(start);
        }
        auto const queue_begin=profiling ? std::chrono::steady_clock::now()
                                        : std::chrono::steady_clock::time_point{};
        bool const queued=jkgm::queue_gpu_3do_mesh(mesh,placement);
        // Inspect native RLU matrices BEFORE GPU eligibility filters can
        // discard a mesh. A zero in the renderer-side RLU diagnostic does
        // not establish that Sith never supplied a scaled node.
        if(profiling && gpu_frame_serial%120U==0U) {
            static thread_local uint64_t sample_frame=0;
            static thread_local unsigned sample_logged=0;
            if(sample_frame!=gpu_frame_serial) {
                sample_frame=gpu_frame_serial;
                sample_logged=0;
            }
            if(sample_logged<12U &&
               gpu_span_readable(mesh,sizeof(native_mesh)) &&
               gpu_span_readable(placement,12U*sizeof(float))) {
                std::array<float,12> native_transform{};
                std::memcpy(native_transform.data(),placement,
                            12U*sizeof(float));
                float axes[3]{};
                bool finite=true;
                for(int i=0;i<3;++i) {
                    int const j=3*i;
                    axes[i]=std::sqrt(native_transform[j]*
                                      native_transform[j]+
                                      native_transform[j+1]*
                                      native_transform[j+1]+
                                      native_transform[j+2]*
                                      native_transform[j+2]);
                    finite=finite && std::isfinite(axes[i]);
                }
                if(finite) {
                    float const det=jkgm::gpu_3do_transform_determinant(
                        native_transform);
                    if(std::fabs(axes[0]-1.0f)>0.005f ||
                       std::fabs(axes[1]-1.0f)>0.005f ||
                       std::fabs(axes[2]-1.0f)>0.005f || det<0.0f) {
                        native_mesh m{};
                        std::memcpy(&m,mesh,sizeof(m));
                        auto const name_end=std::find(
                            m.name,m.name+sizeof(m.name),'\0');
                        std::string name(m.name,name_end);
                        ++sample_logged;
                        LOG_INFO("SUN native RLU mesh=",name,
                                 " owner=",active_native_shadow_owner,
                                 " dynamic=",active_native_shadow_dynamic?1:0,
                                 " casts=",jkgm::thing_casts_sun_shadows(
                                     active_native_model_thing_flags)?1:0,
                                 " GPU=",queued?1:0,
                                 " vertices=",m.num_vertices,
                                 " faces=",m.num_faces,
                                 " axes=",std::to_string(axes[0]),",",
                                 std::to_string(axes[1]),",",
                                 std::to_string(axes[2]),
                                 " det=",std::to_string(det));
                    }
                }
            }
        }
        // A mesh can be ineligible for visible GPU caching (its MAT cel is
        // not resident, unsupported native lighting, etc.) yet still have
        // valid immutable geometry for the directional depth pass. In that
        // case preserve the exact native render below and append a separate
        // shadow-only GPU instance. Previously this was possible ONLY with
        // use_model_cache=false, leaving some characters without shadows.
        // Restrict the second attempt to moving sun casters: don't double
        // snapshot the many static/HUD/native-fallback meshes each frame.
        if(!queued && !active_native_model_camera_hud &&
           the_config && the_config->use_model_cache &&
           the_config->enable_sun_shadows && active_native_shadow_dynamic &&
           active_native_shadow_owner!=0 &&
           jkgm::thing_casts_sun_shadows(active_native_model_thing_flags)) {
            size_t const prior=pending_gpu_instances.size();
            uint64_t const geo_before=gpu_native_stats.geometry_native;
            uint64_t const snapshot_before=gpu_native_stats.snapshot_native;
            uint64_t const source_before=
                gpu_native_stats.snapshot_source_rejected;
            uint64_t const face_before=
                gpu_native_stats.snapshot_face_rejected;
            uint64_t const tri_before=
                gpu_native_stats.snapshot_triangulation_rejected;
            uint64_t const lighting_before=gpu_native_stats.lighting_native;
            jkgm::queue_gpu_3do_mesh(mesh,placement,true);
            bool const captured=pending_gpu_instances.size()>prior;
            char const *shadow_failure=captured?"captured":
                gpu_native_stats.geometry_native>geo_before?"geometry_mode":
                gpu_native_stats.snapshot_source_rejected>source_before
                    ?"source_unreadable":
                gpu_native_stats.snapshot_face_rejected>face_before
                    ?"snapshot_face":
                gpu_native_stats.snapshot_triangulation_rejected>tri_before
                    ?"triangulation":
                gpu_native_stats.snapshot_native>snapshot_before
                    ?"snapshot_other":
                gpu_native_stats.lighting_native>lighting_before
                    ?"camera_or_lighting":"other_early_exit";
            if(profiling && gpu_frame_serial%120U==0U) {
                static thread_local uint64_t sample_frame=0;
                static thread_local unsigned sample_logged=0;
                if(sample_frame!=gpu_frame_serial) {
                    sample_frame=gpu_frame_serial;
                    sample_logged=0;
                }
                if(sample_logged<12U &&
                   gpu_span_readable(mesh,sizeof(native_mesh))) {
                    native_mesh m{};
                    std::memcpy(&m,mesh,sizeof(m));
                    auto const name_end=std::find(
                        m.name,m.name+sizeof(m.name),'\0');
                    std::string const name(m.name,name_end);
                    LOG_INFO("SUN native fallback sidecar mesh=",name,
                             " owner=",active_native_shadow_owner,
                             " captured=",captured?1:0,
                             " reason=",shadow_failure,
                             " vertices=",m.num_vertices,
                             " faces=",m.num_faces);
                    ++sample_logged;
                }
            }
        }
        if(profiling && !queued && active_native_model_camera_hud) {
            // Native fallback is still handled by JK's existing path and
            // cannot be composited as a per-Thing HUD without GPU geometry.
            // Surface this explicitly rather than silently drawing icons
            // inside the post-processed scene.
            static unsigned hud_fallback_logs=0;
            if(hud_fallback_logs++<40U)
                LOG_WARNING("Camera Thing HUD 3DO native fallback; mesh=",
                            reinterpret_cast<uintptr_t>(mesh),
                            " cannot use post-exempt GPU pass this frame");
        }
        if(profiling)
            native_totals.mesh_queue_ns+=elapsed_native_ns(queue_begin);
        if(!queued) {
            auto const fallback_begin=profiling ? std::chrono::steady_clock::now()
                                                : std::chrono::steady_clock::time_point{};
            RealNativeMesh(mesh,placement);
            if(profiling) {
                native_totals.mesh_native_ns+=elapsed_native_ns(fallback_begin);
                ++native_totals.mesh_native_calls;
            }
        }
        else {
            // First side effect of rdModel3_DrawMesh, normally set at 0x446371.
            *reinterpret_cast<void **>(0x0073C1E8U)=mesh;
            if(profiling)++native_totals.mesh_gpu_calls;
        }
        if(profiling) {
            native_totals.mesh_ns+=elapsed_native_ns(start);
            ++native_totals.mesh_calls;
        }
    }
    int __cdecl HookedNativeFace(void *face, int lightFlags)
    {
        auto const start = std::chrono::steady_clock::now();
        int const result = RealNativeFace(face, lightFlags);
        native_totals.face_ns += elapsed_native_ns(start);
        ++native_totals.face_calls;
        if(result != 0) ++native_totals.face_emitted;
        return result;
    }
    bool attach_native_3do_hooks(bool profiling)
    {
        if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) != 0x00400000U ||
           std::memcmp(reinterpret_cast<void const *>(native_model_addr),
                       native_model_bytes, sizeof(native_model_bytes)) != 0 ||
           std::memcmp(reinterpret_cast<void const *>(native_mesh_addr),
                       native_mesh_bytes, sizeof(native_mesh_bytes)) != 0 ||
           std::memcmp(reinterpret_cast<void const *>(native_face_addr),
                       native_face_bytes, sizeof(native_face_bytes)) != 0) {
            LOG_WARNING("Native 3DO mesh hook skipped: unrecognized executable");
            return false;
        }
        LONG error = DetourTransactionBegin();
        if(error != NO_ERROR) return false;
        error = DetourUpdateThread(GetCurrentThread());
        // Sun shadows need rdThing ownership even when profiling is off:
        // the mesh callback has no reliable SithThing pointer of its own.
        if(error == NO_ERROR && (profiling || the_config->enable_sun_shadows))
            error = DetourAttach(&(PVOID &)RealNativeModel, HookedNativeModel);
        if(error == NO_ERROR)
            error = DetourAttach(&(PVOID &)RealNativeMesh, HookedNativeMesh);
        if(error == NO_ERROR && profiling)
            error = DetourAttach(&(PVOID &)RealNativeFace, HookedNativeFace);
        if(error != NO_ERROR) {
            DetourTransactionAbort();
            LOG_WARNING("Native 3DO mesh hook attach failed: ", error);
            return false;
        }
        error = DetourTransactionCommit();
        if(error != NO_ERROR) {
            LOG_WARNING("Native 3DO mesh hook commit failed: ", error);
            return false;
        }
        LOG_INFO("Native 3DO mesh interception attached; profiling=", profiling);
        return true;
    }
    void detach_native_3do_hooks()
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if(the_config->profile_renderer)
            DetourDetach(&(PVOID &)RealNativeFace, HookedNativeFace);
        DetourDetach(&(PVOID &)RealNativeMesh, HookedNativeMesh);
        if(the_config->profile_renderer || the_config->enable_sun_shadows)
            DetourDetach(&(PVOID &)RealNativeModel, HookedNativeModel);
        DetourTransactionCommit();
    }
} // namespace


// Bound shadow-only mesh snapshots to light-local geometry rather than
// copying every native Thing in the level. This AABB/sphere check is
// deliberately conservative; the renderer performs per-group culling later.
// Cached once per native render frame to avoid walking COG Things per mesh.
namespace {
bool native_mesh_overlaps_shadow_light(native_mesh const &mesh,
                                       void const *matrix)
{
    if(!the_config)return false;
    // Sun shadows cover the whole loaded world, so conservatively retain
    // every native model snapshot when that pass is enabled. The renderer
    // then clips each group to the directional map's bounds.
    if(the_config->enable_sun_shadows)return true;
    if(!the_config->enable_spotlights ||
       !the_config->enable_spotlight_shadows)
        return false;
    static uint64_t cached_frame=std::numeric_limits<uint64_t>::max();
    static std::vector<jkgm::spotlight_instance> lights;
    if(cached_frame!=gpu_frame_serial) {
        cached_frame=gpu_frame_serial;
        lights=jkgm::collect_spotlights_for_frame();
    }
    if(lights.empty())return false;
    if(!gpu_span_readable(matrix,12*sizeof(float)))return false;
    float m[12]{};
    std::memcpy(m,matrix,sizeof(m));
    for(float value:m)if(!std::isfinite(value))return false;
    float scale=1.0f;
    for(int i=0;i<9;i+=3)
        scale=std::max(scale,std::sqrt(m[i]*m[i]+m[i+1]*m[i+1]+m[i+2]*m[i+2]));
    float const radius=std::max(0.1f,mesh.radius*scale+0.5f);
    for(auto const &light:lights) {
        if(!(light.range>0.0f && light.width>0.0f &&
             light.height>0.0f))continue;
        bool overlap=true;
        for(int axis=0;axis<3;++axis) {
            float const far_point=light.origin[axis]+
                                  light.forward[axis]*light.range;
            float const extent=std::fabs(light.right[axis])*light.width*0.5f+
                               std::fabs(light.up[axis])*light.height*0.5f;
            float const lo=std::min(light.origin[axis],far_point-extent)-radius;
            float const hi=std::max(light.origin[axis],far_point+extent)+radius;
            if(m[9+axis]<lo || m[9+axis]>hi) {
                overlap=false;break;
            }
        }
        if(overlap)return true;
    }
    return false;
}
}

bool jkgm::queue_gpu_3do_mesh(void const *mesh,void const *matrix,
                              bool force_shadow_only)
{
    // The exact RGB/smooth-normal JK patch has an additional float3*
    // at rdMesh+0x6c, beyond the original 0x58-byte layout. Its loader
    // at 0x9001c0 explicitly zeroes that field before allocating 12 bytes
    // per source vertex (initially white). Verify the patch once.
    static bool const has_rgb_mesh_extension=[] {
        constexpr uintptr_t code=0x009001C0U;
        constexpr uint8_t init_bytes[]={0xc7,0x43,0x6c,0,0,0,0};
        return gpu_span_readable(reinterpret_cast<void const *>(code),
                                 sizeof(init_bytes)) &&
               std::memcmp(reinterpret_cast<void const *>(code),init_bytes,
                           sizeof(init_bytes))==0;
    }();
    if(force_shadow_only && active_native_model_camera_hud)
        return false; // HUD meshes may never be depth-only sun casters.
    bool const shadow_enabled=the_config &&
        ((the_config->enable_spotlights && the_config->enable_spotlight_shadows) ||
         the_config->enable_sun_shadows);
    // Camera-HUD Things are visible-only and cannot enter shadow atlases.
    // Permit their GPU path even when world model caching is disabled.
    bool const shadow_only=!active_native_model_camera_hud &&
        the_config && shadow_enabled &&
        (force_shadow_only || !the_config->use_model_cache);
    bool const casts_sun_shadows=!active_native_model_camera_hud &&
        jkgm::thing_casts_sun_shadows(active_native_model_thing_flags);
    // Excluded/invisible Things never enter either shadow map; they still
    // render normally through JK or the visible GPU 3DO path.
    if(shadow_only && !casts_sun_shadows)return false;
    if(!the_config || (!the_config->use_model_cache && !shadow_only &&
                       !active_native_model_camera_hud) ||
       !mesh || !matrix || pending_gpu_instances.size()>=16384 ||
       // Single VirtualQuery per candidate, including the optional RGB
       // field; thousands of calls happen on busy scenes.
       !gpu_span_readable(mesh,has_rgb_mesh_extension?0x70U:sizeof(native_mesh)) ||
       !gpu_span_readable(matrix,sizeof(float)*12)) return false;
    native_mesh m{};
    std::memcpy(&m,mesh,sizeof(m));
    // Empty 3DO hierarchy nodes still reach the native mesh callback,
    // but the GPU path cannot submit a draw for them. Count them explicitly
    // to distinguish engine traversal overhead from OpenGL draw calls.
    if(the_config->profile_renderer &&
       (m.num_vertices<=0 || m.num_faces<=0))
        ++gpu_native_stats.empty_mesh_native;
    float const *vertex_rgb=nullptr;
    if(has_rgb_mesh_extension) {
        auto const *bytes=static_cast<uint8_t const *>(mesh);
        std::memcpy(&vertex_rgb,bytes+0x6cU,sizeof(vertex_rgb));
    }
    // Only filled geometry is eligible for caching. Preserve native handling
    // for points, wireframe, or faces with ambiguous polygon outlines.
    if(the_config->profile_renderer) ++gpu_native_stats.attempted;
    // rdModel3_Draw at 0x44612c-0x446144 stores the *effective* Thing/
    // global GeoMode in this JK23-specific native global before it visits
    // any hierarchy meshes. Our hook otherwise ignores COG GeoMode changes.
    constexpr uintptr_t effective_geo_mode=0x0073A5D8U;
    if(!gpu_span_readable(reinterpret_cast<void const *>(effective_geo_mode),
                          sizeof(int32_t)))
        return false;
    int32_t current_geo_mode=0;
    std::memcpy(&current_geo_mode,
                reinterpret_cast<void const *>(effective_geo_mode),
                sizeof(current_geo_mode));
    if(!(shadow_only
          ? jkgm::thing_shadow_geomode_visible(current_geo_mode,m.geometry_mode)
          : jkgm::gpu_3do_supports_geo_mode(current_geo_mode,m.geometry_mode))) {
        if(the_config->profile_renderer) {
            ++gpu_native_stats.geometry_native;
            if(current_geo_mode!=4)++gpu_native_stats.geo_thing_not_full;
            else ++gpu_native_stats.geo_mesh_not_full;
        }
        gpu_log_fallback(m,"geometry_mode_not_full");
        return false; // Preserve JK vertex/wire/solid/invisible rendering.
    }
    auto const camera=jkgm::read_active_render_camera();
    if(!camera.valid) {
        if(the_config->profile_renderer) ++gpu_native_stats.lighting_native;
        gpu_log_fallback(m,"camera_unavailable");
        return false;
    }
    // In shadow-only mode JK must still draw the model normally. Outside
    // camera view, only keep a mesh if it can intersect a current spotlight;
    // this also permits off-camera objects to cast shadows.
    bool const near_shadow=shadow_enabled &&
        native_mesh_overlaps_shadow_light(m,matrix);
    if(shadow_only && !near_shadow)return false;
    if(gpu_mesh_outside_native_frustum(m,matrix,camera) &&
       !near_shadow) {
        if(the_config->profile_renderer) ++gpu_native_stats.culled;
        return !shadow_only;
    }
    auto it=gpu_mesh_cache.find(mesh);
    bool snapshot_attempted_this_call=false;
    // Read immutable-looking header fields on each instance, but hash all
    // underlying vertex/UV/face content at most once per mesh per frame.
    // This catches COG writes that keep the same allocation and counts.
    bool const header_unchanged=it!=gpu_mesh_cache.end() &&
        it->second.vertices==m.vertices && it->second.faces==m.faces &&
        it->second.rgb==vertex_rgb && it->second.uvs==m.uvs &&
        it->second.normals==m.normals &&
        it->second.vertex_lights==m.vertex_lights &&
        it->second.num_vertices==m.num_vertices &&
        it->second.num_faces==m.num_faces &&
        it->second.num_uvs==m.num_uvs &&
        it->second.lighting_mode==m.lighting_mode &&
        it->second.texture_mode==m.texture_mode;
    bool const revision_fastpath=the_config->mesh_revision_tracking &&
                                 mesh_revision::active();
    uint64_t const current_generation=mesh_revision::current();
    bool const audit_frame=(gpu_frame_serial%32U)==0U;
    bool const should_hash=!header_unchanged ||
        (revision_fastpath
          ? (it->second.source_checked_generation!=current_generation ||
             (audit_frame && it->second.last_audit_frame!=gpu_frame_serial))
          : it->second.source_checked_frame!=gpu_frame_serial);
    if(should_hash) {
        bool const profiling=the_config->profile_renderer;
        auto const start=profiling ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
        uint64_t signature=0;
        bool const readable=gpu_mesh_source_signature(m,vertex_rgb,signature);
        if(profiling) {
            ++gpu_native_stats.source_checks;
            gpu_native_stats.source_ns+=elapsed_native_ns(start);
        }
        if(!readable) {
            if(profiling) {
                ++gpu_native_stats.snapshot_native;
                ++gpu_native_stats.snapshot_source_rejected;
            }
            gpu_log_fallback(m,"invalid_mesh_source_memory");
            return false;
        }
        if(revision_fastpath && audit_frame && header_unchanged &&
           it->second.source_checked_generation==current_generation &&
           it->second.source_signature!=signature) {
            // A periodic audit caught a modification that bypassed every
            // known JK.dll mutation verb. Switch back to exact old behavior.
            mesh_revision::report_untracked();
        }
        if(!header_unchanged || it->second.source_signature!=signature) {
            char const *failure="unknown_snapshot_error";
            snapshot_attempted_this_call=true;
            auto geom=snapshot_gpu_mesh(m,vertex_rgb,&failure);
            if(gpu_mesh_cache.size()>4096) gpu_mesh_cache.clear();
            if(profiling && header_unchanged)
                ++gpu_native_stats.source_invalidations;
            cached_gpu_mesh updated{};
            updated.vertices=m.vertices;
            updated.faces=m.faces;
            updated.rgb=vertex_rgb;
            updated.uvs=m.uvs;
            updated.normals=m.normals;
            updated.vertex_lights=m.vertex_lights;
            updated.num_vertices=m.num_vertices;
            updated.num_faces=m.num_faces;
            updated.num_uvs=m.num_uvs;
            updated.lighting_mode=m.lighting_mode;
            updated.texture_mode=m.texture_mode;
            updated.source_signature=signature;
            updated.source_checked_frame=gpu_frame_serial;
            updated.source_checked_generation=current_generation;
            updated.last_audit_frame=audit_frame ? gpu_frame_serial : 0U;
            if(geom) {
                updated.id=next_gpu_id++;
                updated.geometry=std::move(geom);
                if(profiling) ++gpu_native_stats.snapshots;
            }
            else {
                // Avoid repeating expensive failed triangulation/validation
                // on every Thing instance and every subsequent frame.
                updated.snapshot_failure=failure;
                gpu_log_fallback(m,failure);
            }
            gpu_mesh_cache[mesh]=std::move(updated);
            it=gpu_mesh_cache.find(mesh);
        }
        else {
            it->second.source_checked_frame=gpu_frame_serial;
            it->second.source_checked_generation=current_generation;
            if(audit_frame)it->second.last_audit_frame=gpu_frame_serial;
        }
    }
    if(!it->second.geometry) {
        if(the_config->profile_renderer) {
            ++gpu_native_stats.snapshot_native;
            if(!snapshot_attempted_this_call)
                ++gpu_native_stats.failed_snapshot_reuses;
            char const *reason=it->second.snapshot_failure;
            if(reason && std::strcmp(reason,"non_simple_or_degenerate_face")==0)
                ++gpu_native_stats.snapshot_triangulation_rejected;
            else if(reason && (
                    std::strcmp(reason,"invalid_face_mode_or_face_indices")==0 ||
                    std::strcmp(reason,"textured_face_missing_uv")==0 ||
                    std::strcmp(reason,"invalid_mesh_or_face_light_mode")==0 ||
                    std::strcmp(reason,"missing_gouraud_normals_or_lights")==0 ||
                    std::strcmp(reason,"invalid_anchor_index")==0 ||
                    std::strcmp(reason,"invalid_position_index")==0 ||
                    std::strcmp(reason,"invalid_uv_index")==0))
                ++gpu_native_stats.snapshot_face_rejected;
            else ++gpu_native_stats.snapshot_other_rejected;
        }
        gpu_log_fallback(m,it->second.snapshot_failure ?
                         it->second.snapshot_failure : "unknown_snapshot_error");
        return false;
    }
    // If a cel has never been encountered, let JK handle this call to warm
    // its native material cache. We can switch to GPU rendering next frame.
    // Many instances share this mesh. Check MAT handles once per mesh per
    // frame, refreshing next frame so animated cels still get a warm-up.
    if(!shadow_only && it->second.ready_checked_frame!=gpu_frame_serial) {
        bool const profiling=the_config->profile_renderer;
        auto const start=profiling ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
        if(profiling) {
            ++gpu_native_stats.ready_checks;
            gpu_native_stats.ready_material_refs+=
                it->second.geometry->material_refs.size();
        }
        it->second.ready_for_frame=gpu_mesh_materials_ready(*it->second.geometry);
        if(profiling) gpu_native_stats.ready_ns+=elapsed_native_ns(start);
        it->second.ready_checked_frame=gpu_frame_serial;
    }
    if(!shadow_only && !it->second.ready_for_frame) {
        if(the_config->profile_renderer) ++gpu_native_stats.material_native;
        gpu_log_fallback(m,"native_MAT_cel_not_yet_resident");
        return false;
    }
    gpu_3do_instance inst;
    inst.id=it->second.id;
    inst.geometry=it->second.geometry;
    inst.shadow_only=shadow_only;
    inst.camera_hud=active_native_model_camera_hud;
    inst.camera_hud_world_depth=jkgm::camera_hud_uses_world_depth(
        inst.camera_hud,active_native_model_thing_flags);
    inst.casts_sun_shadows=casts_sun_shadows;
    inst.shadow_owner=active_native_shadow_owner;
    inst.spotlight_shadow_family=active_native_spotlight_family;
    inst.dynamic_sun_caster=active_native_shadow_dynamic;
    // Depth-only casters need no native MAT residency, point lights,
    // colormap or per-Thing lighting cap. Preserve the native render call.
    std::memcpy(inst.transform.data(),matrix,sizeof(float)*12);
    for(float v:inst.transform)if(!std::isfinite(v))return false;
    if(!shadow_only && !inst.camera_hud) {
        inst.colormap_tint=read_gpu_3do_colormap_tint();
        // curLightingMode (rdModel3_Draw's effective Thing/global cap):
        // the verified rdModel3 global block is +0x48 from original JK.
        // This is sampled per instance because COG can change it each frame.
        constexpr uintptr_t effective_light_mode_addr=0x0073C218U;
        if(!gpu_span_readable(reinterpret_cast<void const *>(effective_light_mode_addr),4))
            return false;
        int32_t lighting_cap=0;
        std::memcpy(&lighting_cap,
                    reinterpret_cast<void const *>(effective_light_mode_addr),4);
        if(lighting_cap<0 || lighting_cap>3) {
            if(the_config->profile_renderer) ++gpu_native_stats.lighting_native;
            gpu_log_fallback(m,"unsupported_thing_lighting_mode");
            return false;
        }
        inst.effective_light_mode=static_cast<float>(lighting_cap);
        if(!snapshot_gpu_3do_lighting(m,inst.transform.data(),inst)) {
            if(the_config->profile_renderer) ++gpu_native_stats.lighting_native;
            gpu_log_fallback(m,"unsupported_dynamic_lighting_or_light_count");
            return false;
        }
    }
    if(!shadow_only && !inst.camera_hud &&
       the_config->profile_renderer &&
       inst.nonrigid_light_transform)
        ++gpu_native_stats.nonrigid_lit_cached;
    pending_gpu_instances.emplace_back(std::move(inst));
    if(the_config->profile_renderer) ++gpu_native_stats.queued;
    return !shadow_only;
}
jkgm::gpu_3do_native_stats jkgm::consume_gpu_3do_native_stats()
{
    auto const sample=gpu_native_stats;
    gpu_native_stats={};
    // Aggregate across scenes/frames rather than logging thousands of
    // repeated fallback meshes. Distinguish unsupported render modes from
    // fixable geometry snapshot failures using the exact native reason.
    if(the_config && the_config->profile_renderer) {
        static unsigned frames=0;
        static gpu_3do_native_stats window{};
        ++frames;
        window.geometry_native+=sample.geometry_native;
        window.geo_thing_not_full+=sample.geo_thing_not_full;
        window.geo_mesh_not_full+=sample.geo_mesh_not_full;
        window.snapshot_native+=sample.snapshot_native;
        window.snapshot_source_rejected+=sample.snapshot_source_rejected;
        window.snapshot_face_rejected+=sample.snapshot_face_rejected;
        window.snapshot_triangulation_rejected+=
            sample.snapshot_triangulation_rejected;
        window.snapshot_other_rejected+=sample.snapshot_other_rejected;
        window.material_native+=sample.material_native;
        window.lighting_native+=sample.lighting_native;
        window.nonrigid_lit_cached+=sample.nonrigid_lit_cached;
        if(frames>=120U) {
            LOG_INFO("PERF GPU fallback taxonomy frames=",frames,
                     " geometry_thing=",window.geo_thing_not_full,
                     " geometry_mesh=",window.geo_mesh_not_full,
                     " snapshot_source=",window.snapshot_source_rejected,
                     " snapshot_face=",window.snapshot_face_rejected,
                     " snapshot_triangulation=",
                     window.snapshot_triangulation_rejected,
                     " snapshot_other=",window.snapshot_other_rejected,
                     " material=",window.material_native,
                     " lighting=",window.lighting_native,
                     " nonrigid_lit_GPU=",window.nonrigid_lit_cached);
            frames=0;
            window={};
        }
    }
    return sample;
}
std::vector<jkgm::gpu_3do_instance> jkgm::consume_gpu_3do_instances()
{
    std::vector<gpu_3do_instance> out;
    out.swap(pending_gpu_instances);
    ++gpu_frame_serial;
    return out;
}
void jkgm::reset_gpu_3do_instances()
{
    pending_gpu_instances.clear();
    gpu_mesh_cache.clear();
    gpu_mat_residency.clear();
    ++gpu_frame_serial;
    gpu_native_stats={};
}

void jkgm::notify_native_mat_texture_uploaded()
{
    native_mat_upload_generation.fetch_add(1,std::memory_order_relaxed);
}

jkgm::native_3do_sample jkgm::consume_native_3do_sample()
{
    auto result = native_totals;
    native_totals = {};
    return result;
}
bool jkgm::native_3do_available()
{
    return native_3do_hooks_active && the_config->profile_renderer;
}


// Extended JK23 SPR orientation: intercept only the native sprite draw routine,
// then adjust the already constructed 4 camera-space vertices immediately before
// clipping. This retains JK's existing MAT/UV/lighting/cel handling and does
// not intercept primitives belonging to level surfaces, models, or particles.
namespace {
    using native_sprite_fn = int (__cdecl *)(void *,void *);
    using native_sprite_mid_fn = void (__cdecl *)();
    constexpr uintptr_t sprite_draw_addr=0x0046CF40U;
    constexpr uintptr_t sprite_preclip_addr=0x0046D182U;
    // JK23: rdCache_AddProcFace records a completed proc-face at
    // [0x86ede0 + 0x6c*index]. The cache converter's 0x43b6f9 cmp
    // immediately follows selection of the native 90/255 alpha.
    constexpr uintptr_t sprite_addproc_addr=0x0043B240U;
    constexpr uintptr_t sprite_alpha_mid_addr=0x0043B6F9U;
    // Verified directly against the supported patched JK23 executables:
    // rdCache_DrawRenderList (0x43C590) is not CALLED by the inlined cache
    // converter. Both the old address 0x5753E8 (triangle count) and
    // 0x6353F8 (triangle buffer) were incorrect for the vertex marker.
    // Hook the actual std3D_AddRenderListVertices submission instead.
    constexpr uintptr_t sprite_hw_upload_addr=0x00429C60U;
    constexpr uintptr_t sprite_hw_buffer_addr=0x0063A400U;
    constexpr uintptr_t sprite_hw_vertex_count_addr=0x005F53F0U;
    constexpr uint32_t sprite_flare_vertex_magic=0xB6A9D000U;
    // The supported patched executables redirect this function to
    // the extended vertex upload implementation through a rel32 JMP.
    constexpr uint8_t sprite_hw_upload_bytes[]={
        0xe9,0x1d,0x72,0x4d,0x00
    };
    constexpr uintptr_t sprite_proc_count_addr=0x0086EDC0U;
    constexpr uintptr_t sprite_proc_base_addr=0x0086EDE0U;
    constexpr size_t sprite_proc_stride=0x6cU;
    constexpr uint8_t sprite_addproc_bytes[]={
        0x55,0x8b,0xec,0x83,0xec,0x18,0xa1,0xc0,0xed,0x86,0x00
    };
    constexpr uint8_t sprite_alpha_mid_bytes[]={
        0x81,0xf9,0xff,0x00,0x00,0x00
    };
    // Preserve frustum bounds when SetSpriteInfo enlarges an individual Thing.
    constexpr uintptr_t sprite_frustum_addr=0x004758D0U;
    constexpr uint8_t sprite_frustum_bytes[]={
        0x83,0xec,0x0c,0x8b,0x54,0x24,0x14,0x8b
    };
    // rdSprite_NewEntry writes type to +0x20; sithSprite_Load overwrites it
    // with the World SPRITES array index. Capture the authored argument.
    using native_sprite_newentry_fn=int(__cdecl *)(
        void *,char const *,int,char const *,float,float,int,int,int,float,
        void const *);
    using native_sprite_free_fn=void(__cdecl *)(void *);
    constexpr uintptr_t sprite_newentry_addr=0x0046CC70U;
    constexpr uintptr_t sprite_free_addr=0x0046CE70U;
    constexpr uintptr_t sprite_freeentry_addr=0x0046CEE0U;
    constexpr uint8_t sprite_newentry_bytes[]={
        0x8b,0x44,0x24,0x08,0x83,0xec,0x10,0x85,0xc0
    };
    constexpr uint8_t sprite_free_bytes[]={
        0x56,0x8b,0x74,0x24,0x08,0x85,0xf6,0x74,0x62
    };
    constexpr uint8_t sprite_freeentry_bytes[]={
        0x56,0x8b,0x74,0x24,0x08,0x8b,0x86,0x9c,0x00,0x00,0x00
    };
    jkgm::sprite_type_registry sprite_types;
    jkgm::sprite_instance_registry sprite_instances;
    std::unordered_set<uint64_t> sprite_decal_diagnostic_keys;
    native_sprite_newentry_fn RealNativeSpriteNewEntry=
        reinterpret_cast<native_sprite_newentry_fn>(sprite_newentry_addr);
    native_sprite_free_fn RealNativeSpriteFree=
        reinterpret_cast<native_sprite_free_fn>(sprite_free_addr);
    native_sprite_free_fn RealNativeSpriteFreeEntry=
        reinterpret_cast<native_sprite_free_fn>(sprite_freeentry_addr);

    // JK23: sithSprite_Load reads ONE 11-field line. Extended SPRs keep
    // that native first resource, with additional variants loaded once and
    // selected when sithThing_SetThingBasedOn creates each Thing.
    constexpr uintptr_t sprite_load_addr=0x004F2620U;
    constexpr uintptr_t sprite_free_world_addr=0x004F25A0U;
    constexpr uintptr_t sprite_clone_thing_addr=0x004CDB70U;
    constexpr uintptr_t sprite_world_loading_ptr=0x00833A18U;
    constexpr uintptr_t sprite_config_arg_count=0x008607E0U;
    constexpr uintptr_t sprite_config_arg_first=0x008607E8U;
    constexpr uint8_t sprite_load_bytes[]={
        0xa1,0xd8,0x2f,0x85,0x00,0x81,0xec,0xc4,0x00,0x00,0x00
    };
    constexpr uint8_t sprite_free_world_bytes[]={
        0x56,0x8b,0x74,0x24,0x08,0x8b,0x46,0x74
    };
    constexpr uint8_t sprite_clone_thing_bytes[]={
        0x83,0xec,0x0c,0x53,0x8b,0x5c,0x24,0x14
    };
    constexpr size_t native_sprite_entry_size=0xACU;
    using sprite_load_fn=void *(__cdecl *)(char const *);
    using sprite_free_world_fn=void (__cdecl *)(void *);
    using sprite_clone_thing_fn=void *(__cdecl *)(void *,void const *);
    using sprite_conf_open_fn=int (__cdecl *)(char const *);
    using sprite_conf_read_fn=int (__cdecl *)();
    using sprite_conf_close_fn=void (__cdecl *)();
    sprite_load_fn RealSithSpriteLoad=
        reinterpret_cast<sprite_load_fn>(sprite_load_addr);
    sprite_free_world_fn RealSithSpriteFreeWorld=
        reinterpret_cast<sprite_free_world_fn>(sprite_free_world_addr);
    sprite_clone_thing_fn RealSithThingClone=
        reinterpret_cast<sprite_clone_thing_fn>(sprite_clone_thing_addr);
    sprite_conf_open_fn SpriteConfigOpen=
        reinterpret_cast<sprite_conf_open_fn>(0x00431210U);
    sprite_conf_read_fn SpriteConfigRead=
        reinterpret_cast<sprite_conf_read_fn>(0x00431880U);
    sprite_conf_close_fn SpriteConfigClose=
        reinterpret_cast<sprite_conf_close_fn>(0x004315D0U);

    struct sprite_variant_group {
        void *world=nullptr;
        std::vector<std::unique_ptr<std::array<uint8_t,native_sprite_entry_size>>> owned;
        std::vector<void *> choices; // first = the normal world-owned sprite
    };
    std::unordered_map<void *,sprite_variant_group> sprite_variant_groups;
    std::unordered_map<void *,void *> sprite_variant_roots;
    std::mt19937 sprite_variant_rng{
        static_cast<uint32_t>(GetTickCount()^GetCurrentProcessId()^0xC053F123U)
    };

    bool read_sprite_variant_fields(jkgm::sprite_variant_spec &out)
    {
        if(!gpu_span_readable(reinterpret_cast<void const *>(
                sprite_config_arg_count),4))return false;
        uint32_t count=0;
        std::memcpy(&count,reinterpret_cast<void const *>(
                    sprite_config_arg_count),4);
        if(count<11 || count>64)return false;
        std::array<std::string,11> fields;
        for(size_t i=0;i<fields.size();++i) {
            uint32_t address=0;
            auto const ptr=sprite_config_arg_first+i*8U;
            if(!gpu_span_readable(reinterpret_cast<void const *>(ptr),4))
                return false;
            std::memcpy(&address,reinterpret_cast<void const *>(ptr),4);
            if(!gpu_span_readable(reinterpret_cast<void const *>(
                    uintptr_t(address)),128))
                return false;
            auto const *value=reinterpret_cast<char const *>(uintptr_t(address));
            size_t len=0;
            while(len<128 && value[len])++len;
            if(len==128)return false;
            fields[i].assign(value,len);
        }
        return jkgm::parse_sprite_variant(fields,out);
    }

    void release_sprite_variants(void *world)
    {
        for(auto it=sprite_variant_groups.begin();
            it!=sprite_variant_groups.end();) {
            if(world && it->second.world!=world) {++it;continue;}
            for(size_t n=1;n<it->second.choices.size();++n) {
                void *variant=it->second.choices[n];
                sprite_variant_roots.erase(variant);
                sprite_types.forget(variant);
                RealNativeSpriteFreeEntry(variant);
            }
            sprite_variant_roots.erase(it->first);
            it=sprite_variant_groups.erase(it);
        }
    }

    void *__cdecl HookedSithSpriteLoad(char const *name)
    {
        // Native Open/Close can reuse the conffile token backing 'name'.
        // Capture the request before letting the native parser run.
        std::string const sprite_name=name?name:"";
        void *base=RealSithSpriteLoad(name);
        if(!base || !name || sprite_variant_groups.count(base) ||
           !gpu_span_readable(base,native_sprite_entry_size))
            return base;
        // Missing names may resolve to default.spr. Do not manufacture
        // variants for an unrelated fallback resource.
        char const *loaded=static_cast<char const *>(base);
        if(_stricmp(loaded,sprite_name.c_str())!=0)return base;
        uint32_t world_address=0;
        if(!gpu_span_readable(reinterpret_cast<void const *>(
               sprite_world_loading_ptr),4))return base;
        std::memcpy(&world_address,reinterpret_cast<void const *>(
                    sprite_world_loading_ptr),4);
        if(!jkgm::decal_native_address_ok(world_address))return base;

        char path[160]{};
        int const n=std::snprintf(path,sizeof(path),"misc\\spr\\%s",sprite_name.c_str());
        if(n<=0 || static_cast<size_t>(n)>=sizeof(path) ||
           !SpriteConfigOpen(path))return base;
        std::vector<jkgm::sprite_variant_spec> specs;
        specs.reserve(8);
        constexpr size_t max_entries=64;
        bool malformed=false;
        while(SpriteConfigRead()) {
            jkgm::sprite_variant_spec entry{};
            if(!read_sprite_variant_fields(entry)) {
                malformed=true;
                break;
            }
            specs.emplace_back(std::move(entry));
            if(specs.size()>=max_entries)break;
        }
        SpriteConfigClose();
        if(malformed) {
            if(the_config && the_config->profile_renderer)
                LOG_WARNING("SPR multi-entry rejected invalid line: ",sprite_name);
            return base;
        }
        if(specs.size()<2)return base;

        sprite_variant_group group{};
        group.world=reinterpret_cast<void *>(uintptr_t(world_address));
        group.choices.push_back(base);
        // Native rdSprite_NewEntry loads shared MATs and owns its own
        // UV/face arrays. Every extra resource is created exactly once.
        for(size_t i=1;i<specs.size();++i) {
            auto entry=std::make_unique<
                std::array<uint8_t,native_sprite_entry_size>>();
            entry->fill(0);
            auto const &v=specs[i];
            float const offset[3]{v.offset[0],v.offset[1],v.offset[2]};
            if(!RealNativeSpriteNewEntry(entry->data(),sprite_name.c_str(),int(v.type),
                  v.material.c_str(),v.width,v.height,v.geometry,
                  v.lighting,v.texture,v.extra_light,offset)) {
                // Failed partial allocations must also be released.
                sprite_types.forget(entry->data());
                RealNativeSpriteFreeEntry(entry->data());
                continue;
            }
            // The trampoline bypasses HookedSpriteNewEntry, so manually
            // retain each alternative's authored mode for rendering.
            sprite_types.remember(entry->data(),v.type,
                                  reinterpret_cast<char const *>(entry->data()));
            group.choices.push_back(entry->data());
            group.owned.emplace_back(std::move(entry));
        }
        if(group.choices.size()<2)return base;
        sprite_variant_roots[base]=base;
        for(size_t i=1;i<group.choices.size();++i)
            sprite_variant_roots[group.choices[i]]=base;
        if(the_config && the_config->profile_renderer)
            LOG_INFO("SPR variants name=",sprite_name,
                     " entries=",group.choices.size(),
                     " shared_resources=1");
        sprite_variant_groups.emplace(base,std::move(group));
        return base;
    }

    void __cdecl HookedSithSpriteFreeWorld(void *world)
    {
        RealSithSpriteFreeWorld(world);
        release_sprite_variants(world);
    }

    void *__cdecl HookedSithThingClone(void *thing,void const *source)
    {
        void *result=RealSithThingClone(thing,source);
        if(!thing || !source || !gpu_span_readable(thing,0xCCU))
            return result;
        auto *rd=static_cast<uint8_t *>(thing)+0xC4U;
        uint32_t kind=0,source_sprite=0;
        std::memcpy(&kind,rd,4);
        if(kind!=4U)return result; // RD_THING_SPRITE3
        std::memcpy(&source_sprite,rd+4,4);
        auto const root=sprite_variant_roots.find(
            reinterpret_cast<void *>(uintptr_t(source_sprite)));
        if(root==sprite_variant_roots.end())return result;
        auto const group=sprite_variant_groups.find(root->second);
        if(group==sprite_variant_groups.end() ||
           group->second.choices.size()<2)return result;
        size_t const choice=jkgm::choose_sprite_variant(
            group->second.choices.size(),sprite_variant_rng);
        uint32_t const selected=static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(group->second.choices[choice]));
        std::memcpy(rd+4,&selected,4);
        return result;
    }


    int __cdecl HookedSpriteNewEntry(
        void *sprite,char const *name,int authored_type,char const *material,
        float width,float height,int geometry,int light,int tex,float extra,
        void const *offset)
    {
        int const ok=RealNativeSpriteNewEntry(
            sprite,name,authored_type,material,width,height,geometry,light,tex,
            extra,offset);
        if(ok && gpu_span_readable(sprite,32))
            sprite_types.remember(sprite,static_cast<uint32_t>(authored_type),
                                  static_cast<char const *>(sprite));
        else sprite_types.forget(sprite);
        return ok;
    }
    void __cdecl HookedSpriteFreeEntry(void *sprite)
    {
        sprite_types.forget(sprite);
        RealNativeSpriteFreeEntry(sprite);
    }
    void __cdecl HookedSpriteFree(void *sprite)
    {
        sprite_types.forget(sprite);
        RealNativeSpriteFree(sprite);
    }

    constexpr uint8_t sprite_draw_bytes[]={
        0xa1,0x18,0xa4,0x73,0x00,0x8b,0x4c,0x24,0x08
    };
    constexpr uint8_t sprite_preclip_bytes[]={
        0x8b,0x4c,0x24,0x10,0x89,0x46,0x10,0x85,0xc9
    };
    native_sprite_fn RealNativeSprite=
        reinterpret_cast<native_sprite_fn>(sprite_draw_addr);
    native_sprite_mid_fn RealSpritePreclip=
        reinterpret_cast<native_sprite_mid_fn>(sprite_preclip_addr);
    bool native_sprite_hook_active=false;
    jkgm::native_sprite_sample native_sprite_totals{};
    bool sprite_variant_hooks_active=false;
    bool sprite_frustum_hook_active=false;
    bool sprite_alpha_hook_active=false;
    using sprite_hw_upload_fn=int(__cdecl *)(void const *,uint32_t);
    sprite_hw_upload_fn RealSpriteHWUpload=
        reinterpret_cast<sprite_hw_upload_fn>(sprite_hw_upload_addr);
    struct sprite_flare_span {uint32_t start=0,count=0;};
    // Only used while the native render cache is converting a face list.
    // Reset when that vertex batch is submitted to D3D; no stale Thing refs.
    thread_local std::vector<sprite_flare_span> pending_sprite_flare_spans;
    // Bounded stage-by-stage flare diagnostics. Logging occurs only with
    // profile_renderer and only a handful of times for the process lifetime.
    // A successful DetourAttach is not evidence that a flare reaches OpenGL.
    unsigned flare_draw_trace_count=0;
    unsigned flare_face_trace_count=0;
    unsigned flare_alpha_trace_count=0;
    unsigned flare_flush_trace_count=0;
    unsigned flare_bad_face_trace_count=0;
    using sprite_addproc_fn=int(__cdecl *)(int,unsigned int,int);
    sprite_addproc_fn RealSpriteAddProc=
        reinterpret_cast<sprite_addproc_fn>(sprite_addproc_addr);
    native_sprite_mid_fn RealSpriteAlphaMid=
        reinterpret_cast<native_sprite_mid_fn>(sprite_alpha_mid_addr);
    using sprite_frustum_fn=int(__cdecl *)(void *,void const *,float);
    sprite_frustum_fn RealSpriteFrustum=
        reinterpret_cast<sprite_frustum_fn>(sprite_frustum_addr);
    void const *sprite_proc_entry(uint32_t index)
    {
        if(index>=1024U) return nullptr;
        auto const addr=sprite_proc_base_addr+
                        static_cast<uintptr_t>(index)*sprite_proc_stride;
        return reinterpret_cast<void const *>(addr);
    }

    // Level-global resources can be shared by many Things. In profiling mode,
    // report a few actual rendered instances per .SPR, not just definitions:
    // this distinguishes a type parsed as zero from Things with differing axes.
    std::unordered_map<std::string,int> sprite_trace_counts;
    void trace_sprite_skip(char const *name,uint32_t type,char const *reason)
    {
        if(!the_config || !the_config->profile_renderer ||
           sprite_trace_counts.size()>=128U) return;
        std::string const key(name);
        int &seen=sprite_trace_counts[key];
        if(seen++<3) LOG_WARNING("SPR pose name=",key," type=",type,
                                  " applied=no reason=",reason);
    }
    void trace_native_sprite(char const *name,uint32_t type,
                             jkgm::sprite_axes const &thing_axes,
                             jkgm::sprite_axes const &camera_axes,
                             jkgm::sprite_axes const &final_axes,
                             bool updated)
    {
        if(!the_config || !the_config->profile_renderer ||
           sprite_trace_counts.size()>=128U) return;
        std::string key(name);
        int &seen=sprite_trace_counts[key];
        if(seen++>=3) return;
        std::ostringstream entry;
        entry << "SPR pose name=" << key << " type=" << type
              << " mode=" << type
              << " applied=" << (updated ? "yes" : "no")
              << " thingUp=(" << thing_axes.up[0] << ","
                                  << thing_axes.up[1] << ","
                                  << thing_axes.up[2] << ")"
              << " cameraLook=(" << camera_axes.look[0] << ","
                                     << camera_axes.look[1] << ","
                                     << camera_axes.look[2] << ")"
              << " finalRight=(" << final_axes.right[0] << ","
                                     << final_axes.right[1] << ","
                                     << final_axes.right[2] << ")"
              << " finalUp=(" << final_axes.up[0] << ","
                                  << final_axes.up[1] << ","
                                  << final_axes.up[2] << ")";
        LOG_INFO(entry.str());
    }
    struct sprite_draw_context {
        void const *thing=nullptr;
        void const *orientation=nullptr;
    };
    // JK renders on its main thread. Recursion still preserves the outer call.
    thread_local sprite_draw_context sprite_draw_active{};
    thread_local bool suppress_native_decal_face=false;

    void transform_native_sprite_quad(float width,float height,float roll)
    {
        constexpr uintptr_t native_quad_addr=0x0082EA00U;
        if(width==1.0f && height==1.0f && roll==0.0f) return;
        if(!gpu_span_readable(reinterpret_cast<void const *>(native_quad_addr),
                              12*sizeof(float))) return;
        jkgm::transform_sprite_camera_quad(
            reinterpret_cast<float *>(native_quad_addr),width,height,roll);
    }

    jkgm::sprite_instance_info current_sprite_info(void const *rd_thing)
    {
        auto const *rd=static_cast<uint8_t const *>(rd_thing);
        if(!rd||!gpu_span_readable(rd,0x48)) return {};
        void const *thing=nullptr;
        std::memcpy(&thing,rd+0x44,sizeof(thing)); // rdThing.pThing
        if(!thing || !gpu_span_readable(thing,0x78)) return {};
        uint32_t signature=0;
        std::memcpy(&signature,static_cast<uint8_t const *>(thing)+0x74,4);
        return sprite_instances.sample(thing,signature,GetTickCount());
    }

    int __cdecl HookedSpriteFrustum(void *frustum,void const *center,float radius)
    {
        if(sprite_draw_active.thing && std::isfinite(radius) && radius>=0.0f) {
            auto const info=current_sprite_info(sprite_draw_active.thing);
            // Native radius is based on .SPR dimensions, not this instance.
            radius*=std::max(1.0f,std::max(info.width,info.height));
        }
        return RealSpriteFrustum(frustum,center,radius);
    }

    float current_sprite_roll(void const *rd_thing)
    {
        auto const *rd=static_cast<uint8_t const *>(rd_thing);
        if(!rd || !gpu_span_readable(rd,0x48)) return 0.0f;
        void const *thing=nullptr;
        std::memcpy(&thing,rd+0x44,sizeof(thing));
        if(!thing || !gpu_span_readable(thing,0x78)) return 0.0f;
        uint32_t signature=0;
        std::memcpy(&signature,static_cast<uint8_t const *>(thing)+0x74,4);
        return sprite_instances.sample_roll(thing,signature,GetTickCount());
    }

    // Called from a naked, register/FPU-preserving mid-function Detours thunk.
    // The original native quad is in this JK23-specific fixed buffer.
    void __cdecl update_native_sprite_vertices()
    {
        auto const *rd_thing=static_cast<uint8_t const *>(sprite_draw_active.thing);
        if(!rd_thing || !sprite_draw_active.orientation ||
           !gpu_span_readable(rd_thing,8) ||
           !gpu_span_readable(sprite_draw_active.orientation,12*sizeof(float)))
            return;
        void const *sprite=nullptr;
        std::memcpy(&sprite,rd_thing+4,sizeof(sprite));
        if(!gpu_span_readable(sprite,0xACU)) return;
        auto const *p=static_cast<uint8_t const *>(sprite);
        // +0x20 holds the world SPR index, not the authored type by draw.
        char sprite_name[33]{};
        std::memcpy(sprite_name,p,32);
        sprite_name[32]=0;
        auto const info=current_sprite_info(rd_thing);
        float const roll=current_sprite_roll(rd_thing);
        uint32_t type=0;
        if(!sprite_types.lookup(sprite,reinterpret_cast<char const *>(p),type)) {
            if(the_config && the_config->profile_renderer &&
               sprite_trace_counts.size()<128U) {
                std::string const key(sprite_name);
                int &seen=sprite_trace_counts[key];
                if(seen++==0) LOG_WARNING(
                    "SPR pose name=",key," has no captured authored type; native fallback");
            }
            transform_native_sprite_quad(info.width,info.height,roll);
            return;
        }
        if(!jkgm::sprite_needs_custom_axes(type)) {
            if(the_config && the_config->profile_renderer &&
               sprite_trace_counts.size()<128U) {
                auto const key=std::string(sprite_name);
                int &seen=sprite_trace_counts[key];
                if(seen++==0) LOG_INFO("SPR pose name=",key,
                    " loaded_type=",type,
                    " (native camera-facing mode)");
            }
            transform_native_sprite_quad(info.width,info.height,roll);
            return;
        }

        float size[4]{}; // width, height, widthHalf, heightHalf
        float offset[3]{};
        std::memcpy(size,p+0x4CU,sizeof(size));
        std::memcpy(offset,p+0xA0U,sizeof(offset));
        if(!std::isfinite(size[2])||!std::isfinite(size[3]) ||
           size[2]<=0.0f||size[3]<=0.0f ||
           size[2]>8192.0f||size[3]>8192.0f)
        {
            trace_sprite_skip(sprite_name,type,"invalid width or height");
            return;
        }
        for(float v:offset) if(!std::isfinite(v)) {
            trace_sprite_skip(sprite_name,type,"invalid local offset");
            return;
        }

        jkgm::gpu_3do_matrix34 thing{},view{};
        std::memcpy(thing.data(),sprite_draw_active.orientation,
                    thing.size()*sizeof(float));
        if(!jkgm::gpu_3do_matrix_finite(thing)) {
            trace_sprite_skip(sprite_name,type,"nonfinite Thing matrix");
            return;
        }
        constexpr uintptr_t native_camera_ptr_addr=0x0073A418U;
        if(!gpu_span_readable(reinterpret_cast<void const *>(
                    native_camera_ptr_addr),sizeof(void *))) {
            trace_sprite_skip(sprite_name,type,"native camera pointer unreadable");
            return;
        }
        void const *camera=nullptr;
        std::memcpy(&camera,reinterpret_cast<void const *>(
                    native_camera_ptr_addr),sizeof(camera));
        if(!gpu_span_readable(camera,8+view.size()*sizeof(float))) {
            trace_sprite_skip(sprite_name,type,"camera matrix unreadable");
            return;
        }
        std::memcpy(view.data(),static_cast<uint8_t const *>(camera)+8,
                    view.size()*sizeof(float));
        if(!jkgm::gpu_3do_matrix_finite(view)) {
            trace_sprite_skip(sprite_name,type,"nonfinite camera matrix");
            return;
        }

        // Diagnostic receiver discovery, one time per world/resource in
        // renderer profiling builds only. The native world/surface layout
        // beyond the validated header must not be used for rendering until
        // this reports plausible counts for real maps.
        if(type==jkgm::sprite_project_decal && the_config &&
           the_config->profile_renderer) {
            auto const read=[](uintptr_t address,void *dst,size_t bytes)->bool {
                if(!gpu_span_readable(reinterpret_cast<void const *>(address),bytes))
                    return false;
                std::memcpy(dst,reinterpret_cast<void const *>(address),bytes);
                return true;
            };
            uint32_t world_address=0;
            if(read(jkgm::engine_material_debug::current_world_global,
                    &world_address,sizeof(world_address)) &&
               world_address>=0x10000U) {
                uint64_t const key=(uint64_t(world_address)<<32) |
                    uint32_t(reinterpret_cast<uintptr_t>(sprite));
                if(sprite_decal_diagnostic_keys.size()<1024U &&
                   sprite_decal_diagnostic_keys.insert(key).second) {
                    jkgm::sprite_axes const axes{
                        {{thing[0],thing[1],thing[2]}},
                        {{thing[3],thing[4],thing[5]}},
                        {{thing[6],thing[7],thing[8]}}
                    };
                    jkgm::decal_projector projector{};
                    bool const valid=jkgm::make_sprite_decal_projector(
                        axes,{{thing[9],thing[10],thing[11]}},
                        {{offset[0],offset[1],offset[2]}},
                        size[0],size[1],info.width,info.height,roll,
                        projector);
                    if(!valid) {
                        LOG_WARNING("SPR decal probe name=",sprite_name,
                                    " failed to build projector");
                    } else {
                        auto const scan=jkgm::project_decal_world_surfaces(
                            read,world_address,projector,2048U);
                        jkgm::sprite_decal_resource resource{};
                        bool const resource_ok=jkgm::read_sprite_decal_resource(
                            read,reinterpret_cast<uintptr_t>(sprite),resource);
                        jkgm::sprite_decal_mesh prepared{};
                        bool const mesh_ready=resource_ok &&
                            jkgm::make_sprite_decal_gpu_mesh(
                                scan,projector,resource.material,
                                resource.texcoords,info.opacity,prepared);
                        LOG_INFO("SPR decal probe name=",sprite_name,
                                 " world=",world_address,
                                 " valid_world=",scan.valid_world,
                                 " surfaces=",scan.surfaces_seen,
                                 " receivers=",scan.receivers,
                                 " clipped_tris=",scan.triangles.size(),
                                 " malformed=",scan.invalid_surfaces,
                                 " skipped_portals=",scan.skipped_portals,
                                 " skipped_sky=",scan.skipped_sky,
                                 " skipped_material=",scan.skipped_material,
                                 " skipped_backfaces=",scan.skipped_backfaces,
                                 " missed_volume=",scan.skipped_projection,
                                 " truncated=",scan.truncated,
                                 " mat_valid=",resource_ok,
                                 " gpu_mesh_ready=",mesh_ready);
                    }
                }
            }
        }

        // Invert only the orthogonal camera basis to recover WORLD axes.
        // Sith rdMatrix34 is {Right,Look,Up,Translation}, each a vec3.
        jkgm::sprite_axes thing_axes{
            {{thing[0],thing[1],thing[2]}},
            {{thing[3],thing[4],thing[5]}},
            {{thing[6],thing[7],thing[8]}}
        };
        jkgm::sprite_axes camera_axes{
            {{view[0],view[3],view[6]}},
            {{view[1],view[4],view[7]}},
            {{view[2],view[5],view[8]}}
        };
        jkgm::sprite_axes final_axes{};
        if(!jkgm::compute_sprite_axes(type,thing_axes,camera_axes,final_axes)) {
            trace_sprite_skip(sprite_name,type,"degenerate Thing orientation axes");
            return;
        }

        for(int axis=0;axis<3;axis++) {
            thing[axis]=final_axes.right[axis];
            thing[axis+3]=final_axes.look[axis];
            thing[axis+6]=final_axes.up[axis];
        }
        auto const modelview=jkgm::gpu_3do_native_modelview(view,thing);
        if(!jkgm::gpu_3do_matrix_finite(modelview)) {
            trace_sprite_skip(sprite_name,type,"nonfinite modelview");
            return;
        }
        // Native sprite buffer at 0x82EA00: four float3s, camera-space.
        constexpr uintptr_t native_quad_addr=0x0082EA00U;
        if(!gpu_span_readable(reinterpret_cast<void const *>(native_quad_addr),
                              12*sizeof(float))) {
            trace_sprite_skip(sprite_name,type,"quad buffer unreadable");
            return;
        }
        float transformed[12]{};
        for(int i=0;i<4;i++) {
            float const x=offset[0]+((i==1||i==2)?size[2]:-size[2]);
            float const z=offset[2]+((i>=2)?size[3]:-size[3]);
            auto const v=jkgm::gpu_3do_native_transform_point(
                modelview,{{x,offset[1],z}});
            for(int j=0;j<3;j++) {
                if(!std::isfinite(v[j])) return;
                transformed[i*3+j]=v[j];
            }
        }
        std::memcpy(reinterpret_cast<void *>(native_quad_addr),transformed,
                    sizeof(transformed));
        transform_native_sprite_quad(info.width,info.height,roll);
        trace_native_sprite(sprite_name,type,thing_axes,camera_axes,final_axes,true);
    }

    // Detours preserves the displaced mid-function instructions in the
    // trampoline. Do not return: jump back with the original register state.
    __declspec(naked) void __cdecl SpritePreclipThunk()
    {
        __asm {
            pushfd
            pushad
            call update_native_sprite_vertices
            popad
            popfd
            jmp dword ptr [RealSpritePreclip]
        }
    }

    int __cdecl HookedSpriteAddProc(int extra,unsigned int vertices,int flags)
    {
        // Type-3 fallback must still append its native proc entry: JK's
        // rdCache processes that face to load its MAT cel into video memory.
        // Earlier returning 1 here suppressed BOTH the visible quad AND MAT
        // warm-up, leaving every GPU decal forever waiting for its texture.
        // Tag the face with zero alpha instead, so cache/texture upload still
        // run while the native free-standing sprite quad is invisible.
        bool const is_sprite=sprite_draw_active.thing!=nullptr;
        bool const hide_decal_face=is_sprite && extra==0 &&
                                   suppress_native_decal_face;
        float const opacity=hide_decal_face?0.0f:
            (is_sprite?current_sprite_info(sprite_draw_active.thing).opacity:1.0f);
        uint32_t const before=*reinterpret_cast<uint32_t const *>(
            sprite_proc_count_addr);
        int const result=RealSpriteAddProc(extra,vertices,flags);
        if(result!=0 && is_sprite && extra==0 &&
           the_config && the_config->profile_renderer)
            ++native_sprite_totals.native_proc_faces;
        if(result==0 || !is_sprite || extra!=0) return result;
        void const *sprite=nullptr;
        std::memcpy(&sprite,static_cast<uint8_t const *>(
            sprite_draw_active.thing)+4,sizeof(sprite));
        uint32_t authored_type=0;
        bool const is_flare=gpu_span_readable(sprite,32) &&
            sprite_types.lookup(sprite,static_cast<char const *>(sprite),
                                authored_type) &&
            jkgm::sprite_is_flare(authored_type);
        if(!hide_decal_face && !is_flare &&
           !(opacity>0.0f && opacity<1.0f))
            return result;
        if(is_flare && the_config && the_config->profile_renderer &&
           flare_face_trace_count++<6U) {
            std::ostringstream msg;
            msg<<"SPR flare stage=face_append before="<<before
               <<" after_candidate="<<*reinterpret_cast<uint32_t const *>(
                   sprite_proc_count_addr)
               <<" extra="<<extra<<" opacity="<<opacity
               <<" authored_type="<<authored_type;
            LOG_INFO(msg.str());
        }
        uint32_t const after=*reinterpret_cast<uint32_t const *>(
            sprite_proc_count_addr);
        if(after!=before+1U) return result;
        auto *entry=const_cast<uint8_t *>(static_cast<uint8_t const *>(
            sprite_proc_entry(before)));
        if(!entry || !gpu_span_readable(entry,sprite_proc_stride)) return result;
        // The first word is rdProcEntry.extraData, +4 its native face flags.
        // ExtraData is only used for line colors in this cache path.
        uint32_t face_type=0;
        std::memcpy(&face_type,entry+4,4);
        uint32_t const geometry_mode=*reinterpret_cast<uint32_t const *>(
            entry+0x08); // rdProcEntry.geometryMode
        if(geometry_mode<2U) {
            if(is_flare && the_config && the_config->profile_renderer &&
               flare_bad_face_trace_count++<4U)
                LOG_WARNING("SPR flare stage=face_rejected geomode=",
                            geometry_mode," face_type=",face_type);
            return result;
        }
        uint32_t alpha_tag=0;
        if(!jkgm::sprite_alpha_encode(
               opacity,(face_type&2U)!=0U,alpha_tag,is_flare)) return result;
        std::memcpy(entry,&alpha_tag,4);
        face_type|=2U; // Force native translucent render state, without MAT mutation.
        std::memcpy(entry+4,&face_type,4);
        return result;
    }

    int __cdecl SpriteFaceAlpha(void const *face,int native_alpha)
    {
        if(!face || !gpu_span_readable(face,4)) return -1;
        uint32_t tag=0;
        std::memcpy(&tag,face,4);
        if((tag&jkgm::sprite_alpha_tag_mask)==jkgm::sprite_alpha_tag_magic &&
           (tag&jkgm::sprite_alpha_flare)!=0U &&
           pending_sprite_flare_spans.size()<256U) {
            // The converter has already cleared/routed a full vertex batch
            // before reaching this face, so totalVerts is its starting index.
            uint32_t n=0,start=0;
            std::memcpy(&n,static_cast<uint8_t const *>(face)+0x1c,4);
            std::memcpy(&start,reinterpret_cast<void const *>(
                sprite_hw_vertex_count_addr),4);
            if(the_config && the_config->profile_renderer &&
               flare_alpha_trace_count++<6U)
                LOG_INFO("SPR flare stage=alpha_converter num_vertices=",n,
                         " first_hw_vertex=",start," tag=",tag,
                         " accepted=",n>=3U && n<=32U && start+n<=4096U);
            if(n>=3U && n<=32U && start+n<=4096U)
                pending_sprite_flare_spans.push_back({start,n});
        }
        return jkgm::sprite_alpha_override(tag,native_alpha);
    }

    // The compiler inlines the cache-flush function in the ordinary render
    // path. Tag the native D3DTLVERTEX array at its *actual upload*, just
    // before std3D copies it into its render list. This also handles
    // buffer-capacity flushes in the middle of a proc-face sequence.
    int __cdecl HookedSpriteHWUpload(void const *vertices,uint32_t count)
    {
        if(vertices==reinterpret_cast<void const *>(sprite_hw_buffer_addr) &&
           !pending_sprite_flare_spans.empty()) {
            uint32_t const native_count=*reinterpret_cast<uint32_t const *>(
                sprite_hw_vertex_count_addr);
            if(the_config && the_config->profile_renderer &&
               flare_flush_trace_count++<6U)
                LOG_INFO("SPR flare stage=hw_upload vertex_count=",count,
                         " native_count=",native_count,
                         " pending_spans=",pending_sprite_flare_spans.size(),
                         " first_span_start=",pending_sprite_flare_spans.front().start,
                         " first_span_count=",pending_sprite_flare_spans.front().count);
            if(count<=4096U && native_count==count &&
               gpu_span_readable(vertices,uintptr_t(count)*32U)) {
                for(auto const &span:pending_sprite_flare_spans) {
                    if(span.start+span.count>count) continue;
                    for(uint32_t i=span.start;i<span.start+span.count;i++) {
                        auto *specular=reinterpret_cast<uint8_t *>(
                            const_cast<void *>(vertices))+uintptr_t(i)*32U+20U;
                        uint32_t const marker=sprite_flare_vertex_magic;
                        std::memcpy(specular,&marker,4);
                    }
                }
            }
            pending_sprite_flare_spans.clear();
        }
        return RealSpriteHWUpload(vertices,count);
    }

    // At 0x43b6f9 ESI points to the current rdProcEntry and ECX is the
    // alpha just selected by native JK. Set saved ECX and [EBP-8] before
    // the displaced CMP executes. All other CPU state is preserved.
    __declspec(naked) void __cdecl SpriteAlphaThunk()
    {
        __asm {
            pushfd
            // Cheap reject for ordinary world/model/particle faces. Only a
            // tagged SPR enters the C++ helper, avoiding per-face call cost.
            push eax
            mov eax,dword ptr [esi]
            and eax,0xfffffc00
            cmp eax,0x51a70000
            pop eax
            jne native_alpha
            pushad
            push ecx
            push esi
            call SpriteFaceAlpha
            add esp,8
            cmp eax,-1
            je no_override
            mov dword ptr [ebp-8],eax
            mov dword ptr [esp+24],eax
        no_override:
            popad
        native_alpha:
            popfd
            jmp dword ptr [RealSpriteAlphaMid]
        }
    }

    // Type-3 SPR: transform an ordinary SPR Thing into projected geometry
    // over actual level architecture, preserving the original loaded MAT.
    // Static receivers are cached per Thing/world/projector to avoid a full
    // level-surface scan on every physics/render frame.
    struct cached_runtime_sprite_decal {
        uint32_t world=0;
        uintptr_t resource=0;
        uintptr_t material=0;
        uint32_t thing_signature=0;
        uint64_t object_revision=0;
        std::array<float,12> matrix{};
        bool attached_to_face=false;
        jkgm::decal_vec3 attached_normal{};
        std::array<float,10> settings{}; // size(4), offset(3), scale(2), roll
        uint64_t id=0;
        uint64_t last_seen_frame=0;
        std::shared_ptr<jkgm::gpu_3do_geometry const> geometry;
    };
    std::unordered_map<uint64_t,cached_runtime_sprite_decal> sprite_gpu_decals;
    jkgm::decal_world_geometry sprite_world_geometry;
    unsigned sprite_decal_submit_logs=0;
    unsigned sprite_decal_queued_logs=0;
    unsigned sprite_decal_material_logs=0;

    struct static_sprite_decal_mesh {
        uint32_t world=0,signature=0;
        std::array<float,12> transform{};
        std::vector<jkgm::decal_world_surface_geometry> receivers;
    };
    std::unordered_map<uint64_t,static_sprite_decal_mesh> sprite_static_meshes;
    jkgm::decal_world_geometry sprite_static_receiver_world;
    uint32_t sprite_static_world=0;
    uint64_t sprite_static_revision=1;
    bool sprite_static_receivers_dirty=true;
    unsigned sprite_static_mesh_logs=0;
    unsigned sprite_static_projection_logs=0;

    void reset_static_sprite_decal_meshes(uint32_t world)
    {
        sprite_static_meshes.clear();
        sprite_static_receiver_world.clear();
        sprite_static_projection_logs=0;
        sprite_static_mesh_logs=0;
        sprite_static_world=world;
        sprite_static_receivers_dirty=true;
        ++sprite_static_revision;
    }

    // rdModel3_DrawMesh supplies the composed WORLD joint transform.
    // Only stationary collide=3 Things are eligible for long-lived caching.
    void capture_static_sprite_decal_mesh(void *mesh,void *placement)
    {
        if(!the_config || !the_config->enable_decals ||
           !mesh || !placement)
            return;
        constexpr uintptr_t current_rdmodel_thing=0x0073C320U;
        if(!gpu_span_readable(reinterpret_cast<void const *>(
                                  current_rdmodel_thing),4))return;
        uint32_t rd=0;
        std::memcpy(&rd,reinterpret_cast<void const *>(current_rdmodel_thing),4);
        if(!gpu_span_readable(reinterpret_cast<void const *>(rd),0x48U))
            return;
        uint32_t rd_type=0,owner=0;
        std::memcpy(&rd_type,reinterpret_cast<void const *>(rd),4);
        std::memcpy(&owner,reinterpret_cast<void const *>(rd+0x44U),4);
        if(rd_type!=1 || !jkgm::decal_native_address_ok(owner) ||
           !gpu_span_readable(reinterpret_cast<void const *>(owner),0xc8U) ||
           rd!=owner+0xc4U)return;
        uint32_t type=0,move_type=0,collision=0,signature=0;
        std::memcpy(&type,reinterpret_cast<void const *>(owner+0x0cU),4);
        std::memcpy(&move_type,reinterpret_cast<void const *>(owner+0x10U),4);
        std::memcpy(&collision,reinterpret_cast<void const *>(owner+0x28U),4);
        std::memcpy(&signature,reinterpret_cast<void const *>(owner+0x74U),4);
        if(!type || move_type!=0 || collision!=3)return;
        constexpr uintptr_t world_ptr=
            jkgm::engine_material_debug::current_world_global;
        if(!gpu_span_readable(reinterpret_cast<void const *>(world_ptr),4))
            return;
        uint32_t world=0;
        std::memcpy(&world,reinterpret_cast<void const *>(world_ptr),4);
        if(!jkgm::decal_native_address_ok(world))return;
        if(sprite_static_world!=world)
            reset_static_sprite_decal_meshes(world);
        // Mesh header is read only on a miss. Don't validate the mesh
        // allocation merely to locate an existing receiver cache entry.
        // For nonstatic Things avoid validating the placement altogether.
        if(!gpu_span_readable(placement,12*sizeof(float)))return;
        jkgm::gpu_3do_matrix34 transform{};
        std::memcpy(transform.data(),placement,sizeof(transform));
        if(!jkgm::gpu_3do_matrix_finite(transform))return;
        // Static architecture normally keeps the same Thing, mesh and
        // transform for thousands of frames. Check the receiver cache
        // BEFORE traversing its vertex/face memory. In a cache hit these
        // expensive VirtualQuery validations and buffer reads are redundant.
        // The existing world/signature/transform checks are unchanged.
        uint64_t const key=(uint64_t(owner)<<32) |
            uint32_t(reinterpret_cast<uintptr_t>(mesh));
        auto existing=sprite_static_meshes.find(key);
        if(existing!=sprite_static_meshes.end() &&
           existing->second.world==world &&
           existing->second.signature==signature &&
           existing->second.transform==transform)return;
        if(existing==sprite_static_meshes.end() &&
           sprite_static_meshes.size()>=4096U)return;
        if(!gpu_span_readable(mesh,sizeof(native_mesh)))return;
        native_mesh source{};
        std::memcpy(&source,mesh,sizeof(source));
        if(source.num_faces<1 || source.num_faces>8192 ||
           source.num_vertices<3 || source.num_vertices>65536 ||
           !gpu_span_readable(source.vertices,
                     size_t(source.num_vertices)*3U*sizeof(float)) ||
           !gpu_span_readable(source.faces,
                     size_t(source.num_faces)*sizeof(native_face)))return;
        static_sprite_decal_mesh updated{};
        updated.world=world;
        updated.signature=signature;
        updated.transform=transform;
        updated.receivers.reserve(static_cast<size_t>(source.num_faces));
        for(int fi=0;fi<source.num_faces;++fi) {
            native_face face{};
            std::memcpy(&face,source.faces+size_t(fi)*sizeof(face),
                        sizeof(face));
            if(face.geo<2 || face.count<3 || face.count>128 ||
               !gpu_span_readable(face.pos_indices,
                                  size_t(face.count)*sizeof(int)))
                continue;
            std::vector<jkgm::decal_vec3> local;
            local.reserve(face.count);
            bool valid=true;
            for(unsigned vi=0;vi<face.count;++vi) {
                int idx=0;
                std::memcpy(&idx,face.pos_indices+vi,sizeof(idx));
                if(idx<0 || idx>=source.num_vertices) {
                    valid=false;break;
                }
                jkgm::decal_vec3 v{};
                std::memcpy(v.data(),source.vertices+size_t(idx)*3U,
                            3U*sizeof(float));
                if(!jkgm::decal_finite(v)) {valid=false;break;}
                local.push_back(v);
            }
            if(!valid)continue;
            jkgm::decal_world_surface_geometry receiver{};
            if(jkgm::build_static_sprite_receiver(
                    transform,local,uint32_t(fi),receiver))
                updated.receivers.push_back(std::move(receiver));
        }
        if(existing!=sprite_static_meshes.end())
            sprite_static_meshes.erase(existing);
        if(!updated.receivers.empty())
            sprite_static_meshes.emplace(key,std::move(updated));
        sprite_static_receivers_dirty=true;
        ++sprite_static_revision;
        if(the_config->profile_renderer &&
           sprite_static_mesh_logs++<12U)
            LOG_INFO("SPR decal static3do captured thing=",owner,
                     " mesh=",reinterpret_cast<uintptr_t>(mesh),
                     " faces=",source.num_faces);
    }

    void rebuild_static_sprite_decal_receivers(uint32_t world)
    {
        if(sprite_static_world!=world)
            reset_static_sprite_decal_meshes(world);
        if(!sprite_static_receivers_dirty)return;
        sprite_static_receiver_world.clear();
        sprite_static_receiver_world.world=world;
        constexpr size_t limit=100000U;
        for(auto const &item:sprite_static_meshes) {
            if(item.second.world!=world)continue;
            if(item.second.receivers.size()>
               limit-sprite_static_receiver_world.receivers.size())
                break;
            for(auto const &receiver:item.second.receivers)
                sprite_static_receiver_world.receivers.push_back(receiver);
        }
        sprite_static_receiver_world.surface_count=static_cast<uint32_t>(
            sprite_static_receiver_world.receivers.size());
        sprite_static_receiver_world.valid_world=true;
        sprite_static_receiver_world.acceleration.build(
            sprite_static_receiver_world.receivers);
        sprite_static_receivers_dirty=false;
    }

    bool submit_native_sprite_decal(
        void const *rd_thing,void const *orientation,
        jkgm::sprite_instance_info const &info)
    {
        if(!the_config || !the_config->enable_decals ||
           !rd_thing || !orientation ||
           !gpu_span_readable(rd_thing,8) ||
           !gpu_span_readable(orientation,12*sizeof(float)))
            return false;
        uintptr_t sprite=0;
        std::memcpy(&sprite,static_cast<uint8_t const *>(rd_thing)+4,4);
        uint32_t authored_type=0;
        if(!gpu_span_readable(reinterpret_cast<void const *>(sprite),0xa4) ||
           !sprite_types.lookup(reinterpret_cast<void const *>(sprite),
                               reinterpret_cast<char const *>(sprite),
                               authored_type) ||
           authored_type!=jkgm::sprite_project_decal)
            return false;
        const auto read=[](uintptr_t at,void *dst,size_t n)->bool {
            if(!gpu_span_readable(reinterpret_cast<void const *>(at),n))
                return false;
            std::memcpy(dst,reinterpret_cast<void const *>(at),n);
            return true;
        };
        uint32_t world=0;
        if(!read(jkgm::engine_material_debug::current_world_global,
                 &world,4) || !jkgm::decal_native_address_ok(world))
            return false;
        std::array<float,12> m{};
        std::memcpy(m.data(),orientation,sizeof(m));
        if(!jkgm::gpu_3do_matrix_finite(m))return false;
        float size[4]{},offset[3]{};
        if(!read(sprite+0x4c,size,sizeof(size)) ||
           !read(sprite+0xa0,offset,sizeof(offset)))
            return false;
        float const roll=current_sprite_roll(rd_thing);
        std::array<float,10> const settings{{
            size[0],size[1],size[2],size[3],
            offset[0],offset[1],offset[2],
            info.width,info.height,roll
        }};
        jkgm::sprite_decal_resource resource{};
        if(!jkgm::read_sprite_decal_resource(read,sprite,resource))
            return false;
        jkgm::decal_vec3 attached_normal{};
        bool attached_to_face=false;
        // rdThing.pThing lives at +0x44. This is the same native
        // attachment data already used by GetThingAttachNormal().
        uint32_t owner=0,thing_signature=0;
        if(read(reinterpret_cast<uintptr_t>(rd_thing)+0x44,&owner,4) &&
           jkgm::decal_native_address_ok(owner)) {
            read(uintptr_t(owner)+0x74,&thing_signature,4);
            attached_to_face=jkgm::read_thing_attachment_normal(
                read,owner,attached_normal);
        }
        uint64_t const key=(uint64_t(world)<<32) |
            uint32_t(reinterpret_cast<uintptr_t>(rd_thing));
        auto it=sprite_gpu_decals.find(key);
        bool const hit=it!=sprite_gpu_decals.end() &&
            it->second.object_revision==sprite_static_revision &&
            it->second.world==world && it->second.resource==sprite &&
            it->second.material==resource.material &&
            it->second.thing_signature==thing_signature &&
            it->second.settings==settings &&
            it->second.attached_to_face==attached_to_face &&
            jkgm::sprite_decal_pose_stable(
                it->second.matrix,m,attached_to_face) &&
            (!attached_to_face ||
             jkgm::sprite_decal_normals_stable(
                 it->second.attached_normal,attached_normal));
        if(!hit) {
            // Surface normals are needed BEFORE constructing the projector.
            // Unattached impact sprites generally retain UP=(0,0,1), even
            // when created from a projectile hitting a vertical wall.
            if(!sprite_world_geometry.matches(read,world)) {
                auto const begin=std::chrono::steady_clock::now();
                bool const ok=sprite_world_geometry.load(read,world);
                if(the_config->profile_renderer) {
                    auto const micros=std::chrono::duration_cast<
                        std::chrono::microseconds>(
                            std::chrono::steady_clock::now()-begin).count();
                    std::ostringstream msg;
                    msg<<"SPR decal receiver cache loaded="<<ok
                       <<" surfaces="<<sprite_world_geometry.surface_count
                       <<" eligible="<<sprite_world_geometry.receivers.size()
                       <<" build_us="<<micros;
                    LOG_INFO(msg.str());
                }
                if(!ok)return false;
            }
            rebuild_static_sprite_decal_receivers(world);
            jkgm::sprite_axes axes{
                {{m[0],m[1],m[2]}},{{m[3],m[4],m[5]}},
                {{m[6],m[7],m[8]}}
            };
            jkgm::decal_vec3 chosen_normal=attached_normal;
            bool aligned_to_receiver=attached_to_face;
            if(!attached_to_face) {
                jkgm::decal_vec3 position{{m[9],m[10],m[11]}};
                // The sprite resource's offset is local to its Thing.
                for(int i=0;i<3;++i)
                    position[i]+=offset[0]*axes.right[i]+
                                 offset[1]*axes.look[i]+
                                 offset[2]*axes.up[i];
                auto const camera=jkgm::read_active_render_camera();
                jkgm::decal_vec3 const incoming=camera.valid
                    ?camera.forward:jkgm::decal_vec3{{0,0,0}};
                auto const nearby=jkgm::find_nearest_decal_surface(
                    sprite_world_geometry,sprite_static_receiver_world,
                    position,incoming);
                if(nearby.valid) {
                    chosen_normal=nearby.normal;
                    aligned_to_receiver=true;
                    if(the_config->profile_renderer &&
                       sprite_decal_submit_logs<12U) {
                        LOG_INFO("SPR decal autoalign normal=(",
                                 std::to_string(chosen_normal[0]),",",
                                 std::to_string(chosen_normal[1]),",",
                                 std::to_string(chosen_normal[2]),
                                 ") proximity_score=",
                                 std::to_string(nearby.distance));
                    }
                }
            }
            if(aligned_to_receiver) {
                jkgm::sprite_axes aligned{};
                if(!jkgm::sprite_decal_attachment_axes(
                       axes,chosen_normal,aligned))return false;
                axes=aligned;
            }
            jkgm::decal_projector projector{};
            if(!jkgm::make_sprite_decal_projector(
                axes,{{m[9],m[10],m[11]}},
                {{offset[0],offset[1],offset[2]}},
                size[0],size[1],info.width,info.height,roll,projector))
                return false;
            auto scan=jkgm::project_cached_decal_world(
                sprite_world_geometry,projector,2048U);
            uint32_t object_triangles=0,object_receivers=0;
            if(!sprite_static_receiver_world.receivers.empty() &&
               !scan.truncated && scan.triangles.size()<2048U) {
                auto objects=jkgm::project_cached_decal_world(
                    sprite_static_receiver_world,projector,
                    2048U-static_cast<uint32_t>(scan.triangles.size()));
                object_triangles=static_cast<uint32_t>(objects.triangles.size());
                object_receivers=objects.receivers;
                scan.truncated|=objects.truncated;
                scan.receivers+=objects.receivers;
                scan.broadphase_candidates+=objects.broadphase_candidates;
                scan.triangles.insert(scan.triangles.end(),
                    objects.triangles.begin(),objects.triangles.end());
            }
            if(the_config->profile_renderer &&
               sprite_static_projection_logs++<16U)
                LOG_INFO("SPR decal static3do projection meshes=",
                         sprite_static_meshes.size(),
                         " faces=",sprite_static_receiver_world.receivers.size(),
                         " clipped_tris=",object_triangles,
                         " clipped_faces=",object_receivers);
            jkgm::sprite_decal_mesh mesh{};
            bool const ready=jkgm::make_sprite_decal_gpu_mesh(
                scan,projector,resource.material,resource.texcoords,
                info.opacity,mesh);
            if(!ready && the_config->profile_renderer &&
               sprite_decal_submit_logs++<12U) {
                std::ostringstream msg;
                msg<<"SPR decal GPU stage=no_receiver"
                   <<" attached="<<attached_to_face
                   <<" valid_world="<<scan.valid_world
                   <<" surfaces="<<scan.surfaces_seen
                   <<" receivers="<<scan.receivers
                   <<" broadphase="<<scan.broadphase_candidates
                   <<" missed_volume="<<scan.skipped_projection
                   <<" backfaces="<<scan.skipped_backfaces
                   <<" material="<<scan.skipped_material;
                LOG_INFO(msg.str());
            }
            // Cache definitive no-receiver results, too. Otherwise an
            // attached static blood splat can rescan EVERY world polygon on
            // every frame forever. Moving/reoriented/attached Things still
            // invalidate the key, so impact recomputes the projected shape.
            if(!ready && (!scan.valid_world || scan.truncated ||
                          !scan.triangles.empty()))
                return false;
            cached_runtime_sprite_decal entry{};
            entry.world=world;
            entry.resource=sprite;
            entry.material=resource.material;
            entry.thing_signature=thing_signature;
            entry.object_revision=sprite_static_revision;
            entry.matrix=m;
            entry.attached_to_face=attached_to_face;
            entry.attached_normal=attached_normal;
            entry.settings=settings;
            entry.id=ready?next_gpu_id++:0;
            entry.geometry=ready?std::move(mesh.geometry):nullptr;
            // Do not discard EVERY blood splat when crossing the cache
            // limit: that previously caused repeated projection and GPU
            // uploads for all still-visible decals after the 128th entry.
            // Prefer stale-world entries, otherwise evict only the LRU.
            constexpr size_t decal_cache_limit=1024U;
            if(it==sprite_gpu_decals.end() &&
               sprite_gpu_decals.size()>=decal_cache_limit) {
                auto victim=sprite_gpu_decals.begin();
                for(auto candidate=sprite_gpu_decals.begin();
                    candidate!=sprite_gpu_decals.end();++candidate) {
                    if(candidate->second.world!=world) {
                        victim=candidate;
                        break;
                    }
                    if(candidate->second.last_seen_frame<
                       victim->second.last_seen_frame)
                        victim=candidate;
                }
                sprite_gpu_decals.erase(victim);
            }
            it=sprite_gpu_decals.insert_or_assign(key,std::move(entry)).first;
            if(ready && the_config->profile_renderer &&
               sprite_decal_submit_logs++<12U)
                LOG_INFO("SPR decal GPU stage=geometry world=",world,
                         " triangles=",scan.triangles.size(),
                         " receivers=",scan.receivers,
                         " broadphase=",scan.broadphase_candidates,
                         " static3do_tris=",object_triangles,
                         " static3do_receivers=",object_receivers,
                         " attached=",attached_to_face,
                         " id=",it->second.id);
        }
        it->second.last_seen_frame=gpu_frame_serial;
        if(!it->second.geometry)return false; // Cached no-receiver.
        if(!gpu_mesh_materials_ready(*it->second.geometry)) {
            if(the_config->profile_renderer &&
               sprite_decal_material_logs++<12U)
                LOG_INFO("SPR decal GPU stage=mat_wait id=",it->second.id,
                         " native_proc_warmup=",
                         sprite_alpha_hook_active);
            return false;
        }
        if(the_config->profile_renderer &&
           sprite_decal_material_logs++<12U)
            LOG_INFO("SPR decal GPU stage=mat_ready id=",it->second.id);
        jkgm::gpu_3do_instance inst{};
        inst.id=it->second.id;
        inst.geometry=it->second.geometry;
        inst.transform={{1,0,0, 0,1,0, 0,0,1, 0,0,0}};
        inst.is_projected_decal=true;
        inst.decal_opacity=info.opacity;
        inst.effective_light_mode=0.0f;
        pending_gpu_instances.emplace_back(std::move(inst));
        if(the_config->profile_renderer &&
           sprite_decal_queued_logs++<12U) {
            std::ostringstream msg;
            msg<<"SPR decal GPU stage=queued id="<<it->second.id
               <<" opacity="<<info.opacity
               <<" vertices="<<it->second.geometry->vertices.size();
            LOG_INFO(msg.str());
        }
        return true;
    }

    // Only timer rdSprite_Draw itself, rather than attributing the
    // separate GPU projector geometry build to the native submission path.
    int profile_native_sprite_draw(void *thing,void *orientation)
    {
        auto const previous=sprite_draw_active;
        sprite_draw_active={thing,orientation};
        bool const profiling=the_config && the_config->profile_renderer;
        auto const start=profiling?std::chrono::steady_clock::now()
                                  :std::chrono::steady_clock::time_point{};
        int const result=RealNativeSprite(thing,orientation);
        if(profiling) {
            native_sprite_totals.native_ns+=static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now()-start).count());
            ++native_sprite_totals.native_calls;
            if(result!=0)++native_sprite_totals.native_success;
        }
        sprite_draw_active=previous;
        return result;
    }

    int __cdecl HookedNativeSprite(void *thing,void *orientation)
    {
        bool const profiling=the_config && the_config->profile_renderer;
        uint32_t authored_type=0;
        bool known_type=false;
        // Always identify authored SPR types: disabling decals must hide
        // type-3 sprites rather than sending them through the vanilla
        // camera-facing billboard path.
        if(the_config && gpu_span_readable(thing,8)) {
            void const *resource=nullptr;
            std::memcpy(&resource,static_cast<uint8_t const *>(thing)+4,
                        sizeof(resource));
            known_type=gpu_span_readable(resource,32) &&
                sprite_types.lookup(resource,static_cast<char const *>(resource),
                                    authored_type);
        }
        if(profiling) {
            if(known_type && authored_type<native_sprite_totals.type_calls.size())
                ++native_sprite_totals.type_calls[authored_type];
            else
                ++native_sprite_totals.other_type_calls;
        }
        // A disabled decal is not a billboard: returning to native sprite
        // drawing would make blood/skidmarks float and face the camera.
        if(known_type && authored_type==jkgm::sprite_project_decal &&
           the_config && !the_config->enable_decals) {
            if(profiling)++native_sprite_totals.hidden_calls;
            return 0;
        }
        auto const info=current_sprite_info(thing);
        if(info.width==0.0f || info.height==0.0f || info.opacity==0.0f) {
            if(profiling)++native_sprite_totals.hidden_calls;
            return 0;
        }
        // Type-3 decals keep their GPU path and masked native MAT warm-up.
        // Count these separately from regular native sprite submissions.
        bool const projected=the_config && the_config->enable_decals &&
                             known_type &&
                             authored_type==jkgm::sprite_project_decal;
        if(projected) {
            if(submit_native_sprite_decal(thing,orientation,info)) {
                if(profiling)++native_sprite_totals.projected_gpu;
                return 1;
            }
            if(sprite_alpha_hook_active) {
                auto const was_suppressed=suppress_native_decal_face;
                suppress_native_decal_face=true;
                profile_native_sprite_draw(thing,orientation);
                suppress_native_decal_face=was_suppressed;
                if(profiling)++native_sprite_totals.projected_warmups;
            }
            return 1;
        }
        if(profiling && known_type && jkgm::sprite_is_flare(authored_type) &&
           flare_draw_trace_count++<6U) {
            // Native flare submission may be zero even if the SPR loaded.
            std::ostringstream msg;
            msg<<"SPR flare stage=draw_enter opacity="<<info.opacity;
            LOG_INFO(msg.str());
        }
        return profile_native_sprite_draw(thing,orientation);
    }

    bool request_native_sprite_hooks()
    {
        if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=0x00400000U ||
           !gpu_span_readable(reinterpret_cast<void const *>(sprite_draw_addr),
                              sizeof(sprite_draw_bytes)) ||
           !gpu_span_readable(reinterpret_cast<void const *>(sprite_preclip_addr),
                              sizeof(sprite_preclip_bytes)) ||
           std::memcmp(reinterpret_cast<void const *>(sprite_draw_addr),
                       sprite_draw_bytes,sizeof(sprite_draw_bytes))!=0 ||
           std::memcmp(reinterpret_cast<void const *>(sprite_preclip_addr),
                       sprite_preclip_bytes,sizeof(sprite_preclip_bytes))!=0 ||
           !gpu_span_readable(reinterpret_cast<void const *>(sprite_newentry_addr),
                              sizeof(sprite_newentry_bytes)) ||
           std::memcmp(reinterpret_cast<void const *>(sprite_newentry_addr),
                       sprite_newentry_bytes,sizeof(sprite_newentry_bytes))!=0 ||
           !gpu_span_readable(reinterpret_cast<void const *>(sprite_free_addr),
                              sizeof(sprite_free_bytes)) ||
           std::memcmp(reinterpret_cast<void const *>(sprite_free_addr),
                       sprite_free_bytes,sizeof(sprite_free_bytes))!=0 ||
           !gpu_span_readable(reinterpret_cast<void const *>(sprite_freeentry_addr),
                              sizeof(sprite_freeentry_bytes)) ||
           std::memcmp(reinterpret_cast<void const *>(sprite_freeentry_addr),
                       sprite_freeentry_bytes,sizeof(sprite_freeentry_bytes))!=0 ||
           !gpu_span_readable(reinterpret_cast<void const *>(sprite_frustum_addr),
                              sizeof(sprite_frustum_bytes)) ||
           std::memcmp(reinterpret_cast<void const *>(sprite_frustum_addr),
                       sprite_frustum_bytes,sizeof(sprite_frustum_bytes))!=0) {
            LOG_WARNING("SPR mode overrides disabled: unrecognized JK23 executable");
            return false;
        }
        LONG error=DetourAttach(&(PVOID &)RealNativeSpriteNewEntry,
                                HookedSpriteNewEntry);
        if(error!=NO_ERROR) {
            LOG_WARNING("SPR authored-type capture hook unavailable: ",error);
            return false;
        }
        error=DetourAttach(&(PVOID &)RealNativeSpriteFree,HookedSpriteFree);
        if(error!=NO_ERROR) {
            DetourDetach(&(PVOID &)RealNativeSpriteNewEntry,HookedSpriteNewEntry);
            LOG_WARNING("SPR resource free hook unavailable: ",error);
            return false;
        }
        error=DetourAttach(&(PVOID &)RealNativeSpriteFreeEntry,
                           HookedSpriteFreeEntry);
        if(error!=NO_ERROR) {
            DetourDetach(&(PVOID &)RealNativeSpriteFree,HookedSpriteFree);
            DetourDetach(&(PVOID &)RealNativeSpriteNewEntry,HookedSpriteNewEntry);
            LOG_WARNING("SPR pooled resource free hook unavailable: ",error);
            return false;
        }
        error=DetourAttach(&(PVOID &)RealNativeSprite,HookedNativeSprite);
        if(error!=NO_ERROR) {
            DetourDetach(&(PVOID &)RealNativeSpriteFreeEntry,
                         HookedSpriteFreeEntry);
            DetourDetach(&(PVOID &)RealNativeSpriteFree,HookedSpriteFree);
            DetourDetach(&(PVOID &)RealNativeSpriteNewEntry,HookedSpriteNewEntry);
            LOG_WARNING("SPR entry hook unavailable: ",error);
            return false;
        }
        error=DetourAttach(&(PVOID &)RealSpritePreclip,SpritePreclipThunk);
        if(error!=NO_ERROR) {
            DetourDetach(&(PVOID &)RealNativeSprite,HookedNativeSprite);
            DetourDetach(&(PVOID &)RealNativeSpriteFreeEntry,
                         HookedSpriteFreeEntry);
            DetourDetach(&(PVOID &)RealNativeSpriteFree,HookedSpriteFree);
            DetourDetach(&(PVOID &)RealNativeSpriteNewEntry,HookedSpriteNewEntry);
            LOG_WARNING("SPR preclip hook unavailable: ",error);
            return false;
        }
        // Optional exact-alpha hooks. A signature mismatch must never disable
        // native SPR orientation or make changes to unrelated cache faces.
        if(gpu_span_readable(reinterpret_cast<void const *>(sprite_addproc_addr),
                             sizeof(sprite_addproc_bytes)) &&
           gpu_span_readable(reinterpret_cast<void const *>(sprite_alpha_mid_addr),
                             sizeof(sprite_alpha_mid_bytes)) &&
           gpu_span_readable(reinterpret_cast<void const *>(sprite_hw_upload_addr),
                             sizeof(sprite_hw_upload_bytes)) &&
           std::memcmp(reinterpret_cast<void const *>(sprite_hw_upload_addr),
                       sprite_hw_upload_bytes,sizeof(sprite_hw_upload_bytes))==0 &&
           std::memcmp(reinterpret_cast<void const *>(sprite_addproc_addr),
                       sprite_addproc_bytes,sizeof(sprite_addproc_bytes))==0 &&
           std::memcmp(reinterpret_cast<void const *>(sprite_alpha_mid_addr),
                       sprite_alpha_mid_bytes,sizeof(sprite_alpha_mid_bytes))==0) {
            LONG const proc_err=DetourAttach(
                &(PVOID &)RealSpriteAddProc,HookedSpriteAddProc);
            if(proc_err==NO_ERROR) {
                LONG const alpha_err=DetourAttach(
                    &(PVOID &)RealSpriteAlphaMid,SpriteAlphaThunk);
                if(alpha_err==NO_ERROR) {
                    LONG const hw_err=DetourAttach(
                        &(PVOID &)RealSpriteHWUpload,HookedSpriteHWUpload);
                    if(hw_err==NO_ERROR) {
                        sprite_alpha_hook_active=true;
                        LOG_INFO("SPR per-Thing alpha and flare markers enabled");
                    } else {
                        DetourDetach(&(PVOID &)RealSpriteAlphaMid,SpriteAlphaThunk);
                        DetourDetach(&(PVOID &)RealSpriteAddProc,HookedSpriteAddProc);
                        LOG_WARNING("SPR HW upload flare marker hook unavailable: ",hw_err);
                    }
                }
                else {
                    DetourDetach(&(PVOID &)RealSpriteAddProc,HookedSpriteAddProc);
                    LOG_WARNING("SPR alpha converter hook unavailable: ",alpha_err);
                }
            }
            else LOG_WARNING("SPR face recording hook unavailable: ",proc_err);
        }
        else LOG_WARNING("SPR exact alpha disabled: unrecognized JK23 cache");

        // Optional native multi-entry .SPR extension. All three detours are
        // required together. A signature mismatch leaves ordinary single-
        // entry SPR loading untouched, rather than patching unknown code.
        bool const variant_signatures_ok=
            gpu_span_readable(reinterpret_cast<void const *>(
                sprite_load_addr),sizeof(sprite_load_bytes)) &&
            gpu_span_readable(reinterpret_cast<void const *>(
                sprite_free_world_addr),sizeof(sprite_free_world_bytes)) &&
            gpu_span_readable(reinterpret_cast<void const *>(
                sprite_clone_thing_addr),sizeof(sprite_clone_thing_bytes)) &&
            std::memcmp(reinterpret_cast<void const *>(sprite_load_addr),
                        sprite_load_bytes,sizeof(sprite_load_bytes))==0 &&
            std::memcmp(reinterpret_cast<void const *>(sprite_free_world_addr),
                        sprite_free_world_bytes,sizeof(sprite_free_world_bytes))==0 &&
            std::memcmp(reinterpret_cast<void const *>(sprite_clone_thing_addr),
                        sprite_clone_thing_bytes,sizeof(sprite_clone_thing_bytes))==0;
        if(variant_signatures_ok) {
            LONG const load_err=DetourAttach(
                &(PVOID &)RealSithSpriteLoad,HookedSithSpriteLoad);
            if(load_err==NO_ERROR) {
                LONG const free_err=DetourAttach(
                    &(PVOID &)RealSithSpriteFreeWorld,HookedSithSpriteFreeWorld);
                if(free_err==NO_ERROR) {
                    LONG const clone_err=DetourAttach(
                        &(PVOID &)RealSithThingClone,HookedSithThingClone);
                    if(clone_err==NO_ERROR) {
                        sprite_variant_hooks_active=true;
                        LOG_INFO("SPR multi-entry per-Thing variant hooks enabled");
                    } else {
                        DetourDetach(&(PVOID &)RealSithSpriteFreeWorld,
                                     HookedSithSpriteFreeWorld);
                        DetourDetach(&(PVOID &)RealSithSpriteLoad,
                                     HookedSithSpriteLoad);
                        LOG_WARNING("SPR Thing variant selection unavailable: ",
                                    clone_err);
                    }
                } else {
                    DetourDetach(&(PVOID &)RealSithSpriteLoad,
                                 HookedSithSpriteLoad);
                    LOG_WARNING("SPR variant world cleanup unavailable: ",free_err);
                }
            } else LOG_WARNING("SPR variant loader unavailable: ",load_err);
        } else LOG_WARNING("SPR variants disabled: unknown native loader");
        error=DetourAttach(&(PVOID &)RealSpriteFrustum,HookedSpriteFrustum);
        if(error==NO_ERROR) sprite_frustum_hook_active=true;
        else LOG_WARNING("SPR instance frustum-radius hook unavailable: ",error);
        LOG_INFO("SPR authored-type capture attached (index at +0x20 ignored)");
        return true;
    }
}

jkgm::native_sprite_sample jkgm::consume_native_sprite_sample()
{
    auto result=native_sprite_totals;
    native_sprite_totals={};
    return result;
}
bool jkgm::native_sprite_available()
{
    return native_sprite_hook_active;
}

// DirectX hooks

using DirectDrawCreate_type = HRESULT(WINAPI *)(GUID *lpGUID,
                                                LPDIRECTDRAW *lplpDD,
                                                IUnknown *pUnkOuter);
static DirectDrawCreate_type TrueDirectDrawCreate = nullptr;
HRESULT WINAPI HookedDirectDrawCreate(GUID *lpGUID, LPDIRECTDRAW *lplpDD, IUnknown *pUnkOuter)
{
    LOG_DEBUG("DirectDrawCreate hook called");
    *lplpDD = the_renderer->get_directdraw();
    return DD_OK;
}

using DirectDrawEnumerateA_type = HRESULT(WINAPI *)(LPDDENUMCALLBACKA lpCallback, LPVOID lpContext);
static DirectDrawEnumerateA_type TrueDirectDrawEnumerateA = nullptr;
HRESULT WINAPI HookedDirectDrawEnumerateA(LPDDENUMCALLBACKA lpCallback, LPVOID lpContext)
{
    LOG_DEBUG("DirectDrawEnumerateA hook called");
    return the_renderer->enumerate_devices(lpCallback, lpContext);
}

// Win32 API hooks

using CreateWindowExA_type = HWND(WINAPI *)(DWORD dwExStyle,
                                            LPCSTR lpClassName,
                                            LPCSTR lpWindowName,
                                            DWORD dwStyle,
                                            int X,
                                            int Y,
                                            int nWidth,
                                            int nHeight,
                                            HWND hWndParent,
                                            HMENU hMenu,
                                            HINSTANCE hInstance,
                                            LPVOID lpParam);

static CreateWindowExA_type TrueCreateWindowExA = nullptr;

static bool main_window_seen = false;
static HWND jk_window = NULL;
static std::string const jk_window_class_name = "wKernel";

HWND WINAPI HookedCreateWindowExA(DWORD dwExStyle,
                                  LPCSTR lpClassName,
                                  LPCSTR lpWindowName,
                                  DWORD dwStyle,
                                  int X,
                                  int Y,
                                  int nWidth,
                                  int nHeight,
                                  HWND hWndParent,
                                  HMENU hMenu,
                                  HINSTANCE hInstance,
                                  LPVOID lpParam)
{
    if(!main_window_seen) {
        LOG_DEBUG("CreateWindowExA hook called");
    }

    auto rv = TrueCreateWindowExA(dwExStyle,
                                  lpClassName,
                                  lpWindowName,
                                  dwStyle,
                                  X,
                                  Y,
                                  nWidth,
                                  nHeight,
                                  hWndParent,
                                  hMenu,
                                  hInstance,
                                  lpParam);

    if(!main_window_seen && jk_window_class_name == lpClassName) {
        LOG_DEBUG("Detected main window creation");
        main_window_seen = true;

        the_renderer->initialize(hInstance, rv);
    }

    return rv;
}

static HBITMAP const phony_dibsection_bitmap = (HBITMAP)0x70FFFF10;
static std::array<char, 640 * 480> phony_dibsection_memory;

using CreateDIBSection_type = HBITMAP(WINAPI *)(HDC hDC,
                                                BITMAPINFO const *pbmi,
                                                UINT usage,
                                                VOID **ppvBits,
                                                HANDLE hSection,
                                                DWORD offset);
static CreateDIBSection_type TrueCreateDIBSection = nullptr;
HBITMAP WINAPI HookedCreateDIBSection(HDC hDC,
                                      BITMAPINFO const *pbmi,
                                      UINT usage,
                                      VOID **ppvBits,
                                      HANDLE hSection,
                                      DWORD offset)
{
    LOG_DEBUG("CreateDIBSection hook called");
    *ppvBits = phony_dibsection_memory.data();
    the_renderer->set_menu_source(phony_dibsection_bitmap,
                                  reinterpret_cast<char const *>(*ppvBits));
    return phony_dibsection_bitmap;
}

using GetCursorPos_type = BOOL(WINAPI *)(LPPOINT lpPoint);
static GetCursorPos_type TrueGetCursorPos = nullptr;
BOOL WINAPI HookedGetCursorPos(LPPOINT lpPoint)
{
    POINT real_pos;
    auto rv = TrueGetCursorPos(&real_pos);

    if(rv != FALSE) {
        auto conv_pos =
            the_renderer->get_cursor_pos(jkgm::make_point((int)real_pos.x, (int)real_pos.y));
        lpPoint->x = (LONG)jkgm::get<0>(conv_pos);
        lpPoint->y = (LONG)jkgm::get<1>(conv_pos);
    }

    return rv;
}

using SetDIBColorTable_type = UINT(WINAPI *)(HDC hDC,
                                             UINT iStart,
                                             UINT cEntries,
                                             RGBQUAD const *prgbq);
static SetDIBColorTable_type TrueSetDIBColorTable = nullptr;
UINT WINAPI HookedSetDIBColorTable(HDC hDC, UINT iStart, UINT cEntries, RGBQUAD const *prgbq)
{
    the_renderer->set_menu_palette(iStart, jkgm::make_span(prgbq, cEntries));
    return TrueSetDIBColorTable(hDC, iStart, cEntries, prgbq);
}

using GdiFlush_type = BOOL(WINAPI *)();
static GdiFlush_type TrueGdiFlush = nullptr;
BOOL WINAPI HookedGdiFlush()
{
    bool rv = TrueGdiFlush();
    the_renderer->present_menu_gdi();
    return rv;
}

using DeleteObject_type = BOOL(WINAPI *)(HGDIOBJ ho);
static DeleteObject_type TrueDeleteObject = nullptr;
BOOL WINAPI HookedDeleteObject(HGDIOBJ ho)
{
    if(ho == (HGDIOBJ)phony_dibsection_bitmap) {
        the_renderer->maybe_clear_menu_source(ho);
        return TRUE;
    }

    return TrueDeleteObject(ho);
}

using ChangeDisplaySettingsA_type = LONG(WINAPI *)(DEVMODEA *lpDevMode, DWORD dwFlags);
static ChangeDisplaySettingsA_type TrueChangeDisplaySettingsA = nullptr;
LONG WINAPI HookedChangeDisplaySettingsA(DEVMODEA *lpDevMode, DWORD dwFlags)
{
    LOG_DEBUG("Suppressing ChangeDisplaySettingsA");
    return DISP_CHANGE_SUCCESSFUL;
}

using ChangeDisplaySettingsW_type = LONG(WINAPI *)(DEVMODEW *lpDevMode, DWORD dwFlags);
static ChangeDisplaySettingsW_type TrueChangeDisplaySettingsW = nullptr;
LONG WINAPI HookedChangeDisplaySettingsW(DEVMODEW *lpDevMode, DWORD dwFlags)
{
    LOG_DEBUG("Called ChangeDisplaySettingsW");
    return TrueChangeDisplaySettingsW(lpDevMode, dwFlags);
}

// IJIM-style KEY marker callbacks. Addresses and callback track stride were
// disassembled from the supported JK23 game executable, not guessed from stock
// JK. Do not install unless each native function still has its tested prologue.
namespace {
    constexpr uintptr_t puppet_advance_addr=0x0043ED50U;
    constexpr uintptr_t cog_thing_send_addr=0x004DFD80U;
    constexpr uintptr_t cog_add_message_addr=0x004E0890U;
    constexpr int key_callback_message=42; // free message slot in JK23 (JK.dll adds 40,44,47)
    constexpr uint8_t puppet_advance_bytes[]={
        0x55,0x8b,0xec,0x83,0xec,0x10,0x8b,0x55,0x0c
    };
    constexpr uint8_t cog_thing_send_bytes[]={
        0x83,0xec,0x08,0xa1,0x78,0x6c,0x83,0x00
    };
    constexpr uint8_t cog_add_message_bytes[]={
        0x8b,0x44,0x24,0x0c,0x8b,0x4c,0x24,0x04
    };
    using puppet_callback_fn=void(__cdecl *)(void *,int,uint32_t);
    using puppet_advance_fn=void(__cdecl *)(void *,int,float);
    using cog_thing_send_fn=float(__cdecl *)(void *,void *,int,float,float,float,float);
    using cog_add_message_fn=void(__cdecl *)(void *,int,char const *);
    puppet_advance_fn RealPuppetAdvance=
        reinterpret_cast<puppet_advance_fn>(puppet_advance_addr);
    bool key_callback_hook_active=false;
    bool key_callback_registration_hook_active=false;
    bool key_callback_message_registered=false;
    thread_local puppet_callback_fn active_previous_callback=nullptr;

    bool key_callback_signatures_match()
    {
        auto matches=[](uintptr_t addr,uint8_t const *bytes,size_t len) {
            return gpu_span_readable(reinterpret_cast<void const *>(addr),len) &&
                   std::memcmp(reinterpret_cast<void const *>(addr),bytes,len)==0;
        };
        return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))==0x00400000U &&
               matches(puppet_advance_addr,puppet_advance_bytes,
                       sizeof(puppet_advance_bytes)) &&
               matches(cog_thing_send_addr,cog_thing_send_bytes,
                       sizeof(cog_thing_send_bytes)) &&
               matches(cog_add_message_addr,cog_add_message_bytes,
                       sizeof(cog_add_message_bytes));
    }

    void __cdecl RelayPuppetCallback(void *thing,int track,uint32_t marker)
    {
        // Preserve all original footsteps, saber events, sound cues and track
        // completion logic even when there is no COG callback label.
        uint32_t thing_index=0;
        bool const valid_thing=thing && gpu_span_readable(thing,16);
        if(valid_thing)
            std::memcpy(&thing_index,static_cast<uint8_t const *>(thing)+4,4);
        puppet_callback_fn const original=active_previous_callback;
        if(original) original(thing,track,marker);
        if(!key_callback_message_registered || !valid_thing ||
           !gpu_span_readable(thing,16)) return;
        uint32_t current_index=0,thing_type=0;
        std::memcpy(&current_index,static_cast<uint8_t const *>(thing)+4,4);
        std::memcpy(&thing_type,static_cast<uint8_t const *>(thing)+12,4);
        if(current_index!=thing_index || thing_type==0) return;
        auto const send=reinterpret_cast<cog_thing_send_fn>(cog_thing_send_addr);
        // Sender is the animated Thing. Match IJIM-style marker/track parameters.
        send(thing,nullptr,key_callback_message,
             static_cast<float>(marker),
             static_cast<float>(track),0.0f,0.0f);
    }

    void __cdecl HookedPuppetAdvance(void *puppet,int track,float frames)
    {
        // rdPuppet has a track array at +8, stride 0x134; the callback is
        // at +0x12c in each track. The original engine calls it from three
        // branches in rdPuppet_AdvanceTrack. Temporarily relay it only while
        // advancing, so native callback lifetime and cleanup stay untouched.
        if(!key_callback_message_registered || !puppet ||
           track<0 || track>=4) {
            RealPuppetAdvance(puppet,track,frames);
            return;
        }
        auto *slot=static_cast<uint8_t *>(puppet)+8+0x134U*track+0x12cU;
        if(!gpu_span_readable(slot,sizeof(void *))) {
            RealPuppetAdvance(puppet,track,frames);
            return;
        }
        puppet_callback_fn cb=nullptr;
        std::memcpy(&cb,slot,sizeof(cb));
        // Also relay tracks without a native callback: the original
        // rdPuppet dispatcher skips every marker when this slot is null.
        if(cb==RelayPuppetCallback) {
            RealPuppetAdvance(puppet,track,frames);
            return;
        }
        auto const previous=active_previous_callback;
        active_previous_callback=cb;
        puppet_callback_fn const relay=RelayPuppetCallback;
        std::memcpy(slot,&relay,sizeof(relay));
        RealPuppetAdvance(puppet,track,frames);
        // rdPuppet_RemoveTrack may clear the callback during playback.
        // Never restore stale callback pointers after engine cleanup.
        if(gpu_span_readable(slot,sizeof(void *))) {
            puppet_callback_fn current=nullptr;
            std::memcpy(&current,slot,sizeof(current));
            if(current==relay) std::memcpy(slot,&cb,sizeof(cb));
        }
        active_previous_callback=previous;
    }

    void __cdecl register_key_callback_message(void *symbols)
    {
        // The executable signatures were verified before attaching Detours.
        // Do NOT recheck original bytes here: rdPuppet_AdvanceTrack has already
        // been patched with a Detours jump, so that check always fails.
        if(!symbols || !key_callback_hook_active) {
            LOG_WARNING("COG callback message not registered: missing symbol table or KEY hook");
            return;
        }
        reinterpret_cast<cog_add_message_fn>(cog_add_message_addr)(
            symbols,key_callback_message,"callback");
        key_callback_message_registered=true;
        LOG_INFO("Registered KEY callback COG message: Sender=Thing, "
                 "Param(0)=marker, Param(1)=track");
    }
    using jk_init_messages_fn=void(__cdecl *)();
    jk_init_messages_fn TrueJkInitNewCogMessages=nullptr;
    // JK.dll's InitNewCogMessages passes the COG symbol table in ESI rather
    // than on the stack. Preserve that register across its original thunk.
    __declspec(naked) void __cdecl HookedJkInitNewCogMessages()
    {
        __asm {
            pushfd
            pushad
            call dword ptr [TrueJkInitNewCogMessages]
            push esi
            call register_key_callback_message
            add esp,4
            popad
            popfd
            ret
        }
    }
}

// COG verbs (distance fog)
//
// The community patch's JK.dll exports InitNewCogVerbs, which jk.exe calls at startup
// with a function for registering COG verbs. We wrap that export: run the original,
// then register our own verbs through the same function.
//
// Verb handlers talk to jk.exe's COG interpreter through a few helper functions inside
// jk.exe, exactly like the handlers in JK.dll do. Those addresses belong to one specific
// build of jk.exe, so before registering anything we compare the first bytes of every
// helper against what we expect. If anything differs we register nothing, and the game
// runs normally without the fog verbs.
//
// COG pushes verb arguments left to right, so a handler pops them in reverse order.

namespace {
    using cog_handler_type = void(__cdecl *)(void *ctx);
    using cog_register_verb_type = void(__cdecl *)(void *ctx,
                                                   cog_handler_type handler,
                                                   char const *name);
    using JkInitNewCogVerbs_type = void(__cdecl *)(cog_register_verb_type register_verb,
                                                   void *ctx);

    using cog_pop_float_type = float(__cdecl *)(void *ctx);
    using cog_pop_int_type = int(__cdecl *)(void *ctx);
    using cog_pop_vector_type = void(__cdecl *)(void *ctx, float *out_xyz);
    using cog_pop_thing_type = void *(__cdecl *)(void *ctx);
    using cog_push_float_type = void(__cdecl *)(void *ctx, float value);
    using cog_push_int_type = void(__cdecl *)(void *ctx, int value);
    using cog_push_vector_type = void(__cdecl *)(void *ctx, float const *xyz);

    constexpr uintptr_t pop_float_address = 0x4E2780;
    constexpr uintptr_t pop_int_address = 0x4E2850;
    constexpr uintptr_t pop_vector_address = 0x4E2970;
    constexpr uintptr_t pop_thing_address = 0x4E2B50;
    constexpr uintptr_t push_float_address = 0x4E3650;
    constexpr uintptr_t push_int_address = 0x4E35D0;
    constexpr uintptr_t push_vector_address = 0x4E36E0;

    cog_pop_float_type const cog_pop_float = reinterpret_cast<cog_pop_float_type>(pop_float_address);
    cog_pop_int_type const cog_pop_int = reinterpret_cast<cog_pop_int_type>(pop_int_address);
    cog_pop_vector_type const cog_pop_vector =
        reinterpret_cast<cog_pop_vector_type>(pop_vector_address);
    cog_pop_thing_type const cog_pop_thing =
        reinterpret_cast<cog_pop_thing_type>(pop_thing_address);
    cog_push_float_type const cog_push_float =
        reinterpret_cast<cog_push_float_type>(push_float_address);
    cog_push_int_type const cog_push_int = reinterpret_cast<cog_push_int_type>(push_int_address);
    cog_push_vector_type const cog_push_vector =
        reinterpret_cast<cog_push_vector_type>(push_vector_address);

    struct cog_helper_signature {
        uintptr_t address;
        std::array<uint8_t, 8> prologue;
    };

    constexpr std::array<cog_helper_signature, 7> cog_helper_signatures = {{
        {pop_float_address, {0x8b, 0x4c, 0x24, 0x04, 0x83, 0xec, 0x20, 0x8b}},
        {pop_int_address, {0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x8b, 0x4d}},
        {pop_vector_address, {0x8b, 0x4c, 0x24, 0x04, 0x83, 0xec, 0x10, 0x8b}},
        {pop_thing_address, {0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x8b, 0x4d}},
        {push_float_address, {0x83, 0xec, 0x10, 0x8b, 0x44, 0x24, 0x18, 0x56}},
        {push_int_address, {0x83, 0xec, 0x10, 0x53, 0x8b, 0x5c, 0x24, 0x1c}},
        {push_vector_address, {0x83, 0xec, 0x10, 0x8b, 0x44, 0x24, 0x18, 0x53}},
    }};

    bool cog_helpers_match()
    {
        for(auto const &sig : cog_helper_signatures) {
            MEMORY_BASIC_INFORMATION mbi{};
            if(VirtualQuery(reinterpret_cast<void const *>(sig.address), &mbi, sizeof(mbi)) == 0) {
                return false;
            }

            if(mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
                return false;
            }

            auto const region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if(sig.address + sig.prologue.size() > region_end) {
                return false;
            }

            if(std::memcmp(reinterpret_cast<void const *>(sig.address),
                           sig.prologue.data(),
                           sig.prologue.size()) != 0) {
                return false;
            }
        }

        return true;
    }

    float clamp_float(float v, float lo, float hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Pure math COG extensions. Numeric parameters are popped in reverse
    // order, and all non-Sign results return flex. Lerp is intentionally
    // UNCLAMPED, so t outside [0,1] extrapolates.
    void __cdecl cog_math_min(void *ctx)
    {
        float const b=cog_pop_float(ctx);
        float const a=cog_pop_float(ctx);
        cog_push_float(ctx,jkgm::cog_math_min(a,b));
    }

    void __cdecl cog_math_max(void *ctx)
    {
        float const b=cog_pop_float(ctx);
        float const a=cog_pop_float(ctx);
        cog_push_float(ctx,jkgm::cog_math_max(a,b));
    }

    void __cdecl cog_math_clamp(void *ctx)
    {
        float const upper=cog_pop_float(ctx);
        float const lower=cog_pop_float(ctx);
        float const value=cog_pop_float(ctx);
        cog_push_float(ctx,jkgm::cog_math_clamp(value,lower,upper));
    }

    void __cdecl cog_math_sign(void *ctx)
    {
        float const value=cog_pop_float(ctx);
        cog_push_int(ctx,jkgm::cog_math_sign(value));
    }

    void __cdecl cog_math_lerp(void *ctx)
    {
        float const t=cog_pop_float(ctx);
        float const b=cog_pop_float(ctx);
        float const a=cog_pop_float(ctx);
        cog_push_float(ctx,jkgm::cog_math_lerp(a,b,t));
    }

    // Confirmed in JK23 jkgm.exe: Thing.controlType is +0x14,
    // Thing.actor is +0x278; AIControlBlock goalLVec +0x194,
    // movePos +0x1a0, pDistractor +0x1d0, and movepos +0x218.
    // These access the existing AI controller, not a duplicate AI state.
    void const *valid_ai_controller(void *thing)
    {
        if(!thing || !gpu_span_readable(thing,0x27c)) return nullptr;
        auto const *p=static_cast<uint8_t const *>(thing);
        uint32_t control=0;
        void const *ai=nullptr;
        std::memcpy(&control,p+0x14,4);
        if(control!=2) return nullptr;
        std::memcpy(&ai,p+0x278,sizeof(ai));
        return gpu_span_readable(ai,0x220)?ai:nullptr;
    }

    // AIGetFireTarget(actorThing): returns the target of the native
    // AISetFireTarget verb, or -1 if the AI/target is unavailable.
    // This is the AI 'distractor' / fire-target request; attack instincts
    // can independently decide whether to fire.
    void __cdecl cog_ai_get_fire_target(void *ctx)
    {
        void *thing=cog_pop_thing(ctx);
        void const *ai=valid_ai_controller(thing);
        void const *target=nullptr;
        if(ai) std::memcpy(&target,
            static_cast<uint8_t const *>(ai)+0x1d0,sizeof(target));
        int result=-1;
        if(target && gpu_span_readable(target,0x18)) {
            auto const *p=static_cast<uint8_t const *>(target);
            uint32_t kind=0,idx=0;
            std::memcpy(&kind,p+0x10,4);
            std::memcpy(&idx,p+4,4);
            if(kind!=0 && kind<=12 && idx<65536U)
                result=static_cast<int>(idx);
        }
        cog_push_int(ctx,result);
    }

    // AIGetMoveTargetPos(actorThing): native movement destination (+0x1a0).
    // JK's existing AIGetMovePos reads a different vector (+0x218).
    void __cdecl cog_ai_get_move_target_pos(void *ctx)
    {
        void *thing=cog_pop_thing(ctx);
        void const *ai=valid_ai_controller(thing);
        float result[3]{};
        if(ai) std::memcpy(result,
            static_cast<uint8_t const *>(ai)+0x1a0,sizeof(result));
        cog_push_vector(ctx,result);
    }

    // AIGetLookDir(actorThing): the actor's current horizontal goal
    // direction (native goalLVec, +0x194), not an absolute look position.
    void __cdecl cog_ai_get_look_dir(void *ctx)
    {
        void *thing=cog_pop_thing(ctx);
        void const *ai=valid_ai_controller(thing);
        float result[3]{};
        if(ai) std::memcpy(result,
            static_cast<uint8_t const *>(ai)+0x194,sizeof(result));
        cog_push_vector(ctx,result);
    }

    // JKGM's native rdModel3_GetMeshMatrix has a different address from
    // OpenJKDF2's JK layout. In the tested JKGM EXE, 0x445FD0 refreshes the
    // animated node matrices when the render frame is stale, then returns
    // an rdMatrix34 (12 floats). Never rely on a cached offscreen pose.
    using jkgm_get_node_matrix_fn=int(__cdecl *)(void *,float const *,
                                                  unsigned,float *);
    jkgm_get_node_matrix_fn const jkgm_get_node_matrix=
        reinterpret_cast<jkgm_get_node_matrix_fn>(
            jkgm::jkgm_node_matrix_address);
    // Native rdMatrix_ExtractAngles34 found by matching JK's PYR
    // extraction algorithm in the tested JKGM executable.
    using jkgm_extract_node_angles_fn=void(__cdecl *)(float const *,float *);
    jkgm_extract_node_angles_fn const jkgm_extract_node_angles=
        reinterpret_cast<jkgm_extract_node_angles_fn>(
            jkgm::jkgm_node_angles_address);

    bool get_cog_thing_node_matrix(void *thing,int node,
                                    jkgm::gpu_3do_matrix34 &output)
    {
        auto const safe_read=[](uintptr_t address,void *dest,size_t length)->bool {
            if(!jkgm::thing_node_valid_address(address,length) ||
               !gpu_span_readable(reinterpret_cast<void const *>(address),
                                  length))
                return false;
            std::memcpy(dest,reinterpret_cast<void const *>(address),length);
            return true;
        };
        // Check the exact engine accessor, not merely COG helper signatures.
        // This also rejects a different JK executable without touching it.
        static bool const supported=jkgm::validate_native_thing_node_accessor(
            safe_read);
        if(!supported) return false;
        jkgm::thing_node_native_input native{};
        if(!jkgm::resolve_native_thing_node(safe_read,
              reinterpret_cast<uintptr_t>(thing),node,native))
            return false;
        if(!jkgm_get_node_matrix(reinterpret_cast<void *>(native.rd_thing),
                                 native.world_placement.data(),
                                 static_cast<unsigned>(node),output.data()))
            return false;
        return jkgm::gpu_3do_matrix_finite(output);
    }

    // World-impact handler, metal already dispatched: select HITEARTH
    // for EARTH surfaces instead of routing every remaining hit to HITHARD.
    // Signature-guarded, no change to native volume/threshold/touched order.
    using native_earth_sound_fn=void(__cdecl *)();
    native_earth_sound_fn RealNativeEarthSound=
        reinterpret_cast<native_earth_sound_fn>(
            jkgm::native_earth_sound_branch);
    void *native_earth_sound_resume_addr=
        reinterpret_cast<void *>(jkgm::native_earth_sound_resume);
    __declspec(naked) void __cdecl NativeEarthSoundThunk()
    {
        __asm {
            // Original: MOV ECX,[ESP+0xC]; PUSH ECX.
            mov ecx,dword ptr [esp+0x0c]
            push ecx
            pushfd
            // EDI still points at the impacted SithSurface, +0x10 flags.
            test dword ptr [edi+0x10],0x00080000
            jz earth_sound_hard
            popfd
            push 48 // SITH_SC_HITEARTH
            jmp dword ptr [native_earth_sound_resume_addr]
        earth_sound_hard:
            popfd
            push 46 // SITH_SC_HITHARD
            jmp dword ptr [native_earth_sound_resume_addr]
        }
    }
    bool request_native_earth_sound_hook()
    {
        auto const addr=jkgm::native_earth_sound_branch;
        if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=
               0x00400000U ||
           !gpu_span_readable(reinterpret_cast<void const *>(addr),
                              jkgm::native_earth_sound_bytes.size()) ||
           std::memcmp(reinterpret_cast<void const *>(addr),
                       jkgm::native_earth_sound_bytes.data(),
                       jkgm::native_earth_sound_bytes.size())!=0) {
            LOG_WARNING("JKGM HITEARTH hook skipped: unsupported EXE");
            return false;
        }
        LONG const error=DetourAttach(
            &(PVOID &)RealNativeEarthSound,NativeEarthSoundThunk);
        if(error!=NO_ERROR) {
            LOG_WARNING("JKGM HITEARTH hook attach failed: ",error);
            return false;
        }
        return true;
    }

    // At 0x4F7833 native physics has JUST selected its friction factor:
    // [ESP+0x10]. ESI is the Thing being simulated; for a Thing-face
    // attachment, [ESI+0x58] is the supporting Thing. Preserve all flags
    // and registers; Detours replays the displaced MOV ECX/MOV EDX.
    using native_thingface_ice_join_fn=void(__cdecl *)();
    native_thingface_ice_join_fn RealThingFaceIceJoin=
        reinterpret_cast<native_thingface_ice_join_fn>(
            jkgm::native_thingface_ice_point);
    __declspec(naked) void __cdecl ThingFaceIceMultiplierThunk()
    {
        __asm {
            pushfd
            push eax
            test dword ptr [esi+0x34],2  // SITH_ATTACH_THINGFACE
            jz ice_done
            mov eax,dword ptr [esi+0x58] // attached parent Thing
            test eax,eax
            jz ice_done
            test dword ptr [eax],0x80000000 // Custom icy ThingFlag
            jz ice_done
            mov dword ptr [esp+0x18],0x3e99999a // Native icy: 0.3
        ice_done:
            pop eax
            popfd
            jmp dword ptr [RealThingFaceIceJoin]
        }
    }

    bool request_native_thingface_friction_hook()
    {
        auto const at=jkgm::native_thingface_ice_point;
        if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=
               0x00400000U ||
           !gpu_span_readable(reinterpret_cast<void const *>(at),
                              jkgm::native_thingface_ice_bytes.size()) ||
           std::memcmp(reinterpret_cast<void const *>(at),
                       jkgm::native_thingface_ice_bytes.data(),
                       jkgm::native_thingface_ice_bytes.size())!=0) {
            LOG_WARNING("JKGM Thing-face icy hook skipped: unsupported EXE");
            return false;
        }
        LONG const error=DetourAttach(
            &(PVOID&)RealThingFaceIceJoin,ThingFaceIceMultiplierThunk);
        if(error!=NO_ERROR) {
            LOG_WARNING("JKGM Thing-face icy hook attach failed: ",error);
            return false;
        }
        return true;
    }

    // Tested JKGM ABI: 11 cdecl args. Do not patch the original EXE.
    using native_intersect_fn=int (__cdecl *)(
        void*,float const*,float const*,float,float,void*,
        uint32_t,float*,void**,void**,float*);
    native_intersect_fn RealType2Intersection=reinterpret_cast<native_intersect_fn>(
        jkgm::jkgm_sphere_thing_intersect_address);
    thread_local bool type2_busy=false;
    unsigned type2_trace_count=0;

    // Native engine collision candidates are valid SithThing pointers.
    // The common case is NOT collide=2; avoid multiple VirtualQuery calls
    // and allocating/converting 3DO data for every crowded-sector pair.
    // Keep an SEH guard because this is an injected native ABI boundary.
    bool type2_pair_needs_mesh(void const *checked,void const *moving)
    {
        if(!checked || reinterpret_cast<uintptr_t>(checked)<0x10000U ||
           (moving && reinterpret_cast<uintptr_t>(moving)<0x10000U))
            return false;
        int32_t checked_mode=0,moving_mode=0;
        __try {
            auto const *c=static_cast<uint8_t const *>(checked);
            checked_mode=*reinterpret_cast<int32_t const *>(c+0x28);
            if(moving) {
                auto const *m=static_cast<uint8_t const *>(moving);
                moving_mode=*reinterpret_cast<int32_t const *>(m+0x28);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return checked_mode==2 || moving_mode==2;
    }
    void trace_type2(char const *status,uint32_t node=0) {
        if(the_config && the_config->profile_renderer && type2_trace_count++<16)
            LOG_INFO("JKGM collide=2 stage=",status," node=",node);
    }

    int __cdecl HookedType2Intersection(
        void* moving,float const* start,float const* dir,float max_distance,
        float moving_radius,void* checked,uint32_t flags,float* out_distance,
        void** out_mesh,void** out_face,float* out_normal)
    {
        auto const native=[&]()->int {
            return RealType2Intersection(moving,start,dir,max_distance,
                                         moving_radius,checked,flags,
                                         out_distance,out_mesh,out_face,out_normal);
        };
        if(type2_busy || !the_config || !the_config->enable_mesh_collision ||
           (flags&(jkgm::mots_raycast_force_sphere|0x400U))!=0 ||
           !checked || !start || !dir || !out_distance ||
           !out_mesh || !out_face || !out_normal)
            return native();
        if(!type2_pair_needs_mesh(checked,moving))
            return native();
        auto const read=[](uintptr_t address,void* output,size_t size)->bool {
            if(!jkgm::thing_node_valid_address(address,size) ||
               !gpu_span_readable(reinterpret_cast<void const*>(address),size))
                return false;
            std::memcpy(output,reinterpret_cast<void const*>(address),size);
            return true;
        };
        int32_t checked_mode=0,moving_mode=0;
        if(!read(reinterpret_cast<uintptr_t>(checked)+0x28,&checked_mode,4) ||
           (moving && !read(reinterpret_cast<uintptr_t>(moving)+0x28,
                           &moving_mode,4)))
            return native();
        if(!gpu_span_readable(start,12) || !gpu_span_readable(dir,12) ||
           !gpu_span_readable(out_distance,4) ||
           !gpu_span_readable(out_mesh,4) || !gpu_span_readable(out_face,4) ||
           !gpu_span_readable(out_normal,12))return native();

        struct guard {
            bool &flag;
            explicit guard(bool &x):flag(x){flag=true;}
            ~guard(){flag=false;}
        } active(type2_busy);

        jkgm::thing_node_vector source{},direction{},checked_pos{},moving_pos{};
        std::memcpy(source.data(),start,12);
        std::memcpy(direction.data(),dir,12);
        if(!jkgm::mots_native_detail::read_vec3(
               read,reinterpret_cast<uintptr_t>(checked)+0xb8,checked_pos) ||
           (moving && !jkgm::mots_native_detail::read_vec3(
               read,reinterpret_cast<uintptr_t>(moving)+0xb8,moving_pos))) {
            trace_type2("invalid_thing_position");
            return native();
        }
        float checked_size=0,moving_size=0;
        if(!read(reinterpret_cast<uintptr_t>(checked)+0x30,&checked_size,4) ||
           (moving && !read(reinterpret_cast<uintptr_t>(moving)+0x30,
                            &moving_size,4)))return native();
        jkgm::mots_collision_participant const checked_part{
            checked_mode,checked_size};
        jkgm::mots_collision_participant const moving_part{
            moving_mode,moving_size};
        bool const reverse=checked_mode!=2;
        void* owner=reverse?moving:checked;
        if(!owner)return native();
        // Cached geometry is read/fitted only on a new model identity or
        // a bounded periodic refresh. Amputation flags remain live.
        thread_local jkgm::mots_collision_snapshot_cache collision_cache;
        jkgm::mots_native_model_snapshot const *model=nullptr;
        uint64_t const now_ms=GetTickCount64();
        if(!collision_cache.get(
               read,reinterpret_cast<uintptr_t>(owner),now_ms,model)) {
            trace_type2("invalid_3do_snapshot");
            return native();
        }
        // Resolve the current Thing placement ONCE, not per mesh. Native
        // rdModel3_GetMeshMatrix still updates animated nodes every query.
        jkgm::thing_node_native_input pose_source{};
        if(!jkgm::resolve_native_thing_node(
               read,reinterpret_cast<uintptr_t>(owner),0,pose_source)) {
            trace_type2("invalid_pose_source");
            return native();
        }
        jkgm::mots_native_pose_reader pose=[&](
            uint32_t index,jkgm::gpu_3do_matrix34 &matrix) {
            return jkgm_get_node_matrix(
                reinterpret_cast<void*>(pose_source.rd_thing),
                pose_source.world_placement.data(),index,matrix.data()) &&
                jkgm::gpu_3do_matrix_finite(matrix);
        };
        thread_local uint64_t last_cache_log_ms=0;
        if(the_config->profile_renderer &&
           (last_cache_log_ms==0 || now_ms-last_cache_log_ms>=2000)) {
            auto const &c=collision_cache.counters();
            LOG_INFO("JKGM collide=2 cache hits=",c.hits,
                     " rebuilds=",c.rebuilds," failures=",c.failed,
                     " evictions=",c.evicted);
            last_cache_log_ms=now_ms;
        }
        jkgm::mots_collision_query_hit result{};
        jkgm::mots_collision_query_stats counters{};
        auto const status=jkgm::mots_probe_type2_collision(
            moving?&moving_part:nullptr,checked_part,
            reverse?model:nullptr,reverse?nullptr:model,
            pose,pose,moving_pos,checked_pos,source,direction,
            max_distance,moving_radius,flags,true,true,result,&counters);
        if(status==jkgm::mots_query_status::unavailable) {
            trace_type2("invalid_pose_or_query");
            return native();
        }
        if(status==jkgm::mots_query_status::miss) {
            trace_type2("miss");
            return 0;
        }
        jkgm::mots_engine_type2_hit hit{};
        if(!jkgm::mots_prepare_engine_type2_hit(*model,result,hit)) {
            trace_type2("invalid_hit_pointer");
            return native();
        }
        *out_distance=hit.distance;
        // MotS sphere-tree collision does not change mesh/face outputs;
        // preserving them matches its native hit-result ABI.
        std::memcpy(out_normal,hit.normal.data(),12);
        trace_type2("hit",result.node);
        return hit.flags;
    }

    bool request_type2_intersection_hook()
    {
        if(!the_config || !the_config->enable_mesh_collision)return false;
        auto const safe_read=[](uintptr_t at,void* dst,size_t len)->bool {
            if(!jkgm::thing_node_valid_address(at,len) ||
               !gpu_span_readable(reinterpret_cast<void const*>(at),len))
                return false;
            std::memcpy(dst,reinterpret_cast<void const*>(at),len);
            return true;
        };
        std::array<uint8_t,12> signature{};
        if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=
               0x00400000U ||
           !safe_read(jkgm::jkgm_sphere_thing_intersect_address,
                      signature.data(),signature.size()) ||
           signature!=jkgm::jkgm_sphere_thing_intersect_prologue ||
           !jkgm::validate_native_thing_node_accessor(safe_read)) {
            LOG_WARNING("JKGM collide=2 hook skipped: unknown game executable");
            return false;
        }
        LONG const err=DetourAttach(&(PVOID&)RealType2Intersection,
                                    HookedType2Intersection);
        if(err!=NO_ERROR) {
            LOG_WARNING("JKGM collide=2 DetourAttach failed: ",err);
            return false;
        }
        return true;
    }

    bool get_cog_thing_node_pose(void *thing,int node,
                                 jkgm::thing_node_pose &pose)
    {
        jkgm::gpu_3do_matrix34 matrix{};
        return get_cog_thing_node_matrix(thing,node,matrix) &&
               jkgm::get_thing_node_pose(matrix,pose);
    }

    // Exact signatures from the user's SHA-verified JKGM executable, NOT
    // vanilla OpenJKDF2 addresses. Reject all calls on another executable.
    constexpr uintptr_t node_attach_native_update=0x004CC4B0U;
    constexpr uintptr_t node_attach_native_attach=0x004CE5A0U;
    constexpr uintptr_t node_attach_native_detach=0x004CE660U;
    constexpr uintptr_t node_attach_native_setpose=0x004CDAB0U;
    constexpr uintptr_t node_attach_native_setsector=0x004CD4B0U;
    using native_thing_update_fn=void(__cdecl *)(float,int);
    using native_thing_attach_fn=void(__cdecl *)(void*,void*);
    using native_thing_detach_fn=void(__cdecl *)(void*);
    using native_thing_setpose_fn=void(__cdecl *)(void*,float const*,float const*);
    using native_thing_setsector_fn=void(__cdecl *)(void*,void*,int);
    native_thing_update_fn RealNativeThingUpdate=
        reinterpret_cast<native_thing_update_fn>(node_attach_native_update);
    native_thing_detach_fn RealNativeThingDetach=
        reinterpret_cast<native_thing_detach_fn>(node_attach_native_detach);

    bool node_attach_native_signatures_match()
    {
        if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=0x00400000U ||
           !jkgm::engine_material_debug::matches_tested_executable())return false;
        auto match=[](uintptr_t address,std::initializer_list<uint8_t> signature) {
            return gpu_span_readable(reinterpret_cast<void const *>(address),
                                     signature.size()) &&
                   std::memcmp(reinterpret_cast<void const *>(address),
                               signature.begin(),signature.size())==0;
        };
        return match(node_attach_native_update,
                     {0x51,0x8b,0x0d,0x10,0x3a,0x83,0x00,0x53}) &&
               match(node_attach_native_attach,
                     {0x83,0xec,0x0c,0x56,0x8b,0x74,0x24,0x14,0x57}) &&
               match(node_attach_native_detach,
                     {0x83,0xec,0x0c,0x53,0x56,0x8b,0x74,0x24,0x18,0x57}) &&
               match(node_attach_native_setpose,
                     {0x8b,0x44,0x24,0x04,0x8b,0x54,0x24,0x08,0x56,0x57}) &&
               match(node_attach_native_setsector,
                     {0x56,0x8b,0x74,0x24,0x08,0x57,0x8b,0x7c,0x24,0x10});
    }

    bool read_node_attachment_identity(uintptr_t thing,uint32_t &signature)
    {
        if(!jkgm::thing_node_valid_address(thing,0xe8) ||
           !gpu_span_readable(reinterpret_cast<void const *>(thing),0xe8))
            return false;
        uint32_t kind=0;
        std::memcpy(&kind,reinterpret_cast<void const *>(thing+0x0c),4);
        std::memcpy(&signature,reinterpret_cast<void const *>(thing+0x74),4);
        return kind>0 && kind<=15 && signature!=0;
    }

    // Advance native attachments AFTER Sith's normal Thing update and BEFORE
    // rendering/culling. Keep sector membership, COG position and render pose
    // synchronized, not just a render-only sprite transform.
    void __cdecl HookedNativeThingUpdate(float seconds,int milliseconds)
    {
        RealNativeThingUpdate(seconds,milliseconds);
        uint32_t world=0;
        if(jkgm::engine_material_debug::read_object(
             jkgm::engine_material_debug::current_world_global,world))
            jkgm::get_node_attachment_registry().prepare_world(world);
        auto records=jkgm::get_node_attachment_registry().snapshot();
        // Parent node attachments must advance before their children.
        std::unordered_set<uintptr_t> completed;
        for(size_t pass=0;pass<records.size();++pass) {
            bool advanced=false;
            for(auto const &rec:records) {
                if(completed.count(rec.child))continue;
                bool awaiting_parent=false;
                for(auto const &p:records)
                    if(p.child==rec.parent && !completed.count(p.child)) {
                        awaiting_parent=true;break;
                    }
                if(awaiting_parent)continue;
                advanced=true;
                completed.insert(rec.child);
                uint32_t child_sig=0,parent_sig=0;
                if(!read_node_attachment_identity(rec.child,child_sig) ||
                   !read_node_attachment_identity(rec.parent,parent_sig) ||
                   child_sig!=rec.child_signature ||
                   parent_sig!=rec.parent_signature) {
                    // Never touch the memory of a recycled/destroyed child.
                    // The native detach/destruction routine owns flag cleanup.
                    jkgm::get_node_attachment_registry().erase(rec.child);
                    continue;
                }
                auto const *child=reinterpret_cast<uint8_t const *>(rec.child);
                auto const *parent=reinterpret_cast<uint8_t const *>(rec.parent);
                uint32_t native_flags=0,parent_ptr=0;
                std::memcpy(&native_flags,child+0x34,4);
                std::memcpy(&parent_ptr,child+0x58,4);
                if((native_flags & (jkgm::native_attach_thing |
                                     jkgm::native_attach_node)) !=
                   (jkgm::native_attach_thing |
                    jkgm::native_attach_node) ||
                   parent_ptr!=rec.parent) {
                    // Vanilla DetachThing, destruction or reattachment won.
                    jkgm::get_node_attachment_registry().erase(rec.child);
                    continue;
                }
                jkgm::thing_node_pose node_pose{},out{};
                if(!get_cog_thing_node_pose(
                       reinterpret_cast<void *>(rec.parent),rec.node,node_pose))
                    continue;
                if(!jkgm::compute_node_attachment_pose(rec,node_pose,out))
                    continue;
                float pose[12]={
                    out.right[0],out.right[1],out.right[2],
                    out.look[0],out.look[1],out.look[2],
                    out.up[0],out.up[1],out.up[2],0,0,0
                };
                auto const setpose=reinterpret_cast<native_thing_setpose_fn>(
                    node_attach_native_setpose);
                setpose(reinterpret_cast<void *>(rec.child),
                        out.origin.data(),pose);
                uint32_t child_sector=0,parent_sector=0;
                std::memcpy(&child_sector,child+0x5c,4);
                std::memcpy(&parent_sector,parent+0x5c,4);
                if(parent_sector && parent_sector!=child_sector) {
                    auto const setsector=reinterpret_cast<native_thing_setsector_fn>(
                        node_attach_native_setsector);
                    setsector(reinterpret_cast<void *>(rec.child),
                              reinterpret_cast<void *>(uintptr_t(parent_sector)),
                              0);
                }
            }
            if(!advanced)break; // defensive against impossible cycle
        }
    }

    void __cdecl HookedNativeThingDetach(void *child)
    {
        jkgm::get_node_attachment_registry().erase(
            reinterpret_cast<uintptr_t>(child));
        RealNativeThingDetach(child);
    }

    bool attach_native_node_attachment_hooks()
    {
        if(!node_attach_native_signatures_match() ||
           !gpu_span_readable(
             reinterpret_cast<void const *>(jkgm::jkgm_node_matrix_address),
             jkgm::jkgm_node_matrix_prologue.size())) {
            LOG_WARNING("ThingNode attachment hooks unavailable: native signature mismatch");
            return false;
        }
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        LONG const a=DetourAttach(&(PVOID&)RealNativeThingUpdate,
                                  HookedNativeThingUpdate);
        LONG const b=a==NO_ERROR ?
            DetourAttach(&(PVOID&)RealNativeThingDetach,
                         HookedNativeThingDetach):a;
        if(a!=NO_ERROR || b!=NO_ERROR) {
            DetourTransactionAbort();
            LOG_WARNING("ThingNode attachment Detours failed: ",b);
            return false;
        }
        LONG const committed=DetourTransactionCommit();
        if(committed!=NO_ERROR) {
            LOG_WARNING("ThingNode attachment transaction failed: ",committed);
            return false;
        }
        LOG_INFO("Native ThingNode attachment hooks installed");
        return true;
    }
    void detach_native_node_attachment_hooks()
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)RealNativeThingUpdate,HookedNativeThingUpdate);
        DetourDetach(&(PVOID&)RealNativeThingDetach,HookedNativeThingDetach);
        DetourTransactionCommit();
    }

    // AttachThingToThingNode(child,parent,node,localOffset).
    // Always follows both position and orientation. Native attach flags hold
    // THING | NOMOVE | NODE, so built-in GetThingAttachFlags() sees 0x80.
    // Normal DetachThing() removes the node bit and sidecar.
    void __cdecl cog_attach_thing_to_thing_node(void *ctx)
    {
        float xyz[3]{};
        cog_pop_vector(ctx,xyz);
        int const node=cog_pop_int(ctx);
        void *parent=cog_pop_thing(ctx);
        void *child=cog_pop_thing(ctx);
        if(!native_node_attachment_hooks_active)return;
        jkgm::node_attachment a{};
        a.child=reinterpret_cast<uintptr_t>(child);
        a.parent=reinterpret_cast<uintptr_t>(parent);
        a.node=node;
        a.offset={{xyz[0],xyz[1],xyz[2]}};
        if(!read_node_attachment_identity(a.child,a.child_signature) ||
           !read_node_attachment_identity(a.parent,a.parent_signature) ||
           !jkgm::node_attachment_valid(a))return;
        jkgm::gpu_3do_matrix34 matrix{};
        if(!get_cog_thing_node_matrix(parent,node,matrix))return;
        // A parent may already have a *native* attachment (not in our
        // registry). Reject cycles through those as well: JK's intrusive
        // pAttachedThing lists cannot represent cyclic parent chains.
        uintptr_t walker=a.parent;
        for(unsigned depth=0;depth<128U;++depth) {
            if(walker==a.child)return;
            if(!jkgm::thing_node_valid_address(walker,0x5c) ||
               !gpu_span_readable(
                   reinterpret_cast<void const *>(walker),0x5c))return;
            uint32_t native_flags=0,native_parent=0;
            std::memcpy(&native_flags,
                reinterpret_cast<void const *>(walker+0x34),4);
            if((native_flags & 0x6U)==0U)break;
            std::memcpy(&native_parent,
                reinterpret_cast<void const *>(walker+0x58),4);
            if(!native_parent)return;
            walker=uintptr_t(native_parent);
            if(depth==127U)return;
        }
        uint32_t world=0;
        if(jkgm::engine_material_debug::read_object(
             jkgm::engine_material_debug::current_world_global,world))
            jkgm::get_node_attachment_registry().prepare_world(world);
        auto &registry=jkgm::get_node_attachment_registry();
        // Check cycles before invoking native attachment, which may detach
        // an existing parent and thus erase an old entry via the hook.
        if(!registry.set(a)) {
            LOG_WARNING("AttachThingToThingNode rejected: invalid node/offset/cycle");
            return;
        }
        auto const native_attach=reinterpret_cast<native_thing_attach_fn>(
            node_attach_native_attach);
        native_attach(child,parent);
        // Native reattachment may invoke DetachThing and erase the sidecar.
        registry.set(a);
        uint32_t current=0,linked=0;
        std::memcpy(&current,static_cast<uint8_t const *>(child)+0x34,4);
        std::memcpy(&linked,static_cast<uint8_t const *>(child)+0x58,4);
        if((current & jkgm::native_attach_thing)==0 ||
           linked!=a.parent) {
            registry.erase(a.child);
            LOG_WARNING("AttachThingToThingNode: native parent link failed");
            return;
        }
        current |= jkgm::native_attach_no_move |
                   jkgm::native_attach_node;
        std::memcpy(static_cast<uint8_t *>(child)+0x34,&current,4);
        LOG_INFO("AttachThingToThingNode: node=",node,
                 " child=",a.child," attachFlags=",current);
    }

    // Returns the REAL attached face normal, not the transient sphere-
    // collision response normal. World/Thing-face attachments are already
    // tracked by JK even when experimental collide=2 is disabled.
    void __cdecl cog_get_thing_attach_normal(void *ctx)
    {
        void *thing=cog_pop_thing(ctx);
        jkgm::thing_node_vector normal{};
        auto const read=[](uintptr_t address,void *dst,size_t length) {
            if(!jkgm::thing_node_valid_address(address,length) ||
               !gpu_span_readable(reinterpret_cast<void const *>(address),length))
                return false;
            std::memcpy(dst,reinterpret_cast<void const *>(address),length);
            return true;
        };
        if(thing)jkgm::read_thing_attachment_normal(
            read,reinterpret_cast<uintptr_t>(thing),normal);
        cog_push_vector(ctx,normal.data());
    }

    enum class cog_thing_node_channel {position,right,look,up};
    void get_cog_thing_node_vector(void *ctx,cog_thing_node_channel channel)
    {
        int const node=cog_pop_int(ctx);
        void *thing=cog_pop_thing(ctx);
        float result[3]{};
        jkgm::thing_node_pose pose{};
        if(get_cog_thing_node_pose(thing,node,pose)) {
            auto const &v=channel==cog_thing_node_channel::position ? pose.origin:
                          channel==cog_thing_node_channel::right ? pose.right:
                          channel==cog_thing_node_channel::look ? pose.look:
                                                                pose.up;
            for(int i=0;i<3;++i)result[i]=v[i];
        }
        cog_push_vector(ctx,result);
    }
    void __cdecl cog_get_thing_node_pos(void *ctx) {
        get_cog_thing_node_vector(ctx,cog_thing_node_channel::position);
    }
    void __cdecl cog_get_thing_node_rvec(void *ctx) {
        get_cog_thing_node_vector(ctx,cog_thing_node_channel::right);
    }
    void __cdecl cog_get_thing_node_lvec(void *ctx) {
        get_cog_thing_node_vector(ctx,cog_thing_node_channel::look);
    }
    void __cdecl cog_get_thing_node_uvec(void *ctx) {
        get_cog_thing_node_vector(ctx,cog_thing_node_channel::up);
    }
    void __cdecl cog_get_thing_node_pyr(void *ctx)
    {
        int const node=cog_pop_int(ctx);
        void *thing=cog_pop_thing(ctx);
        float pyr[3]{};
        auto const safe_read=[](uintptr_t address,void *out,size_t count)->bool {
            if(!jkgm::thing_node_valid_address(address,count) ||
               !gpu_span_readable(reinterpret_cast<void const *>(address),count))
                return false;
            std::memcpy(out,reinterpret_cast<void const *>(address),count);
            return true;
        };
        static bool const angles_supported=
            jkgm::validate_native_thing_node_angles(safe_read);
        jkgm::gpu_3do_matrix34 matrix{};
        if(angles_supported && get_cog_thing_node_matrix(thing,node,matrix)) {
            jkgm_extract_node_angles(matrix.data(),pyr);
            for(float v:pyr) {
                if(!std::isfinite(v)) {
                    pyr[0]=pyr[1]=pyr[2]=0.0f;
                    break;
                }
            }
        }
        cog_push_vector(ctx,pyr);
    }

    bool sprite_cog_thing_identity(void const *thing,uint32_t &signature);

    // Independently animate roll around the SPR's camera-facing plane normal.
    void __cdecl cog_set_sprite_roll(void *ctx)
    {
        float const seconds=cog_pop_float(ctx);
        float const degrees=cog_pop_float(ctx);
        void *thing=cog_pop_thing(ctx);
        uint32_t signature=0;
        if(!sprite_cog_thing_identity(thing,signature)) return;
        if(!sprite_instances.set_roll(thing,signature,degrees,GetTickCount(),seconds))
            LOG_WARNING("SetSpriteRoll: invalid angle or transition");
    }

    void __cdecl cog_get_sprite_roll(void *ctx)
    {
        void *thing=cog_pop_thing(ctx);
        uint32_t signature=0;
        float roll=0.0f;
        if(sprite_cog_thing_identity(thing,signature))
            roll=sprite_instances.sample_roll(thing,signature,GetTickCount());
        cog_push_float(ctx,roll);
    }

    // SetSpriteInfo(thing, vector(width multiplier, height multiplier, opacity),
    //               transition_seconds). A Thing's rdThing is embedded at +0xc4
    // in the validated JK23 SithThing layout, and type 4 means rdSprite.
    // Never alter a shared rdSprite's dimensions or material.
    bool sprite_cog_thing_identity(void const *thing,uint32_t &signature)
    {
        if(!thing || !gpu_span_readable(thing,0xc8)) return false;
        auto const *p=static_cast<uint8_t const *>(thing);
        uint32_t sith_type=0,rd_type=0;
        std::memcpy(&sith_type,p+0x0c,4);
        std::memcpy(&rd_type,p+0xc4,4);
        if(sith_type==0 || rd_type!=4) return false;
        std::memcpy(&signature,p+0x74,4);
        return true;
    }

    void __cdecl cog_set_sprite_info(void *ctx)
    {
        float const seconds=cog_pop_float(ctx);
        float xyz[3]{};
        cog_pop_vector(ctx,xyz);
        void *thing=cog_pop_thing(ctx);
        uint32_t signature=0;
        if(!sprite_cog_thing_identity(thing,signature)) return;
        jkgm::sprite_instance_info const info{xyz[0],xyz[1],xyz[2]};
        if(!sprite_instances.set(thing,signature,info,GetTickCount(),seconds))
            LOG_WARNING("SetSpriteInfo: invalid width, height, opacity or duration");
    }

    // GetSpriteInfo(thing): current independently interpolated width, height,
    // and opacity (defaults to 1,1,1 when no override is active).
    void __cdecl cog_get_sprite_info(void *ctx)
    {
        void *thing=cog_pop_thing(ctx);
        uint32_t signature=0;
        jkgm::sprite_instance_info info{};
        if(sprite_cog_thing_identity(thing,signature))
            info=sprite_instances.sample(thing,signature,GetTickCount());
        float const values[]={info.width,info.height,info.opacity};
        cog_push_vector(ctx,values);
    }

    // SetFogColor(vector rgb, float fadeTime)
    //   rgb components 0.0 - 1.0, e.g. '0.45 0.50 0.58'
    //   fadeTime in seconds; 0.0 changes the color immediately
    void __cdecl cog_set_fog_color(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx); // last argument comes off first
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, rgb);

        jkgm::get_fog_settings().color.fade_to({clamp_float(rgb[0], 0.0f, 1.0f),
                                                clamp_float(rgb[1], 0.0f, 1.0f),
                                                clamp_float(rgb[2], 0.0f, 1.0f)},
                                               fade_time);
    }

    // vector GetFogColor()    the color showing right now (partway through a fade, if fading)
    void __cdecl cog_get_fog_color(void *ctx)
    {
        auto const rgb = jkgm::get_fog_settings().color.value();
        cog_push_vector(ctx, rgb.data());
    }

    // SetFogInfo(vector info, float fadeTime)
    //   info = 'start end opacity': start distance, end distance, opacity 0.0 - 1.0
    //   fadeTime in seconds; 0.0 changes the values immediately
    void __cdecl cog_set_fog_info(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float info[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, info);

        float const start = info[0] < 0.0f ? 0.0f : info[0];
        float const end = info[1] < start + 0.001f ? start + 0.001f : info[1];
        float const opacity = clamp_float(info[2], 0.0f, 1.0f);

        jkgm::get_fog_settings().info.fade_to({start, end, opacity}, fade_time);
    }

    // vector GetFogInfo()    'start end opacity' showing right now
    void __cdecl cog_get_fog_info(void *ctx)
    {
        auto const info = jkgm::get_fog_settings().info.value();
        cog_push_vector(ctx, info.data());
    }

    // ResetFog()    back to the defaults in fog.hpp (fog disabled), immediately
    void __cdecl cog_reset_fog(void * /*ctx*/)
    {
        jkgm::get_fog_settings() = jkgm::fog_settings();
    }

    // SetSunInfo(vector direction, vector color, float intensity, float animTime)
    // Direction points TOWARD the sun in world space; color is normalized RGB.
    void __cdecl cog_set_sun_info(void *ctx)
    {
        float const duration = cog_pop_float(ctx);
        float const intensity = cog_pop_float(ctx);
        float rgb[3] = {1.0f, 1.0f, 1.0f};
        float dir[3] = {0.0f, 0.0f, 1.0f};
        cog_pop_vector(ctx, rgb);
        cog_pop_vector(ctx, dir);

        if(!std::isfinite(intensity) || !std::isfinite(rgb[0]) ||
           !std::isfinite(rgb[1]) || !std::isfinite(rgb[2]) ||
           !std::isfinite(dir[0]) || !std::isfinite(dir[1]) ||
           !std::isfinite(dir[2])) {
            LOG_WARNING("SetSunInfo received non-finite arguments");
            return;
        }
        float const sq = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
        if(!(sq > 1.0e-10f) || !std::isfinite(sq)) {
            LOG_WARNING("SetSunInfo requires a nonzero finite direction");
            return;
        }

        auto const unit = jkgm::normalized_sun_direction({dir[0],dir[1],dir[2]});
        auto const now = jkgm::effect_clock_now();
        float const time = std::isfinite(duration) ? std::max(duration, 0.0f) : 0.0f;
        auto &sun = jkgm::get_sun_settings();
        sun.direction.fade_to(unit, time, now);
        sun.color.fade_to({clamp_float(rgb[0], 0.0f, 1.0f),
                           clamp_float(rgb[1], 0.0f, 1.0f),
                           clamp_float(rgb[2], 0.0f, 1.0f)}, time, now);
        sun.intensity.fade_to({clamp_float(intensity, 0.0f, 16.0f)}, time, now);
    }

    void __cdecl cog_get_sun_direction(void *ctx)
    {
        auto const dir = jkgm::normalized_sun_direction(
            jkgm::get_sun_settings().direction.value());
        cog_push_vector(ctx, dir.data());
    }
    void __cdecl cog_get_sun_color(void *ctx)
    {
        auto const rgb = jkgm::get_sun_settings().color.value();
        cog_push_vector(ctx, rgb.data());
    }
    void __cdecl cog_get_sun_intensity(void *ctx)
    {
        cog_push_float(ctx, jkgm::get_sun_settings().intensity.value()[0]);
    }
    void __cdecl cog_reset_sun(void * /*ctx*/)
    {
        jkgm::get_sun_settings() = jkgm::sun_settings();
    }

    // SetFilmGrain(vector params, float fadeTime)
    //   params = 'amount size rate'
    //     amount: 0.0 - 1.0, 0 turns grain off (0.05 - 0.15 is a typical range)
    //     size:   grain size in screen pixels, 1.0 or more
    //     rate:   grain pattern changes per second, 0 freezes it (24 looks filmic)
    //   fadeTime in seconds; 0.0 changes the values immediately
    void __cdecl cog_set_film_grain(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float params[3] = {0.0f, 1.0f, 24.0f};
        cog_pop_vector(ctx, params);

        jkgm::get_film_grain_settings().params.fade_to(
            {clamp_float(params[0], 0.0f, 1.0f),
             params[1] < 1.0f ? 1.0f : params[1],
             params[2] < 0.0f ? 0.0f : params[2]},
            fade_time);
    }

    // vector GetFilmGrain()    'amount size rate' showing right now
    void __cdecl cog_get_film_grain(void *ctx)
    {
        auto const params = jkgm::get_film_grain_settings().params.value();
        cog_push_vector(ctx, params.data());
    }

    // ResetFilmGrain()    grain off, immediately
    void __cdecl cog_reset_film_grain(void * /*ctx*/)
    {
        jkgm::get_film_grain_settings() = jkgm::film_grain_settings();
    }

    // SetVignette(vector rgb, float strength, float fadeTime)
    //   rgb:      color the screen edges are pulled towards, 0.0 - 1.0 each
    //             ('0 0 0' is the classic dark vignette, '1 0 0' a red damage flash)
    //   strength: 0.0 - 2.0; 0 = off, 1 = classic vignette, between 1 and 2 it closes in like an
    //             iris until the whole screen is the vignette color at 2
    //   fadeTime: seconds; 0.0 changes the values immediately
    void __cdecl cog_set_vignette(void *ctx)
    {
        // Arguments come off in reverse order: fadeTime, strength, then rgb.
        float const fade_time = cog_pop_float(ctx);
        float const strength = clamp_float(cog_pop_float(ctx), 0.0f, 2.0f);
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, rgb);

        std::array<float, 4> const target = {clamp_float(rgb[0], 0.0f, 1.0f),
                                             clamp_float(rgb[1], 0.0f, 1.0f),
                                             clamp_float(rgb[2], 0.0f, 1.0f),
                                             strength};

        auto &params = jkgm::get_vignette_settings().params;

        // If the vignette is invisible right now, take on the new color straight away and fade
        // only the strength. Otherwise fading in from black to red would pass through dark red.
        auto start = params.value();
        if(start[3] <= 0.001f) {
            start[0] = target[0];
            start[1] = target[1];
            start[2] = target[2];
            params.set(start);
        }

        params.fade_to(target, fade_time);
    }

    // vector GetVignetteRGB()    the edge color right now
    void __cdecl cog_get_vignette_rgb(void *ctx)
    {
        auto const params = jkgm::get_vignette_settings().params.value();
        float const rgb[3] = {params[0], params[1], params[2]};
        cog_push_vector(ctx, rgb);
    }

    // float GetVignetteStrength()    the strength right now
    void __cdecl cog_get_vignette_strength(void *ctx)
    {
        cog_push_float(ctx, jkgm::get_vignette_settings().params.value()[3]);
    }

    // ResetVignette()    disable vignette immediately (vanilla JK has none)
    void __cdecl cog_reset_vignette(void * /*ctx*/)
    {
        jkgm::get_vignette_settings() = jkgm::vignette_settings();
    }

    // SetColorFilter(vector params, float fadeTime)
    //   params = 'brightness contrast saturation'
    //     brightness: -1.0 - 1.0 (0 = unchanged)
    //     contrast:   0.0 - 4.0 (1 = unchanged)
    //     saturation: 0.0 - 4.0 (1 = unchanged, 0 = black and white)
    //   fadeTime in seconds; 0.0 changes the values immediately
    void __cdecl cog_set_color_filter(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float params[3] = {0.0f, 1.0f, 1.0f};
        cog_pop_vector(ctx, params);

        jkgm::get_color_filter_settings().params.fade_to({clamp_float(params[0], -1.0f, 1.0f),
                                                          clamp_float(params[1], 0.0f, 4.0f),
                                                          clamp_float(params[2], 0.0f, 4.0f)},
                                                         fade_time);
    }

    // vector GetColorFilter()    'brightness contrast saturation' right now
    void __cdecl cog_get_color_filter(void *ctx)
    {
        auto const params = jkgm::get_color_filter_settings().params.value();
        cog_push_vector(ctx, params.data());
    }

    // ResetColorFilter()    back to the defaults in postfx.hpp, immediately
    void __cdecl cog_reset_color_filter(void * /*ctx*/)
    {
        jkgm::get_color_filter_settings() = jkgm::color_filter_settings();
    }

    // SetFrameTrail(float strength, float fadeTime)
    //   strength: 0 = no history; 1 = freeze the last composited frame
    //   fadeTime: interpolation time in seconds (0 = immediate)
    void __cdecl cog_set_frame_trail(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float const strength = clamp_float(cog_pop_float(ctx), 0.0f, 1.0f);
        jkgm::get_frame_blend_settings().strength.fade_to({strength}, fade_time);
    }

    // float GetFrameTrail()
    void __cdecl cog_get_frame_trail(void *ctx)
    {
        cog_push_float(ctx, jkgm::get_frame_blend_settings().strength.value()[0]);
    }

    // ResetFrameTrail() = disable temporal blending and discard old frame history.
    void __cdecl cog_reset_frame_trail(void * /*ctx*/)
    {
        jkgm::get_frame_blend_settings().reset();
    }

    // SetDistanceBlur(vector(start,end,strength), float fadeTime)
    // Distances in JK units, strength 0..1.
    void __cdecl cog_set_distance_blur(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float params[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, params);

        float const start = std::max(params[0], 0.0f);
        float const end = std::max(params[1], start + 0.001f);
        jkgm::get_distance_blur_settings().params.fade_to(
            {start, end, clamp_float(params[2], 0.0f, 1.0f)}, fade_time);
    }

    // vector GetDistanceBlur()
    void __cdecl cog_get_distance_blur(void *ctx)
    {
        auto const params = jkgm::get_distance_blur_settings().params.value();
        cog_push_vector(ctx, params.data());
    }

    // ResetDistanceBlur()
    void __cdecl cog_reset_distance_blur(void * /*ctx*/)
    {
        jkgm::get_distance_blur_settings() = jkgm::distance_blur_settings();
    }

    // SetPeripheralBlur(vector(inner,outer,strength), float fadeTime)
    // Radii are normalized screen-center-to-corner distances, 0..1.
    void __cdecl cog_set_peripheral_blur(void *ctx)
    {
        float const fade_time = cog_pop_float(ctx);
        float params[3] = {0.0f, 0.0f, 0.0f};
        cog_pop_vector(ctx, params);

        float const inner = clamp_float(params[0], 0.0f, 1.0f);
        float const outer = clamp_float(params[1], inner + 0.001f, 1.001f);
        jkgm::get_peripheral_blur_settings().params.fade_to(
            {inner, outer, clamp_float(params[2], 0.0f, 1.0f)}, fade_time);
    }

    // vector GetPeripheralBlur()
    void __cdecl cog_get_peripheral_blur(void *ctx)
    {
        auto const params = jkgm::get_peripheral_blur_settings().params.value();
        cog_push_vector(ctx, params.data());
    }

    // ResetPeripheralBlur()
    void __cdecl cog_reset_peripheral_blur(void * /*ctx*/)
    {
        jkgm::get_peripheral_blur_settings() = jkgm::peripheral_blur_settings();
    }

    // Associate commands issued from a level's startup COG with that world
    // *before* the renderer's next frame notices the level transition.
    jkgm::runtime_material_controls &cog_material_controls()
    {
        uint32_t world = 0;
        if(jkgm::engine_material_debug::matches_tested_executable() &&
           jkgm::engine_material_debug::read_object(
               jkgm::engine_material_debug::current_world_global, world)) {
            jkgm::get_runtime_material_controls().prepare_world(world);
        }
        return jkgm::get_runtime_material_controls();
    }

    // Runtime material effects use packed JK MAT indices. This is the same
    // integer embedded in a COG material variable; PopMaterial in the engine
    // returns its native pointer, but PopInt returns its stable numeric index.
    bool valid_mat_index(int id)
    {
        return id >= 0 && id <= 65535 && (id & 0x7fff) < 16384;
    }

    bool valid_image_id(int id)
    {
        return id >= 0 && id <= 65535;
    }

    void __cdecl cog_set_global_env_map(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int image = cog_pop_int(ctx);
        int source = cog_pop_int(ctx);
        if(valid_mat_index(source) && valid_image_id(image)) {
            cog_material_controls().set_global_env(
                static_cast<uint32_t>(source), image, fade);
            LOG_DEBUG("COG SetGlobalEnvMap: source MAT=", source,
                      " image=", image, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG SetGlobalEnvMap rejected: source MAT=", source,
                         " image=", image);
    }

    // JKGM MAT residency: use the EXACT rdMaterial_AddToTextureCache
    // entrypoint in the user's SHA-audited executable (0x0044ACC0).
    // Verified independently against its native rdCache calls at
    // 0x0043B853 / 0x0043B914 and the std3D uploader at 0x0042A330.
    // 0x0044AA70 is UNRELATED code: invoking it previously crashed Wine.
    using jkgm_rd_material_cache_fn=int(__cdecl *)(void*,void*,int,int);
    constexpr uintptr_t jkgm_mat_cache_addr=0x0044ACC0U;
    bool spotlight_native_mat_cache_verified()
    {
        static bool const verified=[] {
            using namespace jkgm::engine_material_debug;
            constexpr uint8_t entry[]{
                0x8b,0x4c,0x24,0x08,0x8b,0x44,0x24,0x0c,
                0x56,0x8b,0x74,0x24,0x14,0x8b,0x54,0x81
            };
            constexpr uint8_t upload_alpha[]{
                0xe8,0x34,0xf6,0xfd,0xff
            };
            constexpr uint8_t upload_opaque[]{
                0xe8,0xef,0xf5,0xfd,0xff
            };
            constexpr uint8_t uploader[]{
                0x55,0x8b,0xec,0x81,0xec,0x58,0x01,0x00,
                0x00,0x53,0x8b,0x5d,0x08
            };
            constexpr uint8_t native_caller[]{
                0x8b,0x45,0xb0,0x50,0x57,0x56,0x53,
                0xe8,0x68,0xf4,0x00,0x00,0x83,0xc4,0x10
            };
            return matches_tested_executable() &&
                   has_signature(jkgm_mat_cache_addr,entry,sizeof(entry)) &&
                   has_signature(0x0044ACF7U,upload_alpha,sizeof(upload_alpha)) &&
                   has_signature(0x0044AD3CU,upload_opaque,sizeof(upload_opaque)) &&
                   has_signature(0x0042A330U,uploader,sizeof(uploader)) &&
                   has_signature(0x0043B84CU,native_caller,sizeof(native_caller));
        }();
        return verified;
    }

    // Called once when the COG message executes, or retried during opaque
    // composition if that message ran before OpenGL/native D3D was ready.
    // On success JKGM's existing DDraw intercept creates exactly the same
    // texture-cache handle/pack mapping used by a normally rendered MAT.
    bool spotlight_warm_native_mat(uint32_t mat_id)
    {
        using namespace jkgm::engine_material_debug;
        if(!spotlight_native_mat_cache_verified() ||
           !wglGetCurrentContext())return false;
        // std3D_AddToTextureCache dereferences JK's initialized D3D device.
        uint32_t native_device=0;
        if(!read_object(0x0055C7ECU,native_device) || !native_device)
            return false;
        bool const shared=(mat_id&0x8000U)!=0U;
        unsigned const index=mat_id&0x7fffU;
        uint32_t world_address=0;
        world_header world{};
        if(!load_world(shared?static_world_global:current_world_global,
                       world_address,world) ||
           index>=unsigned(world.num_materials))return false;
        uintptr_t const mat_address=uintptr_t(world.materials)+
                              index*sizeof(native_material);
        native_material mat{};
        if(!read_object(mat_address,mat) ||
           mat.id!=mat_id || (mat.tex_type&2U)==0U ||
           mat.num_texinfo==0U || mat.num_texinfo>16U ||
           mat.num_textures==0U || mat.num_textures>256U ||
           !mat.textures)return false;
        unsigned const cel=std::min(mat.current_cel,mat.num_texinfo-1U);
        native_texinfo info{};
        if(!mat.texinfo[cel] || !read_object(mat.texinfo[cel],info) ||
           (info.header[0]&8U)==0U || !info.texture ||
           info.texture<mat.textures)return false;
        uint64_t const texture_offset=uint64_t(info.texture)-mat.textures;
        if(texture_offset%sizeof(native_texture)!=0 ||
           texture_offset/sizeof(native_texture)>=mat.num_textures)
            return false;
        native_texture texture{};
        if(!read_object(info.texture,texture) ||
           texture.header[6]==0U || texture.header[6]>4U ||
           !texture.mipmap_buffers[0] ||
           !gpu_span_readable(reinterpret_cast<void const *>(
               uintptr_t(texture.mipmap_buffers[0])),0x18))
            return false;
        // Transparent MATs need the alpha texture cache; opaque ones
        // select the native no_alpha path (arg 4 = 1).
        bool const has_alpha=(texture.header[0]&1U)!=0U;
        auto const resident=[&](native_texture const &t)->bool {
            auto const &surface=has_alpha?t.alpha_mips[0]:t.opaque_mips[0];
            return surface.texture_loaded!=0 && surface.texture_handle>0;
        };
        // Never invoke native std3D for an already-resident texture.
        // JK can temporarily detach its D3D device during pause/unpause
        // while this native material still reports a valid cached handle.
        // Touching rdMaterial_AddToTextureCache in that window can enter
        // std3D's uploader with a null internal device pointer.
        if(resident(texture))
            return true;
        // Only the explicit SetSpotlight request can enter the native loader
        // for an *unloaded* MAT. In particular, frame rendering must not
        // refresh native texture-cache LRU: that was the unsafe call site.
        auto const native_load=reinterpret_cast<jkgm_rd_material_cache_fn>(
            jkgm_mat_cache_addr);
        if(!native_load(reinterpret_cast<void *>(mat_address),
                        reinterpret_cast<void *>(uintptr_t(info.texture)),
                        0,has_alpha?0:1))
            return false;
        native_texture updated{};
        if(!read_object(info.texture,updated) || !resident(updated))
            return false;
        LOG_INFO("Native MAT loaded on demand: id=",mat_id,
                 " cel=",cel," alpha=",has_alpha);
        return true;
    }
    // SetSpotlight(thing,mat,vector(width,height,range),localOffset).
    // Use MAT indices like other COG material verbs; the renderer resolves
    // the current texture cel only after its native texture is resident.
    void __cdecl cog_set_spotlight(void *ctx)
    {
        float offset[3]{};
        cog_pop_vector(ctx,offset);
        float dimensions[3]{};
        cog_pop_vector(ctx,dimensions);
        int const material=cog_pop_int(ctx);
        void *thing=cog_pop_thing(ctx);
        if(!thing||!gpu_span_readable(thing,0xe8)||
           !valid_mat_index(material)) {
            LOG_WARNING("SetSpotlight: invalid Thing or MAT");
            return;
        }
        uint32_t kind=0,signature=0;
        auto const *p=static_cast<uint8_t const *>(thing);
        std::memcpy(&kind,p+0x0c,4);
        std::memcpy(&signature,p+0x74,4);
        if(kind==0 || kind>15) return;
        uint32_t world=0;
        if(jkgm::engine_material_debug::matches_tested_executable() &&
           jkgm::engine_material_debug::read_object(
               jkgm::engine_material_debug::current_world_global,world))
            jkgm::get_spotlight_registry().prepare_world(world);
        jkgm::spotlight_request request{};
        request.thing=reinterpret_cast<uintptr_t>(thing);
        request.identity=signature;
        request.material=static_cast<uint32_t>(material);
        request.width=dimensions[0];
        request.height=dimensions[1];
        request.range=dimensions[2];
        request.offset={{offset[0],offset[1],offset[2]}};
        if(!jkgm::get_spotlight_registry().set(request)) {
            LOG_WARNING("SetSpotlight: invalid dimensions/range or 4-light limit reached");
        }
        else {
            bool const resident=spotlight_warm_native_mat(request.material);
            LOG_INFO("SetSpotlight: MAT=",material,
                     " registered native_resident=",resident,
                     " (native loading occurs only in SetSpotlight)");
        }
    }

    // SetSpotlightIntensities(thing,light,add,fadetime).
    // The light channel illuminates the receiving material's original
    // pigment; add is a separate raw cookie-radiance contribution.
    void __cdecl cog_set_spotlight_intensities(void *ctx)
    {
        float const fade=cog_pop_float(ctx);
        float const add=cog_pop_float(ctx);
        float const light=cog_pop_float(ctx);
        void *thing=cog_pop_thing(ctx);
        if(!thing || !gpu_span_readable(thing,0xe8))return;
        uint32_t kind=0,signature=0;
        auto const *p=static_cast<uint8_t const *>(thing);
        std::memcpy(&kind,p+0x0c,4);
        std::memcpy(&signature,p+0x74,4);
        if(kind==0 || kind>15 || signature==0)return;
        uint32_t world=0;
        if(jkgm::engine_material_debug::matches_tested_executable() &&
           jkgm::engine_material_debug::read_object(
               jkgm::engine_material_debug::current_world_global,world))
            jkgm::get_spotlight_registry().prepare_world(world);
        if(!jkgm::get_spotlight_registry().set_intensities(
                reinterpret_cast<uintptr_t>(thing),signature,
                {light,add},fade,GetTickCount()))
            LOG_WARNING("SetSpotlightIntensities: no matching active light "
                        "or invalid strengths/fade (0..16, 0..3600s)");
    }

    // ClearSpotlight(thing): unregister this Thing's projector immediately.
    // ResetSpotlight remains an alias for older COG scripts. This erases the
    // request without touching the native MAT or Direct3D texture cache.
    void __cdecl cog_clear_spotlight(void *ctx)
    {
        void *thing=cog_pop_thing(ctx);
        bool const removed=thing &&
            jkgm::get_spotlight_registry().erase_thing(
                reinterpret_cast<uintptr_t>(thing));
        LOG_DEBUG("COG ClearSpotlight: thing=",
                  reinterpret_cast<uintptr_t>(thing),
                  " removed=",removed);
    }

    void __cdecl cog_reset_global_env_map(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        cog_material_controls().reset_global_env(fade);
        LOG_DEBUG("COG ResetGlobalEnvMap: fade=", std::to_string(fade));
    }

    void __cdecl cog_set_mat_env_map(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int image = cog_pop_int(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat) && valid_image_id(image)) {
            cog_material_controls().set_mat_env(
                static_cast<uint32_t>(mat), static_cast<uint32_t>(mat), image, fade);
            LOG_DEBUG("COG SetMatEnvMap: MAT=", mat, " image=", image, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG SetMatEnvMap rejected: MAT=", mat, " image=", image);
    }

    // Resolve image in sourceMat's pack, apply to targetMat.
    void __cdecl cog_set_mat_env_map_from(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int image = cog_pop_int(ctx);
        int source = cog_pop_int(ctx);
        int target = cog_pop_int(ctx);
        if(valid_mat_index(target) && valid_mat_index(source) && valid_image_id(image)) {
            cog_material_controls().set_mat_env(
                static_cast<uint32_t>(target), static_cast<uint32_t>(source), image, fade);
            LOG_DEBUG("COG SetMatEnvMapFrom: target=", target, " source=", source,
                      " image=", image, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG SetMatEnvMapFrom rejected: target=", target,
                         " source=", source, " image=", image);
    }

    void __cdecl cog_reset_mat_env_map(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat)) {
            cog_material_controls().reset_mat_env(
                static_cast<uint32_t>(mat), fade);
            LOG_DEBUG("COG ResetMatEnvMap: MAT=", mat, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG ResetMatEnvMap rejected: MAT=", mat);
    }

    void __cdecl cog_set_material_albedo(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int image = cog_pop_int(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat) && valid_image_id(image)) {
            cog_material_controls().set_mat_albedo(
                static_cast<uint32_t>(mat), image, fade);
            LOG_DEBUG("COG SetMaterialAlbedo: MAT=", mat,
                      " image=", image, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG SetMaterialAlbedo rejected: MAT=", mat,
                         " image=", image);
    }

    void __cdecl cog_reset_material_albedo(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat)) {
            cog_material_controls().reset_mat_albedo(
                static_cast<uint32_t>(mat), fade);
            LOG_DEBUG("COG ResetMaterialAlbedo: MAT=", mat, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG ResetMaterialAlbedo rejected: MAT=", mat);
    }

    void __cdecl cog_set_material_color(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        float rgb[3]{1.0f, 1.0f, 1.0f};
        cog_pop_vector(ctx, rgb);
        int mat = cog_pop_int(ctx);
        if(!valid_mat_index(mat) ||
           !std::isfinite(rgb[0]) || !std::isfinite(rgb[1]) || !std::isfinite(rgb[2])) {
            LOG_WARNING("COG SetMaterialColor rejected: MAT=", mat,
                        " rgb=", std::to_string(rgb[0]), ",", std::to_string(rgb[1]), ",", std::to_string(rgb[2]));
            return;
        }
        cog_material_controls().set_mat_color(
            static_cast<uint32_t>(mat),
            {{clamp_float(rgb[0], 0.0f, 4.0f),
              clamp_float(rgb[1], 0.0f, 4.0f),
              clamp_float(rgb[2], 0.0f, 4.0f)}},
            fade);
        LOG_DEBUG("COG SetMaterialColor: MAT=", mat,
                  " rgb=", std::to_string(rgb[0]), ",", std::to_string(rgb[1]), ",", std::to_string(rgb[2]), " fade=", std::to_string(fade));
    }

    void __cdecl cog_reset_material_color(void *ctx)
    {
        float fade = cog_pop_float(ctx);
        int mat = cog_pop_int(ctx);
        if(valid_mat_index(mat)) {
            cog_material_controls().reset_mat_color(
                static_cast<uint32_t>(mat), fade);
            LOG_DEBUG("COG ResetMaterialColor: MAT=", mat, " fade=", std::to_string(fade));
        }
        else LOG_WARNING("COG ResetMaterialColor rejected: MAT=", mat);
    }

    struct cog_verb_entry {
        char const *name;
        cog_handler_type handler;
    };

    // The array size is deduced from the list, so adding or removing a verb can never leave
    // empty entries behind (those would be registered with a null name and handler).
    constexpr std::array fx_verbs = {
        cog_verb_entry{"min", cog_math_min},
        cog_verb_entry{"max", cog_math_max},
        cog_verb_entry{"clamp", cog_math_clamp},
        cog_verb_entry{"sign", cog_math_sign},
        cog_verb_entry{"lerp", cog_math_lerp},
        cog_verb_entry{"setfogcolor", cog_set_fog_color},
        cog_verb_entry{"getfogcolor", cog_get_fog_color},
        cog_verb_entry{"setfoginfo", cog_set_fog_info},
        cog_verb_entry{"getfoginfo", cog_get_fog_info},
        cog_verb_entry{"resetfog", cog_reset_fog},
        cog_verb_entry{"setsuninfo", cog_set_sun_info},
        cog_verb_entry{"getsundirection", cog_get_sun_direction},
        cog_verb_entry{"getsuncolor", cog_get_sun_color},
        cog_verb_entry{"getsunintensity", cog_get_sun_intensity},
        cog_verb_entry{"resetsun", cog_reset_sun},
        cog_verb_entry{"setfilmgrain", cog_set_film_grain},
        cog_verb_entry{"getfilmgrain", cog_get_film_grain},
        cog_verb_entry{"resetfilmgrain", cog_reset_film_grain},
        cog_verb_entry{"setvignette", cog_set_vignette},
        cog_verb_entry{"getvignettergb", cog_get_vignette_rgb},
        cog_verb_entry{"getvignettestrength", cog_get_vignette_strength},
        cog_verb_entry{"resetvignette", cog_reset_vignette},
        cog_verb_entry{"setcolorfilter", cog_set_color_filter},
        cog_verb_entry{"getcolorfilter", cog_get_color_filter},
        cog_verb_entry{"resetcolorfilter", cog_reset_color_filter},
        cog_verb_entry{"setframetrail", cog_set_frame_trail},
        cog_verb_entry{"getframetrail", cog_get_frame_trail},
        cog_verb_entry{"resetframetrail", cog_reset_frame_trail},
        cog_verb_entry{"setdistanceblur", cog_set_distance_blur},
        cog_verb_entry{"getdistanceblur", cog_get_distance_blur},
        cog_verb_entry{"resetdistanceblur", cog_reset_distance_blur},
        cog_verb_entry{"setperipheralblur", cog_set_peripheral_blur},
        cog_verb_entry{"getperipheralblur", cog_get_peripheral_blur},
        cog_verb_entry{"resetperipheralblur", cog_reset_peripheral_blur},
        cog_verb_entry{"setspotlight", cog_set_spotlight},
        cog_verb_entry{"setspotlightintensities", cog_set_spotlight_intensities},
        cog_verb_entry{"clearspotlight", cog_clear_spotlight},
        cog_verb_entry{"resetspotlight", cog_clear_spotlight},
        cog_verb_entry{"setglobalenvmap", cog_set_global_env_map},
        cog_verb_entry{"resetglobalenvmap", cog_reset_global_env_map},
        cog_verb_entry{"setmatenvmap", cog_set_mat_env_map},
        cog_verb_entry{"setmatenvmapfrom", cog_set_mat_env_map_from},
        cog_verb_entry{"resetmatenvmap", cog_reset_mat_env_map},
        cog_verb_entry{"setmaterialalbedo", cog_set_material_albedo},
        cog_verb_entry{"resetmaterialalbedo", cog_reset_material_albedo},
        cog_verb_entry{"setmaterialcolor", cog_set_material_color},
        cog_verb_entry{"resetmaterialcolor", cog_reset_material_color},
        cog_verb_entry{"setspriteinfo", cog_set_sprite_info},
        cog_verb_entry{"getspriteinfo", cog_get_sprite_info},
        cog_verb_entry{"setspriteroll", cog_set_sprite_roll},
        cog_verb_entry{"getspriteroll", cog_get_sprite_roll},
        cog_verb_entry{"aigetfiretarget", cog_ai_get_fire_target},
        // Replace the original misleading AIGetMovePos with the commanded
        // movement destination. The extended spelling remains an alias.
        cog_verb_entry{"aigetmovepos", cog_ai_get_move_target_pos},
        cog_verb_entry{"aigetmovetargetpos", cog_ai_get_move_target_pos},
        cog_verb_entry{"aigetlookdir", cog_ai_get_look_dir},
        cog_verb_entry{"getthingattachnormal", cog_get_thing_attach_normal},
        cog_verb_entry{"attachthingtothingnode", cog_attach_thing_to_thing_node},
        cog_verb_entry{"getthingnodepos", cog_get_thing_node_pos},
        cog_verb_entry{"getthingnodervec", cog_get_thing_node_rvec},
        cog_verb_entry{"getthingnodelvec", cog_get_thing_node_lvec},
        cog_verb_entry{"getthingnodeuvec", cog_get_thing_node_uvec},
        cog_verb_entry{"getthingnodepyr", cog_get_thing_node_pyr},
    };

    JkInitNewCogVerbs_type TrueJkInitNewCogVerbs = nullptr;

    void __cdecl HookedJkInitNewCogVerbs(cog_register_verb_type register_verb, void *ctx)
    {
        if(the_config && the_config->use_model_cache &&
           the_config->mesh_revision_tracking && register_verb) {
            mesh_revision::begin(register_verb);
            TrueJkInitNewCogVerbs(mesh_revision::register_mutator,ctx);
            mesh_revision::finish();
        } else {
            TrueJkInitNewCogVerbs(register_verb,ctx);
        }

        if(!cog_helpers_match()) {
            LOG_ERROR("jk.exe does not match the build the fog COG verbs were written for; "
                      "fog COG verbs not registered");
            return;
        }

        for(auto const &verb : fx_verbs) {
            register_verb(ctx, verb.handler, verb.name);
        }

        LOG_DEBUG("Registered math, fog, sun, spotlight, post-processing, material, SPR, AI and ThingNode attachment COG verbs");
    }
}

// Snapshot active spotlight owners after native animations have advanced.
// COG stores stable Thing identity; reused/deleted pointers are never read as
// different Things, and invalid animated node poses simply skip a light.
std::vector<jkgm::spotlight_instance> jkgm::collect_spotlights_for_frame()
{
    std::vector<spotlight_instance> instances;
    if(!the_config || !the_config->enable_spotlights)return instances;
    uint32_t world=0;
    if(engine_material_debug::matches_tested_executable() &&
       engine_material_debug::read_object(
           engine_material_debug::current_world_global,world))
        get_spotlight_registry().prepare_world(world);
    for(auto const &request:get_spotlight_registry().snapshot()) {
        auto const *thing=reinterpret_cast<uint8_t const *>(request.thing);
        if(!thing||!gpu_span_readable(thing,0xe8)) {
            get_spotlight_registry().erase_thing(request.thing);
            continue;
        }
        uint32_t kind=0,identity=0;
        std::memcpy(&kind,thing+0x0c,4);
        std::memcpy(&identity,thing+0x74,4);
        if(kind==0||kind>15||identity!=request.identity) {
            // Reclaim active-light slots as soon as JK recycles an owner.
            get_spotlight_registry().erase_thing(request.thing);
            continue;
        }
        thing_node_pose pose{};
        gpu_3do_matrix34 m{};
        std::memcpy(m.data(),thing+0x88,9*sizeof(float));
        std::memcpy(m.data()+9,thing+0xb8,3*sizeof(float));
        if(!get_thing_node_pose(m,pose))continue;
        // IMPORTANT: Never call the native MAT preloader from frame rendering.
        // DirectDraw device teardown/recreation during pause/unpause can leave
        // native std3D internals null even while the spotlight's owner and MAT
        // pointers remain readable. Resolve only currently resident images;
        // native MAT loading remains an explicit SetSpotlight-time attempt.
        spotlight_instance instance{};
        if(spotlight_make_instance(request,pose,instance,GetTickCount())) {
            // Keep exactly the same verified attachment-root identity as
            // the native rdModel3 GPU caster snapshot.
            instance.shadow_owner=spotlight_shadow_family_key(
                (uint64_t(uint32_t(request.thing))<<32)|request.identity);
            instances.push_back(instance);
        }
    }
    return instances;
}

bool attach_hooks()
{
    DetourRestoreAfterWith();
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // Injected functions

    // COG verbs, if the community patch's JK.dll is loaded (its export #1 is InitNewCogVerbs)
    if(HMODULE jk_dll = GetModuleHandleA("JK.dll")) {
        TrueJkInitNewCogVerbs =
            (JkInitNewCogVerbs_type)GetProcAddress(jk_dll, MAKEINTRESOURCEA(1));
        TrueJkInitNewCogMessages=reinterpret_cast<jk_init_messages_fn>(
            GetProcAddress(jk_dll,MAKEINTRESOURCEA(12)));
        if(TrueJkInitNewCogMessages && key_callback_signatures_match()) {
            LONG const msg_err=DetourAttach(
                &(PVOID &)TrueJkInitNewCogMessages,HookedJkInitNewCogMessages);
            if(msg_err!=NO_ERROR) {
                LOG_WARNING("Could not attach KEY COG message registration hook: ",msg_err);
            }
            else {
                LONG const cb_err=DetourAttach(
                    &(PVOID &)RealPuppetAdvance,HookedPuppetAdvance);
                if(cb_err!=NO_ERROR) {
                    DetourDetach(&(PVOID &)TrueJkInitNewCogMessages,
                                 HookedJkInitNewCogMessages);
                    LOG_WARNING("Could not attach KEY callback event hook: ",cb_err);
                }
                else {
                    key_callback_hook_active=true;
                    key_callback_registration_hook_active=true;
                }
            }
        }
        else LOG_WARNING("KEY callback hook unavailable: unsupported JK.dll / game");
        if(TrueJkInitNewCogVerbs != nullptr) {
            DetourAttach(&(PVOID &)TrueJkInitNewCogVerbs, HookedJkInitNewCogVerbs);
        }
        else {
            LOG_DEBUG("JK.dll does not export InitNewCogVerbs; fog COG verbs unavailable");
        }
    }
    else {
        LOG_DEBUG("JK.dll is not loaded; fog COG verbs unavailable");
    }

    // DirectX
    TrueDirectDrawCreate =
        (DirectDrawCreate_type)DetourFindFunction("ddraw.dll", "DirectDrawCreate");
    DetourAttach(&(PVOID &)TrueDirectDrawCreate, HookedDirectDrawCreate);

    TrueDirectDrawEnumerateA =
        (DirectDrawEnumerateA_type)DetourFindFunction("ddraw.dll", "DirectDrawEnumerateA");
    DetourAttach(&(PVOID &)TrueDirectDrawEnumerateA, HookedDirectDrawEnumerateA);

    // Win32 API
    TrueCreateWindowExA = (CreateWindowExA_type)DetourFindFunction("user32.dll", "CreateWindowExA");
    DetourAttach(&(PVOID &)TrueCreateWindowExA, HookedCreateWindowExA);

    TrueGetCursorPos = (GetCursorPos_type)DetourFindFunction("user32.dll", "GetCursorPos");
    DetourAttach(&(PVOID &)TrueGetCursorPos, HookedGetCursorPos);

    TrueCreateDIBSection =
        (CreateDIBSection_type)DetourFindFunction("gdi32.dll", "CreateDIBSection");
    DetourAttach(&(PVOID &)TrueCreateDIBSection, HookedCreateDIBSection);

    TrueSetDIBColorTable =
        (SetDIBColorTable_type)DetourFindFunction("gdi32.dll", "SetDIBColorTable");
    DetourAttach(&(PVOID &)TrueSetDIBColorTable, HookedSetDIBColorTable);

    TrueGdiFlush = (GdiFlush_type)DetourFindFunction("gdi32.dll", "GdiFlush");
    DetourAttach(&(PVOID &)TrueGdiFlush, HookedGdiFlush);

    TrueDeleteObject = (DeleteObject_type)DetourFindFunction("gdi32.dll", "DeleteObject");
    DetourAttach(&(PVOID &)TrueDeleteObject, HookedDeleteObject);

    TrueChangeDisplaySettingsA =
        (ChangeDisplaySettingsA_type)DetourFindFunction("user32.dll", "ChangeDisplaySettingsA");
    DetourAttach(&(PVOID &)TrueChangeDisplaySettingsA, HookedChangeDisplaySettingsA);

    TrueChangeDisplaySettingsW =
        (ChangeDisplaySettingsW_type)DetourFindFunction("user32.dll", "ChangeDisplaySettingsW");
    DetourAttach(&(PVOID &)TrueChangeDisplaySettingsW, HookedChangeDisplaySettingsW);

    // Unlike the GPU-only mesh intercept, forcing native MAT mip 0 applies
    // equally to JK-rendered sectors. JkGRfxMod generates OpenGL mipmaps.
    bool mip_hook_requested=false;
    if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))==0x00400000U &&
       std::memcmp(reinterpret_cast<void const *>(native_set_mip_distances_addr),
                   native_set_mip_distances_bytes,
                   sizeof(native_set_mip_distances_bytes))==0) {
        LONG const mip_error=DetourAttach(
            &(PVOID &)RealNativeSetMipDistances,HookedNativeSetMipDistances);
        if(mip_error==NO_ERROR) mip_hook_requested=true;
        else LOG_WARNING("Could not hook native MAT mip-distance setter: ",mip_error);
    }
    else {
        LOG_WARNING("Native MAT mip override disabled: unsupported executable");
    }

    bool const sprite_hooks_requested=request_native_sprite_hooks();
    bool const mesh_collision_hook_requested=request_type2_intersection_hook();
    bool const ice_thing_hook_requested=request_native_thingface_friction_hook();
    bool const earth_sound_hook_requested=request_native_earth_sound_hook();

    LONG error = DetourTransactionCommit();
    if(error!=NO_ERROR) {
        key_callback_hook_active=false;
        key_callback_registration_hook_active=false;
        key_callback_message_registered=false;
        sprite_frustum_hook_active=false;
        sprite_alpha_hook_active=false;
        sprite_variant_hooks_active=false;
        pending_sprite_flare_spans.clear();
        flare_draw_trace_count=flare_face_trace_count=flare_alpha_trace_count=
            flare_flush_trace_count=flare_bad_face_trace_count=0U;
    }
    if(error==NO_ERROR) native_sprite_hook_active=sprite_hooks_requested;
    if(error==NO_ERROR && mesh_collision_hook_requested) {
        native_mesh_collision_hook_active=true;
        LOG_INFO("JKGM experimental collide=2 Detour installed");
    }
    if(error==NO_ERROR && earth_sound_hook_requested) {
        native_earth_hit_sound_hook_active=true;
        LOG_INFO("JKGM HITEARTH native surface-impact branch installed");
    }
    if(error==NO_ERROR && ice_thing_hook_requested) {
        native_thingface_friction_hook_active=true;
        LOG_INFO("JKGM Thing-face icy flag enabled (0x80000000)");
    }
    if(error==NO_ERROR && mip_hook_requested) {
        native_mat_mips_hook_active=true;
        force_native_mat_mip_zero();
        LOG_INFO("Native MAT mip selection forced to level 0; OpenGL mipmaps remain active");
    }
    return error == NO_ERROR;
}

bool detach_hooks()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // Revert all SPR detours before releasing the DLL.
    if(sprite_variant_hooks_active) {
        DetourDetach(&(PVOID &)RealSithThingClone,HookedSithThingClone);
        DetourDetach(&(PVOID &)RealSithSpriteFreeWorld,
                     HookedSithSpriteFreeWorld);
        DetourDetach(&(PVOID &)RealSithSpriteLoad,HookedSithSpriteLoad);
        sprite_variant_hooks_active=false;
    }
    // Release variant rdSprite-owned arrays while native allocator/entry
    // trampolines remain valid; never release world-owned first entries.
    release_sprite_variants(nullptr);
    sprite_variant_roots.clear();
    if(sprite_alpha_hook_active) {
        DetourDetach(&(PVOID &)RealSpriteHWUpload,HookedSpriteHWUpload);
        DetourDetach(&(PVOID &)RealSpriteAlphaMid,SpriteAlphaThunk);
        DetourDetach(&(PVOID &)RealSpriteAddProc,HookedSpriteAddProc);
        sprite_alpha_hook_active=false;
        pending_sprite_flare_spans.clear();
    }
    if(sprite_frustum_hook_active) {
        DetourDetach(&(PVOID &)RealSpriteFrustum,HookedSpriteFrustum);
        sprite_frustum_hook_active=false;
    }
    if(native_sprite_hook_active) {
        DetourDetach(&(PVOID &)RealSpritePreclip,SpritePreclipThunk);
        DetourDetach(&(PVOID &)RealNativeSprite,HookedNativeSprite);
        DetourDetach(&(PVOID &)RealNativeSpriteFreeEntry,
                     HookedSpriteFreeEntry);
        DetourDetach(&(PVOID &)RealNativeSpriteFree,HookedSpriteFree);
        DetourDetach(&(PVOID &)RealNativeSpriteNewEntry,HookedSpriteNewEntry);
        native_sprite_hook_active=false;
        sprite_types.clear();
        sprite_instances.clear();
        sprite_decal_diagnostic_keys.clear();
        sprite_gpu_decals.clear();
        reset_static_sprite_decal_meshes(0);
        sprite_world_geometry.clear();
    }
    // Detach injected functions
    if(native_earth_hit_sound_hook_active) {
        DetourDetach(&(PVOID &)RealNativeEarthSound,NativeEarthSoundThunk);
        native_earth_hit_sound_hook_active=false;
    }
    if(native_thingface_friction_hook_active) {
        DetourDetach(&(PVOID&)RealThingFaceIceJoin,
                     ThingFaceIceMultiplierThunk);
        native_thingface_friction_hook_active=false;
    }
    if(native_mesh_collision_hook_active) {
        DetourDetach(&(PVOID&)RealType2Intersection,HookedType2Intersection);
        native_mesh_collision_hook_active=false;
    }
    if(native_mat_mips_hook_active) {
        DetourDetach(&(PVOID &)RealNativeSetMipDistances,HookedNativeSetMipDistances);
        native_mat_mips_hook_active=false;
    }
    if(key_callback_hook_active) {
        DetourDetach(&(PVOID &)RealPuppetAdvance,HookedPuppetAdvance);
        key_callback_hook_active=false;
    }
    if(key_callback_registration_hook_active) {
        DetourDetach(&(PVOID &)TrueJkInitNewCogMessages,
                     HookedJkInitNewCogMessages);
        key_callback_registration_hook_active=false;
        key_callback_message_registered=false;
    }
    if(TrueJkInitNewCogVerbs != nullptr) {
        DetourDetach(&(PVOID &)TrueJkInitNewCogVerbs, HookedJkInitNewCogVerbs);
    }
    DetourDetach(&(PVOID &)TrueDirectDrawCreate, HookedDirectDrawCreate);
    DetourDetach(&(PVOID &)TrueDirectDrawEnumerateA, HookedDirectDrawEnumerateA);
    DetourDetach(&(PVOID &)TrueCreateWindowExA, HookedCreateWindowExA);
    DetourDetach(&(PVOID &)TrueGetCursorPos, HookedGetCursorPos);
    DetourDetach(&(PVOID &)TrueCreateDIBSection, HookedCreateDIBSection);
    DetourDetach(&(PVOID &)TrueSetDIBColorTable, HookedSetDIBColorTable);
    DetourDetach(&(PVOID &)TrueGdiFlush, HookedGdiFlush);
    DetourDetach(&(PVOID &)TrueDeleteObject, HookedDeleteObject);
    DetourDetach(&(PVOID &)TrueChangeDisplaySettingsA, HookedChangeDisplaySettingsA);
    DetourDetach(&(PVOID &)TrueChangeDisplaySettingsW, HookedChangeDisplaySettingsW);

    LONG error = DetourTransactionCommit();
    return error == NO_ERROR;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
    if(DetourIsHelperProcess()) {
        return TRUE;
    }

    if(ul_reason_for_call == DLL_PROCESS_ATTACH) {
        jkgm::setup_default_logging();
        the_config = jkgm::load_config_file();

        if(the_config->log_path.has_value()) {
            jkgm::add_file_logging(*the_config->log_path);
        }

        // The dedicated JKGM executable can load renderer.dll before the
        // original game entrypoint. Keep the built-in Direct3D renderer fully
        // untouched when explicitly requested, including COG/Win32 hooks.
        if(the_config->use_vanilla_renderer) {
            LOG_INFO("use_vanilla_renderer=true: native JK Direct3D path selected");
            return TRUE;
        }

        the_renderer = jkgm::create_renderer(hModule, the_config.get());

        LOG_DEBUG("Attaching renderer to process");

        if(!attach_hooks()) {
            LOG_ERROR("Failed to commit Detours hook transaction");
            jkgm::report_error_message(
                "Critical failure: JkGfxMod failed to attach to this application");
            return FALSE;
        }

        renderer_hooks_active = true;
        native_node_attachment_hooks_active=attach_native_node_attachment_hooks();
        if(the_config->profile_renderer || the_config->use_model_cache ||
           the_config->enable_decals ||
           (the_config->enable_spotlights && the_config->enable_spotlight_shadows) ||
           the_config->enable_sun_shadows) {
            native_3do_hooks_active = attach_native_3do_hooks(the_config->profile_renderer);
        }
        LOG_DEBUG("Finished attaching renderer to process");
    }
    else if(ul_reason_for_call == DLL_PROCESS_DETACH && renderer_hooks_active) {
        if(native_node_attachment_hooks_active) {
            detach_native_node_attachment_hooks();
            native_node_attachment_hooks_active=false;
        }
        if(native_3do_hooks_active) {
            detach_native_3do_hooks();
            native_3do_hooks_active = false;
        }
        detach_hooks();
        renderer_hooks_active = false;
    }

    return TRUE;
}
