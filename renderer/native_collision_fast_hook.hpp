#pragma once
// x86 bindings for the verified JKGM native intersection code. Independent
// of simulation profiling and of the type-2/type-3 extension at 0x508360.
#include "native_collision_fast.hpp"
#include "base/log.hpp"
#include <windows.h>
#include <detours/detours.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace jkgm::collision_fast {
using sphere_fn=int(__cdecl *)(float const*,float const*,float,float,
                              float const*,float,float*,int,int);
using plane_fn=int(__cdecl *)(float const*,float const*,float,float,
                             float const*,float const*,float*,int);
using face_fn=int(__cdecl *)(float const*,float const*,float,float,
                            void*,float const*,float*,float*,int);
using mesh_fn=int(__cdecl *)(float const*,float const*,float,float,
                            void*,float*,void**,float*);
inline sphere_fn real_sphere=reinterpret_cast<sphere_fn>(0x005087D0U);
inline plane_fn real_plane=reinterpret_cast<plane_fn>(0x00508E70U);
inline mesh_fn real_mesh=reinterpret_cast<mesh_fn>(0x00508690U);
inline face_fn const native_face=reinterpret_cast<face_fn>(0x00508FB0U);
inline bool sphere_active=false,plane_active=false,mesh_active=false;
inline thread_local bool mesh_prefiltered=false;

inline int __cdecl fast_sphere(float const *start,float const *dir,float distance,
                               float radius,float const *center,float size,
                               float *hit,int check_initial,int flags) {
    if(sphere_miss(query(start,dir,distance,radius),center,size,check_initial!=0))
        return 0;
    return real_sphere(start,dir,distance,radius,center,size,hit,check_initial,flags);
}
inline int __cdecl fast_plane(float const *start,float const *dir,float distance,
                              float radius,float const *normal,float const *point,
                              float *hit,int flags) {
    if(!mesh_prefiltered &&
       plane_miss(query(start,dir,distance,radius),normal,point))return 0;
    return real_plane(start,dir,distance,radius,normal,point,hit,flags);
}
template<class T> inline T field(void const *p,size_t offset) noexcept {
    T value;
    std::memcpy(&value,static_cast<unsigned char const*>(p)+offset,sizeof(value));
    return value;
}

// Native mesh acceptance compares the x87 dot product before rounding it
// to a float. Reproduce the same y*z-sum order, status-bit comparison and
// float store rather than changing nearly coincident face tie behavior.
inline bool normal_score(float const *direction,float const *normal,
                         float best,float &rounded) noexcept {
    unsigned short status;
    float score;
    __asm {
        mov ecx,direction
        mov edx,normal
        fld dword ptr [ecx+4]
        fmul dword ptr [edx+4]
        fld dword ptr [ecx+8]
        fmul dword ptr [edx+8]
        faddp st(1),st(0)
        fld dword ptr [ecx]
        fmul dword ptr [edx]
        faddp st(1),st(0)
        fst dword ptr [score]
        fcomp dword ptr [best]
        fnstsw ax
        mov status,ax
    }
    rounded=score;
    return (status&0x0100U)!=0;
}
struct prefiltered_guard {
    bool previous;
    prefiltered_guard() noexcept :previous(mesh_prefiltered) {mesh_prefiltered=true;}
    ~prefiltered_guard() {mesh_prefiltered=previous;}
};

// Whole native mesh loop, using live geometry instead of a stale-prone
// snapshot. Verified offsets: rdMesh vertices +0x30, faces +0x40,
// vertex count +0x48, face count +0x50; rdFace stride 0x40,
// first vertex-index pointer +0x18, normal +0x34.
// Preserve source order, exact native per-face function and its output
// writes even on nonaccepted hits, shrinking distance and native tie rules.
// No callbacks occur in this loop; outer Thing collision traversal remains
// unchanged (including callbacks which may mutate geometry between queries).
inline int __cdecl fast_mesh(float const *start,float const *dir,float distance,
                             float radius,void *mesh,float *hit_distance,
                             void **hit_face,float *hit_normal) {
    query q(start,dir,distance,radius);
    if(!q.valid || !mesh || !hit_distance || !hit_face || !hit_normal)
        return real_mesh(start,dir,distance,radius,mesh,hit_distance,hit_face,hit_normal);
    auto const faces=field<unsigned char*>(mesh,0x40);
    auto const vertices=field<float const*>(mesh,0x30);
    auto const vertex_count=field<uint32_t>(mesh,0x48);
    auto const count=field<uint32_t>(mesh,0x50);
    // Tiny meshes keep the original loop; unusually large or invalid
    // geometry is not eligible for the replacement.
    if(count<8U || count>1048576U || !faces || !vertices ||
       !vertex_count || vertex_count>1048576U)
        return real_mesh(start,dir,distance,radius,mesh,hit_distance,hit_face,hit_normal);
    prefiltered_guard guard;
    float best=1.0f;
    int accepted=0;
    for(uint32_t i=0;i<count;++i) {
        auto *face=faces+size_t(i)*0x40U;
        auto const normal=reinterpret_cast<float const*>(face+0x34);
        auto const indices=field<int const*>(face,0x18);
        if(indices) {
            int const first=indices[0];
            if(first>=0 && uint32_t(first)<vertex_count &&
               plane_miss(q,normal,vertices+size_t(first)*3U))continue;
        }
        float candidate_normal[3];
        int const result=native_face(start,dir,distance,radius,face,vertices,
                                     hit_distance,candidate_normal,0);
        if(!result)continue;
        float score=0.0f;
        bool const better=normal_score(dir,normal,best,score);
        // Native x87 tests C0, which is also set for unordered values.
        if(!(*hit_distance>=distance) || (accepted!=4 && result==4) || better) {
            accepted=result;
            best=score;
            *hit_face=face;
            std::memcpy(hit_normal,candidate_normal,sizeof(candidate_normal));
            distance=*hit_distance;
            // Native updates the distance even in its unusual tie cases.
            // Refresh only this bound; the start/direction are unchanged.
            q.distance=distance;
            q.valid=valid_scalar(distance);
        }
    }
    return accepted;
}

