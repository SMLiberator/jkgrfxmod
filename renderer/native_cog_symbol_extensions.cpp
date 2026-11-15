#include "native_cog_symbol_extensions.hpp"
#include "cog_symbol_extensions.hpp"
#include "base/log.hpp"
#include <Windows.h>
#include <detours/detours.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
namespace cogex=jkgm::cog_symbol_extensions;
constexpr uintptr_t line_call=0x004fcf50U;
constexpr uintptr_t parser_ref=0x004fdd70U;
constexpr uintptr_t resolver_addr=0x004df6a0U;
constexpr uintptr_t read_args_addr=0x00431880U;
constexpr uintptr_t source_args=0x008607e0U;
constexpr uintptr_t last_loaded_world=0x00833a18U;
constexpr uintptr_t current_world=0x00833a10U;
constexpr uintptr_t static_world=0x00833a14U;
constexpr uintptr_t sprite_loader=0x004f2620U;
constexpr uintptr_t soundclass_loader=0x004e6970U;
constexpr uintptr_t puppet_loader=0x004e5330U;

// Native SithCogSymbol is 28 bytes; values are 32-bit type + 3 x 32-bit.
constexpr size_t native_symbol_stride=0x1cU;
constexpr size_t native_ref_stride=0x38U;
constexpr size_t native_refs_begin=0x1b4U;
constexpr size_t native_refs_count=0x1db4U;

using parse_ref_fn=int (__cdecl*)(void*,int,int);
using resolve_fn=int (__cdecl*)(void*,void*,char*);
using resource_loader_fn=void* (__cdecl*)(char*);
parse_ref_fn const native_parse_ref=reinterpret_cast<parse_ref_fn>(parser_ref);
resolve_fn RealCogResolver=reinterpret_cast<resolve_fn>(resolver_addr);
bool installed=false,callsite_patched=false;
std::array<uint8_t,5> original_call{};

