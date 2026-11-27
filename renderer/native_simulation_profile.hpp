#pragma once
// Opt-in instrumentation of the JKGM x86 native simulation. No game-state
// writes, no replacement algorithms. Only completed scopes are aggregated.
#include "base/log.hpp"
#include <windows.h>
#include <detours/detours.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <sstream>

namespace jkgm::native_simulation_profile {
enum class stage : size_t {
    simulation, thing_update, physics_integrate, thing_move, floor,
    collision_move, collision_search, collision_thing, collision_surface,
    cog_scheduler, cog_vm, cog_message, cog_thing_links, count
};
inline constexpr char const *names[] = {
    "simulation", "thing_update", "physics_integrate", "thing_move", "floor",
    "collision_move", "collision_search", "collision_thing", "collision_surface",
    "cog_scheduler", "cog_vm", "cog_message", "cog_thing_links"
};
inline constexpr size_t stage_count=static_cast<size_t>(stage::count);
static_assert(sizeof(names)/sizeof(names[0])==stage_count);
using clock=std::chrono::steady_clock;
struct counter { uint64_t calls=0, inclusive=0, self=0; };
using counters=std::array<counter,stage_count>;
inline bool enabled=false;
struct scope;
struct thread_state {
    scope *top=nullptr;
    counters inside{}, outside{};
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
    explicit scope(stage s) noexcept : kind(s), active(enabled) {
        if(!active)return;
        parent=state.top;
        inside=s==stage::simulation || (parent && parent->inside);
        state.top=this;
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
        ++c.calls;
        c.inclusive+=ns;
        c.self+=ns>=child_ns?ns-child_ns:0;
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
        state.inside={}; state.outside={}; state.start=now; return;
    }
    ++state.frames;
    if(now-state.start<std::chrono::seconds(2))return;
    auto const frames=state.frames;
    auto report=[&](char const *label,counters const &cs) {
        std::ostringstream out;
        out<<"PERF simulation "<<label<<" frames="<<frames;
        for(size_t i=0;i<stage_count;++i) {
            auto const &c=cs[i];
            out<<" "<<names[i]<<"(incl_us_avg/self_us_avg/calls_total)="
               <<c.inclusive/1000/frames<<"/"<<c.self/1000/frames<<"/"<<c.calls;
        }
        LOG_INFO(out.str());
    };
    report("inside_sithUpdate",state.inside);
    report("outside_sithUpdate",state.outside);
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
using move=hook<stage::thing_move,0x004CC790U,void,void*,float>;
using floor=hook<stage::floor,0x004F57C0U,void,void*,int>;
using collision_move=hook<stage::collision_move,0x004E7BE0U,float,void*,void*,float,int>;
using search=hook<stage::collision_search,0x004E83F0U,float,void*,void*,void const*,void const*,float,float,int>;
using search_thing=hook<stage::collision_thing,0x004E86C0U,float,void*,void*,void const*,void const*,float,float,int>;
using search_surface=hook<stage::collision_surface,0x004E8960U,void,void*,void const*,void const*,float,float,int>;
using scheduler=hook<stage::cog_scheduler,0x004E0690U,void>;
using vm=hook<stage::cog_vm,0x004E21F0U,void,void*>;
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
inline void uninstall() {
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
