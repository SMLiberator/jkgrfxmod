#pragma once
#include "native_cog_vm_fast.hpp"
#include "base/log.hpp"
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace jkgm::cog_vm_fast {
template<class T>
inline T& field(void* p,size_t off) noexcept {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+off);
}
inline value const* resolve(void* ctx,uint32_t id) noexcept {
    void* table=nullptr;
    if(id>=256U) {
        table=*reinterpret_cast<void**>(0x008b5368U);
        id-=256U;
    } else table=field<void*>(ctx,0x94);
    if(!table || id>=field<uint32_t>(table,8))return nullptr;
    auto* records=field<unsigned char*>(table,0);
    if(!records)return nullptr;
    return reinterpret_cast<value const*>(records+size_t(id)*28U+4U);
}
using native_binary_fn=void(__cdecl *)(void*,uint32_t);
inline void __cdecl fast_integer(void* ctx,uint32_t op) {
    auto* stack=&field<value>(ctx,0x98);
    auto& depth=field<uint32_t>(ctx,0x498);
    if(!binary(stack,depth,op,true,[&](uint32_t id){return resolve(ctx,id);}))
        reinterpret_cast<native_binary_fn>(0x004e38c0U)(ctx,op);
}
inline void __cdecl fast_numeric(void* ctx,uint32_t op) {
    auto* stack=&field<value>(ctx,0x98);
    auto& depth=field<uint32_t>(ctx,0x498);
    if(!binary(stack,depth,op,false,[&](uint32_t id){return resolve(ctx,id);}))
        reinterpret_cast<native_binary_fn>(0x004e3b00U)(ctx,op);
}
template<size_t N>
struct patch {
    uintptr_t address;
    std::array<unsigned char,N> original, replacement{};
    bool active=false;
    bool matches(std::array<unsigned char,N> const& bytes) const {
        MEMORY_BASIC_INFORMATION mbi{};
        auto* p=reinterpret_cast<void const*>(address);
        return VirtualQuery(p,&mbi,sizeof(mbi)) && mbi.State==MEM_COMMIT &&
               !(mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
               reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize-address>=N &&
               std::memcmp(p,bytes.data(),N)==0;
    }
    bool write(std::array<unsigned char,N> const& bytes) {
        auto* p=reinterpret_cast<void*>(address);
        DWORD old=0,ignored=0;
        if(!VirtualProtect(p,N,PAGE_EXECUTE_READWRITE,&old))return false;
        std::memcpy(p,bytes.data(),N);
        FlushInstructionCache(GetCurrentProcess(),p,N);
        if(!VirtualProtect(p,N,old,&ignored))
            LOG_WARNING("COG VM patch page protection restore failed");
        return true;
    }
    bool install() { active=matches(original)&&write(replacement);return active; }
    void uninstall() {
        if(!active)return;
        if(!matches(replacement)||!write(original))
            LOG_WARNING("COG VM patch could not be restored");
        else active=false;
    }
};
inline patch<5> integer_call{0x004e2553U,{0xe8,0x68,0x13,0x00,0x00}};
inline patch<5> numeric_call{0x004e255cU,{0xe8,0x9f,0x15,0x00,0x00}};
inline patch<15> dispatch{0x004e2236U,
    {0x33,0xc0,0x8a,0x87,0xb4,0x25,0x4e,0x00,0xff,0x24,0x85,0x7c,0x25,0x4e,0x00}};
inline constexpr std::array<uint32_t,14> native_targets={
    0x4e2564U,0x4e23a1U,0x4e2432U,0x4e2323U,0x4e2245U,0x4e23f7U,
    0x4e2500U,0x4e2551U,0x4e249eU,0x4e247aU,0x4e24c2U,0x4e22e3U,
    0x4e24d3U,0x4e255aU};
inline constexpr std::array<unsigned char,32> native_indices={
    0,1,1,1,2,3,4,5,13,13,13,13,13,6,6,13,
    13,13,13,13,7,7,7,7,7,7,8,9,10,11,13,12};
inline std::array<uint32_t,32> direct_targets{};
inline void prepare_call(patch<5>& p,native_binary_fn fn) {
    p.replacement[0]=0xe8;
    uint32_t const rel=uint32_t(reinterpret_cast<uintptr_t>(fn)-(p.address+5U));
    std::memcpy(p.replacement.data()+1,&rel,4);
}
inline void install(bool scalar,bool direct) {
    if(!scalar&&!direct)return;
    if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=0x00400000U) {
        LOG_WARNING("COG VM acceleration skipped: unsupported executable base");
        return;
    }
    if(scalar) {
        // Verify all three ABI-dependent helper entry points, in addition
        // to the exact callsites. No patch at the profiler's VM entry or
        // its CALLFUNC bridge.
        patch<8> const ih{0x004e38c0U,{0x55,0x8b,0xec,0x83,0xec,0x10,0x53,0x56}};
        patch<8> const nh{0x004e3b00U,{0x83,0xec,0x24,0x53,0x56,0x57,0x8b,0x7c}};
        patch<9> const sh{0x004fd660U,{0x8b,0x44,0x24,0x08,0x3d,0x00,0x01,0x00,0x00}};
        if(ih.matches(ih.original)&&nh.matches(nh.original)&&sh.matches(sh.original)&&
           integer_call.matches(integer_call.original)&&numeric_call.matches(numeric_call.original)) {
            prepare_call(integer_call,fast_integer);
            prepare_call(numeric_call,fast_numeric);
            if(!integer_call.install()||!numeric_call.install()) {
                integer_call.uninstall();numeric_call.uninstall();
            }
        }
        LOG_INFO("COG VM scalar fast path enabled=",integer_call.active&&numeric_call.active,
                 " NOTE=live_symbols;native_x87;native_fallback");
    }
    if(direct) {
        // Flatten the verified two-stage switch. Keep the preceding
        // unsigned opcode range check and all original branch targets.
        if(dispatch.matches(dispatch.original) &&
           std::memcmp(reinterpret_cast<void const*>(0x004e257cU),
                       native_targets.data(),sizeof(native_targets))==0 &&
           std::memcmp(reinterpret_cast<void const*>(0x004e25b4U),
                       native_indices.data(),sizeof(native_indices))==0) {
            for(size_t i=0;i<direct_targets.size();++i)
                direct_targets[i]=native_targets[native_indices[i]];
            dispatch.replacement.fill(0x90);
            // XOR EAX,EAX is retained (original switch flags). Preserve
            // ECX (script pointer), EDX and all nonvolatile registers.
            dispatch.replacement[0]=0x33;dispatch.replacement[1]=0xc0;
            dispatch.replacement[2]=0xff;dispatch.replacement[3]=0x24;
            dispatch.replacement[4]=0xbd; // JMP [EDI*4+disp32]
            uint32_t const addr=uint32_t(reinterpret_cast<uintptr_t>(direct_targets.data()));
            std::memcpy(dispatch.replacement.data()+5,&addr,4);
            dispatch.install();
        }
        LOG_INFO("COG VM direct opcode dispatch enabled=",dispatch.active);
    }
}
inline void uninstall() {
    dispatch.uninstall();numeric_call.uninstall();integer_call.uninstall();
}
}
