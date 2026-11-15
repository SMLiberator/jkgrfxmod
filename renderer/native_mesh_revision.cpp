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
    constexpr size_t mutator_count=17U;
    // Verified in the user's JK.dll: SetModelMesh, CloneModel and
    // ChangeModelMaterial are registered. Other supported mutators are
    // optional, not required for the revision fast path.
    constexpr uint32_t required_mask=(1U<<0)|(1U<<1)|(1U<<2);
    constexpr std::array<char const *,mutator_count> names{{
        "setmodelmesh","clonemodel","changemodelmaterial",
        "replacemodelmaterial","appendthingmaterial","appendmodelmaterial",
        "appendthingemptymesh","appendmodelemptymesh","appendmodelnode",
        "replacemodelmeshmaterial","clearmodel","clearmodelmesh",
        "overwritemodel","reloadmodel","setmodelnodemesh","swapmodelmesh",
        "setthingmesh"
    }};
    std::array<handler,mutator_count> originals{};
    registrar original_registrar=nullptr;
    uint32_t registered_mask=0U;
    uint64_t generation=1U;
    bool complete=false;

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
        invoke<12>,invoke<13>,invoke<14>,invoke<15>,invoke<16>
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
    }
    void finish()
    {
        // Do not require verbs that this DLL does not register. Every
        // supported mutation verb that is actually registered is wrapped.
        complete=(registered_mask&required_mask)==required_mask;
        size_t registered_count=0;
        for(size_t i=0;i<mutator_count;++i)
            if((registered_mask&(1U<<i))!=0U)++registered_count;
        if(complete) {
            LOG_INFO("GPU 3DO revision tracking active: ",registered_count,
                     " of ",mutator_count," known JK.dll mutation verbs registered/wrapped; "
                     "unregistered optional verbs do not force full hashing; "
                     "integrity audits disabled by default");
        } else {
            LOG_WARNING("GPU 3DO revision tracking inactive: missing required "
                        "SetModelMesh / CloneModel / ChangeModelMaterial "
                        "registrations (mask=",registered_mask,"); full hashing retained");
        }
    }
    bool active() {return complete;}
    uint64_t current() {return generation;}
}
