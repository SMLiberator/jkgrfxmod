#include "native_mesh_revision.hpp"

#include "base/log.hpp"
#include <array>
#include <cstddef>
#include <cstring>

// The user's JK.dll exposes mesh/material mutations through registered
// COG verbs. Wrap the verb *registration*, not DLL machine-code offsets:
// this preserves original arguments/results and detects modifications
// immediately after the verb handler returns.
namespace mesh_revision {
    constexpr std::array<char const *,mutator_count> names{{
        "setmodelmesh","clonemodel","changemodelmaterial",
        "replacemodelmaterial","appendthingmaterial","appendmodelmaterial",
        "appendthingemptymesh","appendmodelemptymesh","appendmodelnode",
        "replacemodelmeshmaterial","clearmodel","clearmodelmesh",
        "overwritemodel","reloadmodel","setmodelnodemesh","swapmodelmesh"
    }};
    std::array<handler,mutator_count> originals{};
    mutator_counts calls{}; // per-verb counts since last profiling sample
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
        ++calls[I]; // count real invocations, not individual mesh hashes
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
        calls.fill(0U);
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
    mutator_counts take_mutator_counts()
    {
        auto const sample=calls;
        calls.fill(0U);
        return sample;
    }
    char const *mutator_name(std::size_t index)
    {
        return index<names.size()?names[index]:"unknown";
    }
    void report_untracked()
    {
        if(untracked_write)return;
        untracked_write=true;
        LOG_WARNING("GPU 3DO revision audit detected a mesh write outside "
                    "tracked JK.dll COG verbs; disabling fast path, "
                    "restoring full per-frame hashing for this session");
    }
}
