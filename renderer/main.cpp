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
#include "gpu_3do_preview.hpp"
#include "gpu_3do_frustum.hpp"
#include "gpu_3do_compat.hpp"
#include "gpu_3do_triangulation.hpp"
#include "gpu_3do_cache_signature.hpp"
#include "gpu_3do_colormap.hpp"
#include "jk_colormap_layout.hpp"
#include "native_mat_mips.hpp"
#include "engine_material_debug.hpp"
#include "sun.hpp"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <cstddef>
#include <string>
#include <detours/detours.h>

static std::unique_ptr<jkgm::renderer> the_renderer;
static std::unique_ptr<jkgm::config> the_config;
static bool renderer_hooks_active = false;

static bool native_3do_hooks_active = false;
static bool native_mat_mips_hook_active = false;

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
        if(start<0x10000U || start>0x7fffffffU) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if(VirtualQuery(p,&mbi,sizeof(mbi))==0 || mbi.State!=MEM_COMMIT ||
           (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        auto const end=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
        return start+len>=start && start+len<=end;
    }

    // An unflagged 3DO face can still have translucency in its MAT cel.
    // Build a separate draw range for those faces so alpha ordering can be
    // corrected later without ever changing the persistent index buffer.
    bool gpu_3do_face_may_have_alpha(native_face const &face)
    {
        if((face.type&0x2U)!=0) return true;
        if(!face.material) return false;
        using namespace jkgm::engine_material_debug;
        native_material mat{};
        if(!read_object(reinterpret_cast<uintptr_t>(face.material),mat) ||
           mat.num_texinfo==0 || mat.num_texinfo>16)
            return true; // uncertain: don't join its faces
        int32_t cel=static_cast<int32_t>(face.cel);
        if(cel<0) cel=static_cast<int32_t>(mat.current_cel);
        cel=std::max(0,std::min(cel,static_cast<int32_t>(mat.num_texinfo)-1));
        native_texinfo info{};
        if(!read_object(mat.texinfo[cel],info) || !info.texture)
            return true;
        native_texture tex{};
        if(!read_object(info.texture,tex)) return true;
        return (tex.header[0]&1U)!=0;
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

        float const rx=placement[0],ry=placement[1],rz=placement[2];
        float const fx=placement[3],fy=placement[4],fz=placement[5];
        float const ux=placement[6],uy=placement[7],uz=placement[8];
        float const cfx=fy*uz-fz*uy, cfy=fz*ux-fx*uz, cfz=fx*uy-fy*ux;
        float const determinant=rx*cfx+ry*cfy+rz*cfz;
        // Native lights use Euclidean world distances. For non-unit scaling
        // local-space evaluation would change attenuation; let JK handle it.
        if(!std::isfinite(determinant) ||
           std::fabs(std::fabs(determinant)-1.0f)>0.05f)
            return false;
        for(int axis=0;axis<3;axis++) {
            int const i=axis*3;
            float const len=placement[i]*placement[i]+
                            placement[i+1]*placement[i+1]+
                            placement[i+2]*placement[i+2];
            if(!std::isfinite(len) || std::fabs(len-1.0f)>0.05f)
                return false;
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
            float const reach=radius+std::max(0.0f,m.radius);
            if(dx*dx+dy*dy+dz*dz>reach*reach) continue;
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

    // Calling the native mesh renderer on the first frames is essential:
    // rdCache normally causes MAT cels to be resident and D3D surfaces to
    // receive texture handles. Never bypass it before that work has happened.
    bool gpu_mesh_materials_ready(jkgm::gpu_3do_geometry const &g)
    {
        using namespace jkgm::engine_material_debug;
        if(!matches_tested_executable()) return false;
        for(auto const &ref:g.material_refs) {
            native_material mat{};
            if(!read_object(ref.first,mat)) return false;
            if((mat.tex_type&2U)==0) continue; // pure solid color
            if(mat.num_texinfo==0 || mat.num_texinfo>16) return false;
            int32_t cel=ref.second<0
                ? static_cast<int32_t>(mat.current_cel) : ref.second;
            cel=std::max(0,std::min(cel,static_cast<int32_t>(mat.num_texinfo)-1));
            native_texinfo info{};
            if(!read_object(mat.texinfo[cel],info)) return false;
            if((info.header[0]&8U)==0) continue; // no texture for this cel
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
            if(!present) return false;
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
        auto const start = std::chrono::steady_clock::now();
        int result = RealNativeModel(thing, placement);
        native_totals.model_ns += elapsed_native_ns(start);
        ++native_totals.model_calls;
        if(result != 0) ++native_totals.model_drawn;
        return result;
    }
    void __cdecl HookedNativeMesh(void *mesh, void *placement)
    {
        auto const profiling=the_config->profile_renderer;
        auto const start=profiling ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
        if(!jkgm::queue_gpu_3do_mesh(mesh,placement)) {
            RealNativeMesh(mesh, placement);
        }
        else {
            // First side effect of rdModel3_DrawMesh, normally set at 0x446371.
            *reinterpret_cast<void **>(0x0073C1E8U)=mesh;
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
        if(error == NO_ERROR && profiling)
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
        if(the_config->profile_renderer)
            DetourDetach(&(PVOID &)RealNativeModel, HookedNativeModel);
        DetourTransactionCommit();
    }
} // namespace


bool jkgm::queue_gpu_3do_mesh(void const *mesh,void const *matrix)
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
    if(!the_config || !the_config->use_model_cache || !mesh || !matrix ||
       pending_gpu_instances.size()>=16384 ||
       // Single VirtualQuery per candidate, including the optional RGB
       // field; thousands of calls happen on busy scenes.
       !gpu_span_readable(mesh,has_rgb_mesh_extension?0x70U:sizeof(native_mesh)) ||
       !gpu_span_readable(matrix,sizeof(float)*12)) return false;
    native_mesh m{};
    std::memcpy(&m,mesh,sizeof(m));
    float const *vertex_rgb=nullptr;
    if(has_rgb_mesh_extension) {
        auto const *bytes=static_cast<uint8_t const *>(mesh);
        std::memcpy(&vertex_rgb,bytes+0x6cU,sizeof(vertex_rgb));
    }
    // Only filled geometry is eligible for caching. Preserve native handling
    // for points, wireframe, or faces with ambiguous polygon outlines.
    if(the_config->profile_renderer) ++gpu_native_stats.attempted;
    // Each JK text character is a distinct ephemeral 3DO, often one quad.
    // Uploading a new VAO/VBO/EBO for each glyph is costlier than native
    // rendering, and the persistent cache otherwise retains every glyph.
    if(m.name[0]=='c' && m.name[1]=='\0' &&
       m.num_faces==1 && m.num_vertices<=8) {
        if(the_config->profile_renderer) ++gpu_native_stats.text_native;
        return false;
    }
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
    if(!jkgm::gpu_3do_supports_geo_mode(current_geo_mode,m.geometry_mode)) {
        if(the_config->profile_renderer) ++gpu_native_stats.geometry_native;
        gpu_log_fallback(m,"geometry_mode_not_full");
        return false; // Preserve JK vertex/wire/solid/invisible rendering.
    }
    auto const camera=jkgm::read_active_render_camera();
    if(!camera.valid) {
        if(the_config->profile_renderer) ++gpu_native_stats.lighting_native;
        gpu_log_fallback(m,"camera_unavailable");
        return false;
    }
    if(gpu_mesh_outside_native_frustum(m,matrix,camera)) {
        if(the_config->profile_renderer) ++gpu_native_stats.culled;
        return true;
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
    if(!header_unchanged ||
       it->second.source_checked_frame!=gpu_frame_serial) {
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
            if(profiling) ++gpu_native_stats.snapshot_native;
            gpu_log_fallback(m,"invalid_mesh_source_memory");
            return false;
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
        }
    }
    if(!it->second.geometry) {
        if(the_config->profile_renderer) {
            ++gpu_native_stats.snapshot_native;
            if(!snapshot_attempted_this_call)
                ++gpu_native_stats.failed_snapshot_reuses;
        }
        gpu_log_fallback(m,it->second.snapshot_failure ?
                         it->second.snapshot_failure : "unknown_snapshot_error");
        return false;
    }
    // If a cel has never been encountered, let JK handle this call to warm
    // its native material cache. We can switch to GPU rendering next frame.
    // Many instances share this mesh. Check MAT handles once per mesh per
    // frame, refreshing next frame so animated cels still get a warm-up.
    if(it->second.ready_checked_frame!=gpu_frame_serial) {
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
    if(!it->second.ready_for_frame) {
        if(the_config->profile_renderer) ++gpu_native_stats.material_native;
        gpu_log_fallback(m,"native_MAT_cel_not_yet_resident");
        return false;
    }
    gpu_3do_instance inst;
    inst.id=it->second.id;
    inst.geometry=it->second.geometry;
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
    std::memcpy(inst.transform.data(),matrix,sizeof(float)*12);
    for(float f:inst.transform) if(!std::isfinite(f)) return false;
    if(!snapshot_gpu_3do_lighting(m,inst.transform.data(),inst)) {
        if(the_config->profile_renderer) ++gpu_native_stats.lighting_native;
        gpu_log_fallback(m,"unsupported_dynamic_lighting_or_light_count");
        return false;
    }
    pending_gpu_instances.emplace_back(std::move(inst));
    if(the_config->profile_renderer) ++gpu_native_stats.queued;
    return true;
}
jkgm::gpu_3do_native_stats jkgm::consume_gpu_3do_native_stats()
{
    auto const sample=gpu_native_stats;
    gpu_native_stats={};
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
    ++gpu_frame_serial;
    gpu_native_stats={};
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
    using cog_push_float_type = void(__cdecl *)(void *ctx, float value);
    using cog_push_int_type = void(__cdecl *)(void *ctx, int value);
    using cog_push_vector_type = void(__cdecl *)(void *ctx, float const *xyz);

    constexpr uintptr_t pop_float_address = 0x4E2780;
    constexpr uintptr_t pop_int_address = 0x4E2850;
    constexpr uintptr_t pop_vector_address = 0x4E2970;
    constexpr uintptr_t push_float_address = 0x4E3650;
    constexpr uintptr_t push_int_address = 0x4E35D0;
    constexpr uintptr_t push_vector_address = 0x4E36E0;

    cog_pop_float_type const cog_pop_float = reinterpret_cast<cog_pop_float_type>(pop_float_address);
    cog_pop_int_type const cog_pop_int = reinterpret_cast<cog_pop_int_type>(pop_int_address);
    cog_pop_vector_type const cog_pop_vector =
        reinterpret_cast<cog_pop_vector_type>(pop_vector_address);
    cog_push_float_type const cog_push_float =
        reinterpret_cast<cog_push_float_type>(push_float_address);
    cog_push_int_type const cog_push_int = reinterpret_cast<cog_push_int_type>(push_int_address);
    cog_push_vector_type const cog_push_vector =
        reinterpret_cast<cog_push_vector_type>(push_vector_address);

    struct cog_helper_signature {
        uintptr_t address;
        std::array<uint8_t, 8> prologue;
    };

    constexpr std::array<cog_helper_signature, 6> cog_helper_signatures = {{
        {pop_float_address, {0x8b, 0x4c, 0x24, 0x04, 0x83, 0xec, 0x20, 0x8b}},
        {pop_int_address, {0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x8b, 0x4d}},
        {pop_vector_address, {0x8b, 0x4c, 0x24, 0x04, 0x83, 0xec, 0x10, 0x8b}},
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

    // ResetVignette()    black edges at full strength (the original look), immediately
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
        cog_verb_entry{"setglobalenvmap", cog_set_global_env_map},
        cog_verb_entry{"resetglobalenvmap", cog_reset_global_env_map},
        cog_verb_entry{"setmatenvmap", cog_set_mat_env_map},
        cog_verb_entry{"setmatenvmapfrom", cog_set_mat_env_map_from},
        cog_verb_entry{"resetmatenvmap", cog_reset_mat_env_map},
        cog_verb_entry{"setmaterialalbedo", cog_set_material_albedo},
        cog_verb_entry{"resetmaterialalbedo", cog_reset_material_albedo},
        cog_verb_entry{"setmaterialcolor", cog_set_material_color},
        cog_verb_entry{"resetmaterialcolor", cog_reset_material_color},
    };

    JkInitNewCogVerbs_type TrueJkInitNewCogVerbs = nullptr;

    void __cdecl HookedJkInitNewCogVerbs(cog_register_verb_type register_verb, void *ctx)
    {
        TrueJkInitNewCogVerbs(register_verb, ctx);

        if(!cog_helpers_match()) {
            LOG_ERROR("jk.exe does not match the build the fog COG verbs were written for; "
                      "fog COG verbs not registered");
            return;
        }

        for(auto const &verb : fx_verbs) {
            register_verb(ctx, verb.handler, verb.name);
        }

        LOG_DEBUG("Registered fog, sunlight, post-processing, and material COG verbs");
    }
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

    LONG error = DetourTransactionCommit();
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

    // Detach injected functions
    if(native_mat_mips_hook_active) {
        DetourDetach(&(PVOID &)RealNativeSetMipDistances,HookedNativeSetMipDistances);
        native_mat_mips_hook_active=false;
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
        if(the_config->profile_renderer || the_config->use_model_cache) {
            native_3do_hooks_active = attach_native_3do_hooks(the_config->profile_renderer);
        }
        LOG_DEBUG("Finished attaching renderer to process");
    }
    else if(ul_reason_for_call == DLL_PROCESS_DETACH && renderer_hooks_active) {
        if(native_3do_hooks_active) {
            detach_native_3do_hooks();
            native_3do_hooks_active = false;
        }
        detach_hooks();
        renderer_hooks_active = false;
    }

    return TRUE;
}
