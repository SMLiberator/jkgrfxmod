#pragma once
// Opt-in instrumentation of the JKGM x86 native simulation. No game-state
// writes, no replacement algorithms. Only completed scopes are aggregated.
#include "base/log.hpp"
#include <windows.h>
#include <detours/detours.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <sstream>

namespace jkgm::native_simulation_profile {
enum class stage : size_t {
    simulation, thing_update, physics_integrate, thing_move, floor,
    collision_move, collision_search, collision_thing, collision_surface,
    cog_scheduler, cog_vm, cog_message, cog_thing_links,
    verb_native, verb_jkdll, verb_renderer, verb_other, count
};
inline constexpr char const *names[] = {
    "simulation", "thing_update", "physics_integrate", "thing_move", "floor",
    "collision_move", "collision_search", "collision_thing", "collision_surface",
    "cog_scheduler", "cog_vm", "cog_message", "cog_thing_links",
    "jkgm.exe", "JK.dll", "renderer.dll", "other"
};
inline constexpr size_t stage_count=static_cast<size_t>(stage::count);
static_assert(sizeof(names)/sizeof(names[0])==stage_count);
using clock=std::chrono::steady_clock;
struct counter { uint64_t calls=0, inclusive=0, self=0; };
using counters=std::array<counter,stage_count>;
inline bool enabled=false;
inline bool verbs_enabled=false, verbs_verbose=false;
constexpr size_t first_verb=static_cast<size_t>(stage::verb_native);
struct verb_record {
    uintptr_t address=0;
    stage kind=stage::verb_other;
    char name[64]{};
    counter inside{}, outside{};
};
constexpr size_t verb_capacity=2048;
struct scope;
struct thread_state {
    scope *top=nullptr;
    counters inside{}, outside{};
    std::array<verb_record,verb_capacity> verbs{};
    uint64_t dropped_verbs=0;
    uint64_t frames=0;
    clock::time_point start{};
};
inline thread_local thread_state state{};

// Nested callbacks (COG -> collision -> COG, for example) subtract only
// direct measured children. Exclusive times can therefore be summed without
// double counting. Uninstrumented native verbs remain in cog_vm self.
struct scope {
    stage kind;
    bool active, inside=false;
    scope *parent=nullptr;
    clock::time_point start{};
    uint64_t child_ns=0;
    counter *detail=nullptr;
    explicit scope(stage s,verb_record *verb=nullptr) noexcept : kind(s), active(enabled) {
        if(!active)return;
        parent=state.top;
        inside=s==stage::simulation || (parent && parent->inside);
        state.top=this;
        if(verb)detail=inside?&verb->inside:&verb->outside;
        start=clock::now();
    }
    scope(scope const&)=delete;
    scope& operator=(scope const&)=delete;
    ~scope() noexcept {
        if(!active)return;
        auto const ns=static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now()-start).count());
        state.top=parent;
        auto &c=(inside?state.inside:state.outside)[static_cast<size_t>(kind)];
        auto const self=ns>=child_ns?ns-child_ns:0;
        ++c.calls;
        c.inclusive+=ns;
        c.self+=self;
        if(detail) {
            ++detail->calls;
            detail->inclusive+=ns;
            detail->self+=self;
        }
        if(parent)parent->child_ns+=ns;
    }
};