inline bool matches(uintptr_t address,std::initializer_list<unsigned char> sig) {
    auto const p=reinterpret_cast<void const*>(address);
    MEMORY_BASIC_INFORMATION m{};
    if(!VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
       (m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    auto const end=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
    return end>=address && end-address>=sig.size() &&
           std::memcmp(p,sig.begin(),sig.size())==0;
}
inline bool install(bool sphere,bool plane,bool mesh) {
    if(!sphere && !plane && !mesh)return true;
    if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=0x00400000U ||
       (sphere && !matches(0x005087D0U,{0x83,0xec,0x0c,0xd9,0x44,0x24,0x18,0xd8,0x1d,0x84,0x32,0x52,0x00})) ||
       ((plane || mesh) && !matches(0x00508E70U,{0x8b,0x44,0x24,0x04,0x8b,0x4c,0x24,0x18,0xd9,0x40,0x08})) ||
       (mesh && (!matches(0x00508690U,{0x83,0xec,0x18,0x53,0x57,0x8b,0x7c,0x24,0x34,0x33,0xdb}) ||
                 !matches(0x00508FB0U,{0x83,0xec,0x18,0x8b,0x44,0x24,0x3c,0x8b,0x4c,0x24,0x34})))) {
        LOG_WARNING("Collision acceleration skipped: JKGM intersection signatures differ");
        return false;
    }
    LONG err=DetourTransactionBegin();
    if(err!=NO_ERROR)return false;
    err=DetourUpdateThread(GetCurrentThread());
    if(err==NO_ERROR && sphere)err=DetourAttach(&(PVOID&)real_sphere,fast_sphere);
    if(err==NO_ERROR && plane)err=DetourAttach(&(PVOID&)real_plane,fast_plane);
    if(err==NO_ERROR && mesh)err=DetourAttach(&(PVOID&)real_mesh,fast_mesh);
    if(err!=NO_ERROR) {
        DetourTransactionAbort();
        LOG_WARNING("Collision acceleration attach failed: ",err);
        return false;
    }
    err=DetourTransactionCommit();
    if(err!=NO_ERROR) {
        LOG_WARNING("Collision acceleration commit failed: ",err);
        return false;
    }
    sphere_active=sphere; plane_active=plane; mesh_active=mesh;
    LOG_INFO("Native collision acceleration sphere=",sphere," plane=",plane,
             " ordered_mesh=",mesh," NOTE=native_narrowphase_and_callbacks_preserved");
    return true;
}
inline void uninstall() {
    if(!sphere_active && !plane_active && !mesh_active)return;
    LONG err=DetourTransactionBegin();
    if(err!=NO_ERROR)return;
    err=DetourUpdateThread(GetCurrentThread());
    if(err==NO_ERROR && mesh_active)err=DetourDetach(&(PVOID&)real_mesh,fast_mesh);
    if(err==NO_ERROR && plane_active)err=DetourDetach(&(PVOID&)real_plane,fast_plane);
    if(err==NO_ERROR && sphere_active)err=DetourDetach(&(PVOID&)real_sphere,fast_sphere);
    if(err!=NO_ERROR) {
        DetourTransactionAbort();
        LOG_WARNING("Collision acceleration detach failed: ",err);
        return;
    }
    err=DetourTransactionCommit();
    if(err==NO_ERROR)sphere_active=plane_active=mesh_active=false;
    else LOG_WARNING("Collision acceleration detach commit failed: ",err);
}
}
