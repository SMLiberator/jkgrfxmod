#include "native_spawn_parent_hook.hpp"
#include "native_spawn_parent.hpp"
#include "base/log.hpp"

#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
using namespace jkgm::native_spawn_parent;

// Verified with the supplied jkgm(5).exe SHA-256
// afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f
constexpr std::uintptr_t native_create=0x004CDCB0U;
constexpr std::uintptr_t native_get_parent=0x004CC970U;

enum class parent_mode { source_ebx_if_null, owner_of_esi, owner_of_ebx };
struct spawn_site {
    std::uintptr_t call;
    parent_mode mode;
    char const *reason;
};
// All five instructions are direct CALL rel32 -> native_create.
// The caller's saved source register was independently verified at each call.
// Do not intercept normal weapon explosions or fleshhit: they already supply
// sithThing_GetThingParent(projectile) and preserve native damage behavior.
constexpr std::array<spawn_site,5> sites{{
    {0x004CDF7FU,parent_mode::source_ebx_if_null,"creatething"},
    {0x004FBE46U,parent_mode::owner_of_esi,"explosion debris"},
    {0x004ED3B6U,parent_mode::owner_of_esi,"actor explode"},
    {0x004D3B47U,parent_mode::owner_of_esi,"hitscan explode (primary)"},
    {0x004D41CFU,parent_mode::owner_of_ebx,"hitscan explode (secondary)"}
}};
bool installed=false;
std::array<std::array<std::uint8_t,5>,sites.size()> originals{};

using get_parent_fn=void* (__cdecl *)(void*);
get_parent_fn const get_native_parent=
    reinterpret_cast<get_parent_fn>(native_get_parent);

bool code_matches(std::uintptr_t addr,std::uint8_t const *signature,
                  std::size_t length) {
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery(reinterpret_cast<void const*>(addr),&mbi,sizeof(mbi)) ||
       mbi.State!=MEM_COMMIT || (mbi.Protect&PAGE_GUARD) ||
       (mbi.Protect&PAGE_NOACCESS))return false;
    auto const base=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    if(addr<base || length>mbi.RegionSize ||
       addr-base>mbi.RegionSize-length)return false;
    return std::memcmp(reinterpret_cast<void const*>(addr),
                       signature,length)==0;
}
template<std::size_t N>
bool code_matches(std::uintptr_t addr,std::array<std::uint8_t,N> const&sig) {
    return code_matches(addr,sig.data(),sig.size());
}
bool replace_call(std::uintptr_t addr,std::array<std::uint8_t,5> const&code) {
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(addr),code.size(),
                       PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(addr),code.data(),code.size());
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<void const*>(addr),code.size());
    DWORD ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(addr),code.size(),
                       old,&ignored))
        LOG_WARNING("Native template parent hook: could not restore page protection");
    return true;
}

// This calls JK's existing GetThingParent; on a root Thing it returns
// the Thing itself. The returned pointer is assigned before the synchronous
// 'created:' message, even if the source is subsequently destroyed.
extern "C" void* __cdecl resolve_spawn_root(void* source) {
    return root_or_source(source,[](void* thing) {
        return get_native_parent(thing);
    });
}

// All thunks are entered by a CALL instruction with the normal 5 cdecl
// sithThing_CreateThingAtPos arguments already on the caller's stack:
// [esp+4] template, +8 pos, +12 orient, +16 sector, +20 parent.
// Tail-jumping into the ORIGINAL native function preserves the CALL's return
// address and original caller stack cleanup. No per-frame hooks are involved.
extern "C" __declspec(naked) void __cdecl hook_creatething_parent() {
    __asm {
        cmp dword ptr [esp+14h],0
        jne existing_parent
        // In the recursive native spawn, EBX is the *new source Thing*.
        mov dword ptr [esp+14h],ebx
existing_parent:
        mov eax,04CDCB0h
        jmp eax
    }
}