// Called at the gameplay present boundary, independently of the renderer
// profiler. Do not split an in-progress (possibly reentrant) simulation.
inline void present() {
    if(!enabled || state.top)return;
    auto const now=clock::now();
    if(state.start==clock::time_point{}) {
        // Discard startup/map-loading work preceding the first presentation.
        state.inside={}; state.outside={}; state.verbs={};
        state.dropped_verbs=0; state.start=now; return;
    }
    ++state.frames;
    if(now-state.start<std::chrono::seconds(2))return;
    auto const frames=state.frames;
    auto report=[&](char const *label,counters const &cs) {
        std::ostringstream out;
        out<<"PERF simulation "<<label<<" frames="<<frames;
        for(size_t i=0;i<first_verb;++i) {
            auto const &c=cs[i];
            out<<" "<<names[i]<<"(incl_us_avg/self_us_avg/calls_total)="
               <<c.inclusive/1000/frames<<"/"<<c.self/1000/frames<<"/"<<c.calls;
        }
        LOG_INFO(out.str());
    };
    report("inside_sithUpdate",state.inside);
    report("outside_sithUpdate",state.outside);
    if(verbs_enabled) {
        auto report_verbs=[&](char const *label,counters const &cs) {
            std::ostringstream out;
            out<<"PERF cog_verbs "<<label<<" frames="<<frames
               <<" (inclusive_us/frame,self_us/frame,calls_total,inclusive_ns/call,self_ns/call)";
            for(size_t i=first_verb;i<stage_count;++i) {
                auto const &c=cs[i];
                out<<" "<<names[i]<<"="<<c.inclusive/1000/frames<<"/"
                   <<c.self/1000/frames<<"/"<<c.calls<<"/"
                   <<(c.calls?c.inclusive/c.calls:0)<<"/"<<(c.calls?c.self/c.calls:0);
            }
            LOG_INFO(out.str());
        };
        report_verbs("inside_sithUpdate",state.inside);
        report_verbs("outside_sithUpdate",state.outside);
        if(verbs_verbose) {
            std::array<verb_record const*,verb_capacity> ranked{};
            size_t n=0;
            for(auto const &v:state.verbs)if(v.address)ranked[n++]=&v;
            auto self=[](verb_record const *v){return v->inside.self+v->outside.self;};
            std::sort(ranked.begin(),ranked.begin()+n,
                      [&](auto a,auto b){return self(a)>self(b);});
            LOG_INFO("PERF cog_verb_rank distinct_handlers=",n,
                     " untracked_calls=",state.dropped_verbs,
                     " NOTE=aliases_share_handler;ranked_by_self_time");
            for(size_t i=0;i<n && i<20;++i) {
                auto const &v=*ranked[i];
                auto const calls=v.inside.calls+v.outside.calls;
                std::ostringstream addr;
                addr<<std::hex<<v.address;
                LOG_INFO("PERF cog_verb_rank ",i+1," module=",
                         names[static_cast<size_t>(v.kind)]," name=",
                         v.name[0]?v.name:"(unknown)"," address=0x",addr.str(),
                         " inclusive_us_avg=",(v.inside.inclusive+v.outside.inclusive)/1000/frames,
                         " self_us_avg=",self(&v)/1000/frames,
                         " calls_total=",calls," self_ns_per_call=",calls?self(&v)/calls:0);
            }
            state.verbs={};
            state.dropped_verbs=0;
        }
    }
    state.inside={}; state.outside={}; state.frames=0; state.start=now;
}