bool check_bytes(uintptr_t address,std::initializer_list<uint8_t> signature) {
    MEMORY_BASIC_INFORMATION mbi{};
    if(!VirtualQuery(reinterpret_cast<void const*>(address),&mbi,sizeof(mbi)) ||
       mbi.State!=MEM_COMMIT ||
       (mbi.Protect&PAGE_GUARD)!=0 ||
       mbi.Protect==PAGE_NOACCESS)return false;
    if(reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize <
       address+signature.size())return false;
    return std::memcmp(reinterpret_cast<void const*>(address),
                       signature.begin(),signature.size())==0;
}
char const *arg(unsigned index,bool value=false) {
    auto const count=*reinterpret_cast<uint32_t const*>(source_args);
    if(index>=count || index>=64U)return nullptr;
    auto const address=source_args+4U+uintptr_t(index)*8U+(value?4U:0U);
    return *reinterpret_cast<char const* const*>(address);
}
uint32_t word(uintptr_t p) {
    uint32_t value=0;
    if(p)std::memcpy(&value,reinterpret_cast<void const*>(p),sizeof(value));
    return value;
}
void write_word(uintptr_t p,uint32_t v) {
    std::memcpy(reinterpret_cast<void*>(p),&v,sizeof(v));
}
// Returns the last declared COG symbol; the native parser just appended it.
uintptr_t last_symbol(uintptr_t script) {
    if(!script)return 0;
    uintptr_t const tab=word(script+0x2cU);
    if(!tab)return 0;
    uint32_t const count=word(tab+8U);
    uintptr_t const data=word(tab);
    if(!count || count>1024U || !data)return 0;
    return data+uintptr_t(count-1U)*native_symbol_stride;
}
void fix_vector_declaration(uintptr_t script) {
    auto const *value=arg(1,true);
    if(!value || !*value)return;
    std::array<float,3> xyz{};
    if(!cogex::parse_initial_vector(value,xyz)) {
        LOG_WARNING("COG vector initializer rejected: ",value);
        return;
    }
    uintptr_t const symbol=last_symbol(script);
    if(!symbol || word(symbol+4U)!=5U)return;
    std::memcpy(reinterpret_cast<void*>(symbol+8U),
                xyz.data(),sizeof(float)*3U);

    // The native ParseVector() deliberately ignores the default assignment.
    // For nonlocal symbols, save it on the native symbol-ref so JKL args
    // still take precedence during sithCog_Open(). Local declarations have
    // no symbol-ref and retain the directly initialized symbol value.
    uint32_t const count=word(script+native_refs_count);
    if(!count || count>128U)return;
    uintptr_t const ref=script+native_refs_begin+
                        uintptr_t(count-1U)*native_ref_stride;
    if(word(ref)!=11U)return;
    if(word(ref+0x10U)!=word(symbol))return; // native symbol ID
    size_t const len=std::strlen(value);
    if(len>=32U) {
        LOG_WARNING("COG vector declaration exceeds 31-char default limit");
        return;
    }
    std::memset(reinterpret_cast<void*>(ref+0x18U),0,32U);
    std::memcpy(reinterpret_cast<void*>(ref+0x18U),value,len);
}
extern "C" void __cdecl process_extra_cog_symbols(void *script,int descriptions) {
    auto const *kind=arg(0,true);
    if(!kind || !script)return;
    if(std::strcmp(kind,"vector")==0) {
        fix_vector_declaration(reinterpret_cast<uintptr_t>(script));
        return;
    }
    int const type=cogex::resource_ref_type(kind);
    if(type) {
        if(native_parse_ref(script,type,descriptions)==0)
            LOG_WARNING("COG resource symbol could not be declared: ",kind);
        else {
            uintptr_t const sym=last_symbol(reinterpret_cast<uintptr_t>(script));
            if(sym && word(sym+4U)==3U)write_word(sym+8U,uint32_t(-1));
        }
    }
}
// The single native sithCogParse_Load callsite at 0x4FCF50 executes
// after processing each symbols-section line (including unknown types).
// EBP = SithCogScript*, EBX = bParseDescription. No COG opcode parsing
// is affected. Preserve all native registers and return ReadArgs' EAX.
extern "C" __declspec(naked) int __cdecl HookedCogReadSymbolLine() {
    __asm {
        pushad
        push ebx
        push ebp
        call process_extra_cog_symbols
        add esp,8
        popad
        mov eax,0431880h
        call eax
        ret
    }
}
int find_loaded_index(uintptr_t pointer,int kind) {
    if(!pointer)return -1;
    size_t count_off=0,ptr_off=0,stride=0;
    if(kind==cogex::sprite_ref) {
        count_off=0x70U;ptr_off=0x78U;stride=0xacU;
    } else if(kind==cogex::soundclass_ref) {
        count_off=0x138U;ptr_off=0x140U;stride=0x1a0U;
    } else if(kind==cogex::puppet_ref) {
        count_off=0x174U;ptr_off=0x17cU;stride=0x1068U;
    } else return -1;
    uintptr_t const current=word(current_world);
    uintptr_t const stat=word(static_world);
    uintptr_t const last=word(last_loaded_world);
    // Search the actual owner: loaders may reuse already loaded static
    // resources even when a level world is being initialized.
    for(uintptr_t w:{current,stat,last}) {
        if(!w)continue;
        uint32_t const count=word(w+count_off);
        uintptr_t const first=word(w+ptr_off);
        int const index=cogex::resource_index(pointer,first,count,stride);
        if(index>=0)
            return index | (w==stat ? 0x8000 : 0);
    }
    return -1;
}
int __cdecl HookedCogResolver(void *symbol,void *ref,char *filename) {
    if(!symbol || !ref)return RealCogResolver(symbol,ref,filename);
    int const kind=static_cast<int>(word(reinterpret_cast<uintptr_t>(ref)));
    if(kind!=cogex::sprite_ref && kind!=cogex::soundclass_ref &&
       kind!=cogex::puppet_ref)
        return RealCogResolver(symbol,ref,filename);
    uintptr_t const addr=reinterpret_cast<uintptr_t>(symbol);
    write_word(addr+4U,3U); // ordinary integer reference
    int id=-1;
    if(filename && *filename) {
        void *resource=nullptr;
        if(kind==cogex::sprite_ref)
            resource=reinterpret_cast<resource_loader_fn>(sprite_loader)(filename);
        else if(kind==cogex::soundclass_ref)
            resource=reinterpret_cast<resource_loader_fn>(soundclass_loader)(filename);
        else
            resource=reinterpret_cast<resource_loader_fn>(puppet_loader)(filename);
        id=find_loaded_index(reinterpret_cast<uintptr_t>(resource),kind);
    }
    write_word(addr+8U,static_cast<uint32_t>(id));
    if(id<0 && filename && *filename)
        LOG_WARNING("COG resource failed: type=",kind," name=",filename);
    return id>=0?1:0;
}
bool write_call(uintptr_t target) {
    constexpr uintptr_t resume=line_call+5U;
    intptr_t const displacement=
        reinterpret_cast<intptr_t>(&HookedCogReadSymbolLine)-
        static_cast<intptr_t>(resume);
    if(displacement<std::numeric_limits<int32_t>::min() ||
       displacement>std::numeric_limits<int32_t>::max())return false;
    uint8_t data[5]={0xe8,0,0,0,0};
    int32_t const disp=static_cast<int32_t>(displacement);
    std::memcpy(data+1,&disp,4);
    DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(target),5U,
                       PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(target),data,5U);
    DWORD ignored=0;
    VirtualProtect(reinterpret_cast<void*>(target),5U,old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<void const*>(target),5U);
    return true;
}
}