extern "C" __declspec(naked) void __cdecl hook_spawn_owner_esi() {
    __asm {
        pushad
        // In debris, actor death, and first hitscan path ESI holds the source.
        push esi
        call resolve_spawn_root
        add esp,4
        // pushad preserved the original stack, adding 0x20 bytes.
        mov dword ptr [esp+34h],eax
        popad
        mov eax,04CDCB0h
        jmp eax
    }
}

extern "C" __declspec(naked) void __cdecl hook_spawn_owner_ebx() {
    __asm {
        pushad
        // In the other hitscan path EBX holds the source weapon.
        push ebx
        call resolve_spawn_root
        add esp,4
        mov dword ptr [esp+34h],eax
        popad
        mov eax,04CDCB0h
        jmp eax
    }
}

std::uintptr_t hook_for(parent_mode mode) {
    switch(mode) {
        case parent_mode::source_ebx_if_null:
            return reinterpret_cast<std::uintptr_t>(&hook_creatething_parent);
        case parent_mode::owner_of_esi:
            return reinterpret_cast<std::uintptr_t>(&hook_spawn_owner_esi);
        case parent_mode::owner_of_ebx:
            return reinterpret_cast<std::uintptr_t>(&hook_spawn_owner_ebx);
    }
    return 0;
}
bool signatures_match() {
    // Matches exactly this build's native create and root-parent functions.
    constexpr std::array<std::uint8_t,16> create_bytes{{
        0x8b,0x44,0x24,0x04,0x8b,0x15,0x38,0x31,
        0x83,0x00,0x8b,0x0d,0x10,0x3a,0x83,0x00
    }};
    constexpr std::array<std::uint8_t,11> root_bytes{{
        0x8b,0x44,0x24,0x04,0x56,0x8b,0x88,
        0x80,0x00,0x00,0x00
    }};
    if(reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr))!=0x00400000U ||
       !code_matches(native_create,create_bytes) ||
       !code_matches(native_get_parent,root_bytes))return false;

    // Guard source-register assumptions as well as the actual CALL.
    constexpr std::array<std::uint8_t,5> recursive_args{{0x56,0x57,0x52,0x51,0x50}};
    constexpr std::array<std::uint8_t,2> zero_parent{{0x6a,0x00}};
    if(!code_matches(0x004CDF7AU,recursive_args) ||
       !code_matches(0x004FBE3AU,zero_parent) ||
       !code_matches(0x004ED3AAU,zero_parent) ||
       !code_matches(0x004D3B31U,zero_parent) ||
       !code_matches(0x004D41B0U,zero_parent))return false;
    for(auto const& site:sites) {
        std::array<std::uint8_t,5> expected{};
        if(!make_call(site.call,native_create,expected) ||
           !code_matches(site.call,expected))
            return false;
    }
    return true;
}
}

namespace jkgm::native_spawn_parent_hook {
bool install() {
    if(installed)return true;
    if(!signatures_match()) {
        LOG_WARNING("Native template-parent inheritance skipped: unknown jkgm.exe");
        return false;
    }
    std::size_t applied=0;
    for(auto const &site:sites) {
        std::array<std::uint8_t,5> code{};
        if(!make_call(site.call,hook_for(site.mode),code) ||
           !make_call(site.call,native_create,originals[applied]) ||
           !replace_call(site.call,code)) {
            LOG_WARNING("Native template parent hook failed at ",site.reason,
                        "; reverting modified callsites");
            while(applied) {
                --applied;
                replace_call(sites[applied].call,originals[applied]);
            }
            return false;
        }
        ++applied;
    }
    installed=true;
    LOG_INFO("Native template parent inheritance enabled for creatething, debris, "
             "actor explode and two hitscan explode callsites; "
             "weapon explode/fleshhit unchanged");
    return true;
}
void uninstall() {
    if(!installed)return;
    for(std::size_t i=0;i<sites.size();++i) {
        if(!replace_call(sites[i].call,originals[i]))
            LOG_WARNING("Failed to restore native parent callsite: ",sites[i].reason);
    }
    installed=false;
}
}