template<stage S, uintptr_t Address, class R, class... Args>
struct hook {
    using fn=R(__cdecl *)(Args...);
    static inline fn real=reinterpret_cast<fn>(Address);
    static R __cdecl entry(Args... args) {
        scope timer(S);
        return real(args...);
    }
    static LONG attach() { return DetourAttach(&(PVOID&)real,entry); }
    static LONG detach() { return DetourDetach(&(PVOID&)real,entry); }
    static bool verify(std::initializer_list<unsigned char> bytes) {
        MEMORY_BASIC_INFORMATION region{};
        auto const p=reinterpret_cast<void const *>(Address);
        if(!VirtualQuery(p,&region,sizeof(region)) || region.State!=MEM_COMMIT ||
           (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        auto const end=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;
        return end>=Address && end-Address>=bytes.size() &&
               std::memcmp(p,bytes.begin(),bytes.size())==0;
    }
};
using update=hook<stage::simulation,0x004C4DF0U,int>;
using integrate=hook<stage::physics_integrate,0x004F5B70U,void,void*,float>;
// Native caller pushes Thing, delta seconds AND delta milliseconds.
using move=hook<stage::thing_move,0x004CC790U,void,void*,float,int>;
using floor=hook<stage::floor,0x004F57C0U,void,void*,int>;
using collision_move=hook<stage::collision_move,0x004E7BE0U,float,void*,void*,float,int>;
using search=hook<stage::collision_search,0x004E83F0U,float,void*,void*,void const*,void const*,float,float,int>;
using search_thing=hook<stage::collision_thing,0x004E86C0U,float,void*,void*,void const*,void const*,float,float,int>;
using search_surface=hook<stage::collision_surface,0x004E8960U,void,void*,void const*,void const*,float,float,int>;
using scheduler=hook<stage::cog_scheduler,0x004E0690U,void>;
using vm=hook<stage::cog_vm,0x004E21F0U,int,void*>;
using message=hook<stage::cog_message,0x004E25E0U,void,void*,int>;
using links=hook<stage::cog_thing_links,0x004DFD80U,float,void*,void*,int,float,float,float,float>;

// sithThing_Update is already detoured by the node attachment subsystem.
// Its existing trampoline is timed in main.cpp rather than hooked twice.
inline bool install(bool thing_hook_available) {
    if(!thing_hook_available ||
       reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr))!=0x00400000U ||
       !update::verify({0xf6,0x05,0xa4,0xe5,0x8e,0x00,0x08,0x53}) ||
       !integrate::verify({0x8b,0x44,0x24,0x04,0x8b,0x48,0x5c}) ||
       !move::verify({0x83,0xec,0x18,0x55,0x56,0x8b,0x74,0x24,0x24}) ||
       !floor::verify({0x83,0xec,0x20,0x53,0x55,0x8b,0x6c,0x24,0x2c}) ||
       !collision_move::verify({0x83,0xec,0x3c,0xd9,0x44,0x24,0x48}) ||
       !search::verify({0x83,0xec,0x08,0xa1,0xa0,0xba,0x54,0x00}) ||
       !search_thing::verify({0x83,0xec,0x18,0x8b,0x44,0x24,0x1c}) ||
       !search_surface::verify({0x83,0xec,0x30,0x53,0x55,0x56,0x57}) ||
       !scheduler::verify({0xa1,0xa0,0xe5,0x8e,0x00,0x53,0x8b,0x1d}) ||
       !vm::verify({0x81,0xec,0x90,0x00,0x00,0x00,0x53,0x55}) ||
       !message::verify({0x8b,0x44,0x24,0x08,0x55,0x56,0x8b,0x74,0x24,0x0c}) ||
       !links::verify({0x83,0xec,0x08,0xa1,0x78,0x6c,0x83,0x00})) {
        LOG_WARNING("Simulation profiler skipped: native signature or Thing update hook mismatch");
        return false;
    }
    LONG err=DetourTransactionBegin();
    if(err!=NO_ERROR) {
        LOG_WARNING("Simulation profiler transaction begin failed: ",err); return false;
    }
    err=DetourUpdateThread(GetCurrentThread());
    using action=LONG(*)();
    action const attachers[]={update::attach,integrate::attach,move::attach,
        floor::attach,collision_move::attach,search::attach,search_thing::attach,
        search_surface::attach,scheduler::attach,vm::attach,message::attach,links::attach};
    for(auto attach:attachers)if(err==NO_ERROR)err=attach();
    if(err!=NO_ERROR) {
        DetourTransactionAbort();
        LOG_WARNING("Simulation profiler attach failed: ",err);
        return false;
    }
    err=DetourTransactionCommit();
    enabled=err==NO_ERROR;
    if(enabled)LOG_INFO("Simulation profiler installed (12 detours + existing Thing update hook)");
    else LOG_WARNING("Simulation profiler commit failed: ",err);
    return enabled;
}
// Classify the registered entry point, not every instruction executed by
// the verb. In particular a renderer wrapper can call a native/JK.dll verb.
struct module_range {
    uintptr_t base=0, size=0;
    bool contains(uintptr_t a) const {return size && a>=base && a-base<size;}
};
inline module_range image_range(HMODULE module) {
    if(!module)return {};
    auto const base=reinterpret_cast<uintptr_t>(module);
    auto const dos=reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0)return {};
    auto const nt=reinterpret_cast<IMAGE_NT_HEADERS const*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE)return {};
    return {base,nt->OptionalHeader.SizeOfImage};
}
inline module_range native_range{}, jk_range{}, renderer_range{};
inline stage classify(uintptr_t a) {
    if(native_range.contains(a))return stage::verb_native;
    if(jk_range.contains(a))return stage::verb_jkdll;
    if(renderer_range.contains(a))return stage::verb_renderer;
    // JK.dll normally precedes renderer.dll, but allow a later load.
    if(!jk_range.size) {
        jk_range=image_range(GetModuleHandleA("JK.dll"));
        if(jk_range.contains(a))return stage::verb_jkdll;
    }
    return stage::verb_other;
}
inline verb_record *find_verb(uintptr_t addr,stage kind,void const *symbol) {
    if(!verbs_verbose)return nullptr;
    size_t slot=((addr>>2)*2654435761U)&(verb_capacity-1);
    for(size_t n=0;n<verb_capacity;++n,slot=(slot+1)&(verb_capacity-1)) {
        auto &v=state.verbs[slot];
        if(v.address==addr)return &v;
        if(v.address)continue;
        v.address=addr; v.kind=kind;
        // Native symbol lookup 0x4FD660 uses 28-byte records; the name
        // accessor 0x4FD6A0 reads +0x14. Snapshot the name, not its pointer.
        char const *name=nullptr;
        std::memcpy(&name,static_cast<unsigned char const*>(symbol)+0x14,sizeof(name));
        MEMORY_BASIC_INFORMATION region{};
        if(name && VirtualQuery(name,&region,sizeof(region)) && region.State==MEM_COMMIT &&
           !(region.Protect&(PAGE_NOACCESS|PAGE_GUARD))) {
            auto const limit=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;
            auto const start=reinterpret_cast<uintptr_t>(name);
            for(size_t i=0;i+1<sizeof(v.name) && start+i<limit;++i) {
                char const ch=name[i];
                if(!ch)break;
                if(static_cast<unsigned char>(ch)<32 || static_cast<unsigned char>(ch)>126)break;
                v.name[i]=ch;
            }
        }
        return &v;
    }
    ++state.dropped_verbs;
    return nullptr;
}
inline void __cdecl invoke_verb(void const *symbol,void *context) {
    using verb_fn=void(__cdecl *)(void*);
    verb_fn fn=nullptr;
    std::memcpy(&fn,static_cast<unsigned char const*>(symbol)+8,sizeof(fn));
    if(!fn)return; // original VM skips null handlers
    auto const addr=reinterpret_cast<uintptr_t>(fn);
    auto const kind=classify(addr);
    auto *detail=find_verb(addr,kind,symbol);
    scope timer(kind,detail);
    fn(context);
}
// The CALLFUNC branch contains a null test, push esi, call eax, add esp,4.
// Replace precisely this block, after the VM has validated the symbol type.
// Both the null and non-null paths originally converge at 0x4E2564.
// The bridge receives EAX=symbol, ESI=context and returns with ESP unchanged.
constexpr uintptr_t verb_site=0x004E238BU;
inline constexpr std::array<unsigned char,17> verb_original={
    0x8b,0x40,0x08,0x85,0xc0,0x0f,0x84,0xce,0x01,0x00,0x00,
    0x56,0xff,0xd0,0x83,0xc4,0x04};