namespace jkgm::native_cog_symbol_extensions {
bool request() {
    if(installed)return true;
    if(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=0x00400000U ||
       !check_bytes(line_call,{0xe8,0x2b,0x49,0xf3,0xff}) ||
       !check_bytes(parser_ref,{0x83,0xec,0x14,0x53,0x55,0x56}) ||
       !check_bytes(resolver_addr,{0x8b,0x44,0x24,0x08,0x56}) ||
       !check_bytes(0x004fe2d0U,{0x83,0xec,0x18,0x53,0x55}) ||
       !check_bytes(sprite_loader,{0xa1,0xd8,0x2f,0x85,0x00}) ||
       !check_bytes(soundclass_loader,{0x81,0xec,0x80,0,0,0}) ||
       !check_bytes(puppet_loader,{0xa1,0xd0,0x7e,0x84,0x00})) {
        LOG_WARNING("COG symbol extensions disabled: unsupported JKGM parser or loaders");
        return false;
    }
    std::memcpy(original_call.data(),
                reinterpret_cast<void const*>(line_call),5U);
    LONG const err=DetourAttach(&(PVOID&)RealCogResolver,HookedCogResolver);
    if(err!=NO_ERROR) {
        LOG_WARNING("COG resource resolver detour failed: ",err);
        return false;
    }
    if(!write_call(line_call)) {
        DetourDetach(&(PVOID&)RealCogResolver,HookedCogResolver);
        LOG_WARNING("COG symbol parser callsite patch failed");
        return false;
    }
    callsite_patched=installed=true;
    LOG_INFO("COG symbol extensions: initialized vectors, sprite, soundclass, puppet");
    return true;
}
void detach() {
    if(callsite_patched) {
        DWORD old=0;
        if(VirtualProtect(reinterpret_cast<void*>(line_call),5U,
                          PAGE_EXECUTE_READWRITE,&old)) {
            std::memcpy(reinterpret_cast<void*>(line_call),
                        original_call.data(),5U);
            DWORD ignored=0;
            VirtualProtect(reinterpret_cast<void*>(line_call),5U,old,&ignored);
            FlushInstructionCache(GetCurrentProcess(),
                                  reinterpret_cast<void const*>(line_call),5U);
        }
        callsite_patched=false;
    }
    if(installed) {
        DetourDetach(&(PVOID&)RealCogResolver,HookedCogResolver);
        installed=false;
    }
}
void on_failed_commit() {
    // A failed Detours commit has already rolled back the resolver hook.
    // The direct CALL patch is ours and must be undone independently.
    if(callsite_patched) {
        DWORD old=0;
        if(VirtualProtect(reinterpret_cast<void*>(line_call),5U,
                          PAGE_EXECUTE_READWRITE,&old)) {
            std::memcpy(reinterpret_cast<void*>(line_call),
                        original_call.data(),5U);
            DWORD ignored=0;
            VirtualProtect(reinterpret_cast<void*>(line_call),5U,old,&ignored);
            FlushInstructionCache(GetCurrentProcess(),
                                  reinterpret_cast<void const*>(line_call),5U);
        }
    }
    installed=callsite_patched=false;
}
}