inline std::array<unsigned char,17> verb_patch{};
inline bool write_verb_bytes(std::array<unsigned char,17> const &bytes) {
    auto const p=reinterpret_cast<void*>(verb_site);
    DWORD old=0;
    if(!VirtualProtect(p,bytes.size(),PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(p,bytes.data(),bytes.size());
    FlushInstructionCache(GetCurrentProcess(),p,bytes.size());
    DWORD ignored=0;
    if(!VirtualProtect(p,bytes.size(),old,&ignored))
        LOG_WARNING("COG verb profiler: could not restore original page protection");
    return true;
}
inline bool install_verbs(HMODULE renderer,void (*bridge)(),bool verbose) {
    if(!enabled) {
        LOG_WARNING("COG verb profiler requires active simulation profiling");
        return false;
    }
    auto const p=reinterpret_cast<void const*>(verb_site);
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(p,&region,sizeof(region)) || region.State!=MEM_COMMIT ||
       (region.Protect&(PAGE_NOACCESS|PAGE_GUARD)) ||
       reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize-verb_site<verb_original.size() ||
       std::memcmp(p,verb_original.data(),verb_original.size())!=0) {
        LOG_WARNING("COG verb profiler skipped: CALLFUNC signature mismatch");
        return false;
    }
    native_range=image_range(GetModuleHandleA(nullptr));
    jk_range=image_range(GetModuleHandleA("JK.dll"));
    renderer_range=image_range(renderer);
    verb_patch.fill(0x90);
    verb_patch[0]=0xe8;
    uint32_t const rel=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bridge)-(verb_site+5));
    std::memcpy(verb_patch.data()+1,&rel,4);
    verbs_verbose=verbose;
    verbs_enabled=write_verb_bytes(verb_patch);
    if(verbs_enabled)LOG_INFO("COG verb module profiler installed; detailed handler ranking=",verbose);
    else LOG_WARNING("COG verb profiler could not patch CALLFUNC");
    return verbs_enabled;
}
inline void uninstall_verbs() {
    if(!verbs_enabled)return;
    if(std::memcmp(reinterpret_cast<void const*>(verb_site),verb_patch.data(),verb_patch.size())==0) {
        if(!write_verb_bytes(verb_original))LOG_WARNING("COG verb profiler could not restore CALLFUNC");
    } else LOG_WARNING("COG verb profiler CALLFUNC changed; not overwriting");
    verbs_enabled=false;
}
inline void uninstall() {
    uninstall_verbs();
    if(!enabled)return;
    enabled=false;
    LONG err=DetourTransactionBegin();
    if(err!=NO_ERROR)return;
    err=DetourUpdateThread(GetCurrentThread());
    using action=LONG(*)();
    action const detachers[]={links::detach,message::detach,vm::detach,
        scheduler::detach,search_surface::detach,search_thing::detach,
        search::detach,collision_move::detach,floor::detach,move::detach,
        integrate::detach,update::detach};
    for(auto detach:detachers)if(err==NO_ERROR)err=detach();
    if(err!=NO_ERROR) {
        DetourTransactionAbort(); LOG_WARNING("Simulation profiler detach failed: ",err); return;
    }
    err=DetourTransactionCommit();
    if(err!=NO_ERROR)LOG_WARNING("Simulation profiler detach commit failed: ",err);
}
}
