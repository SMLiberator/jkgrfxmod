#include "native_mesh_revision.hpp"

#include "base/log.hpp"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>

// Revisions for native JK.dll COG mutations. The DLL and the supplied
// jkgm(5).exe use the 1998 JK23 COG ABI. Never pop interpreter arguments:
// peeking at the validated native stack leaves handlers/results untouched.
namespace mesh_revision {
    constexpr size_t mutator_count=17U;
    constexpr std::array<char const *,mutator_count> names{{
        "setmodelmesh","clonemodel","changemodelmaterial",
        "replacemodelmaterial","appendthingmaterial","appendmodelmaterial",
        "appendthingemptymesh","appendmodelemptymesh","appendmodelnode",
        "replacemodelmeshmaterial","clearmodel","clearmodelmesh",
        "overwritemodel","reloadmodel","setmodelnodemesh","swapmodelmesh",
        "setthingmesh"
    }};
    // Only the four mutations verified as registered in the user's DLL
    // are mandatory; optional mutators, if encountered, get a conservative
    // global fallback until their argument layouts are individually verified.
    constexpr uint32_t required_mask=(1U<<0)|(1U<<1)|(1U<<2)|(1U<<16);
    std::array<handler,mutator_count> originals{};
    registrar original_registrar=nullptr;
    uint32_t registered_mask=0U;
    uint64_t generation=1U; // still used by precise type-3 collision cache
    uint64_t fallback_epoch=0U; // unidentified mutations only
    std::unordered_map<uintptr_t,uint64_t> model_epochs;
    bool complete=false;

    bool read_u32(uintptr_t address,uint32_t &value)
    {
        if(address<0x10000U || address>0x7ffffffbU)return false;
        SIZE_T received=0;
        return ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<void const *>(address),&value,
            sizeof(value),&received)!=0 && received==sizeof(value);
    }

    bool signature_matches(uintptr_t address,uint8_t const *signature,size_t len)
    {
        if(len>16U)return false;
        std::array<uint8_t,16> actual{};
        SIZE_T received=0;
        return ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<void const *>(address),actual.data(),
                len,&received)!=0 &&
               received==len && std::memcmp(actual.data(),signature,len)==0;
    }

    bool supported_executable()
    {
        // Exact entry signatures from the provided jkgm(5).exe.
        // The DLL calls these fixed game addresses directly.
        constexpr uint8_t pop_model[]={
            0x55,0x8b,0xec,0x83,0xec,0x10,0x8b,0x4d,0x08};
        constexpr uint8_t pop_thing[]={
            0x55,0x8b,0xec,0x83,0xec,0x10,0x8b,0x4d,0x08};
        constexpr uint8_t lookup_symbol[]={
            0x8b,0x44,0x24,0x08,0x3d,0x00,0x01,0x00,0x00};
        return signature_matches(0x004e3140U,pop_model,sizeof(pop_model)) &&
               signature_matches(0x004e2b50U,pop_thing,sizeof(pop_thing)) &&
               signature_matches(0x004fd660U,lookup_symbol,sizeof(lookup_symbol));
    }

    // Validated COG stack: count @ ctx+0x498; 16-byte values @ ctx+0x98.
    // Symbol table @ ctx+0x94, with 28-byte symbol entries and value @ +4.
    bool peek_int(void *ctx,uint32_t from_top,int32_t &value)
    {
        uintptr_t const c=reinterpret_cast<uintptr_t>(ctx);
        uint32_t stack_count=0;
        if(!c || !read_u32(c+0x498U,stack_count) ||
           stack_count<from_top || stack_count>64U)return false;
        uintptr_t const item=c+0x98U+size_t(stack_count-from_top)*16U;
        uint32_t type=0,raw=0;
        if(!read_u32(item,type) || !read_u32(item+4U,raw))return false;
        if(type==1U) {
            uint32_t table=0;
            uint32_t symbol_index=raw;
            if(symbol_index>=0x100U) {
                symbol_index-=0x100U;
                if(!read_u32(0x008b5368U,table))return false;
            } else if(!read_u32(c+0x94U,table))return false;
            uint32_t count=0,base=0;
            if(!table || !read_u32(uintptr_t(table)+8U,count) ||
               !read_u32(table,base) || !base || count>65536U ||
               symbol_index>=count)return false;
            uintptr_t const sym=uintptr_t(base)+size_t(symbol_index)*28U+4U;
            if(!read_u32(sym,type) || !read_u32(sym+4U,raw))return false;
        }
        if(type==0U) { // indirect integer value (same as native PopModel3)
            if(!read_u32(raw,raw))return false;
            type=3U;
        }
        if(type==3U) {
            value=static_cast<int32_t>(raw);
            return true;
        }
        if(type==2U) {
            float f=0.0f;
            std::memcpy(&f,&raw,sizeof(f));
            if(!std::isfinite(f) || f<-10000000.0f || f>10000000.0f)
                return false;
            value=static_cast<int32_t>(std::nearbyint(f));
            return true;
        }
        return false;
    }

    bool current_world(uint32_t index,uint32_t &world)
    {
        // PopModel3 and PopThing honor the 0x800000 static-world bit.
        uint32_t const slot=(index&0x00800000U)
            ? 0x00833a14U : 0x00833a10U;
        return read_u32(slot,world) && world!=0U;
    }
    uintptr_t model_from_cog(void *ctx,uint32_t from_top)
    {
        int32_t index=0;
        if(!peek_int(ctx,from_top,index) || index<0)return 0U;
        uint32_t value=static_cast<uint32_t>(index);
        uint32_t world=0,count=0,base=0;
        if(!current_world(value,world) ||
           !read_u32(uintptr_t(world)+0x64U,count) ||
           !read_u32(uintptr_t(world)+0x6cU,base) ||
           !base || count>65536U)return 0U;
        value&=~0x00800000U;
        if(value>=count)return 0U;
        return uintptr_t(base)+size_t(value)*0x84U;
    }
    uintptr_t thing_from_cog(void *ctx,uint32_t from_top)
    {
        int32_t index=0;
        if(!peek_int(ctx,from_top,index) || index<0)return 0U;
        uint32_t value=static_cast<uint32_t>(index);
        uint32_t world=0,count=0,base=0;
        if(!current_world(value,world) ||
           !read_u32(uintptr_t(world)+0xc0U,count) ||
           !read_u32(uintptr_t(world)+0xc4U,base) ||
           !base || count>65536U)return 0U;
        value&=~0x00800000U;
        if(value>=count)return 0U;
        uintptr_t const thing=uintptr_t(base)+size_t(value)*0x2b0U;
        uint32_t type=0;
        if(!read_u32(thing+0x0cU,type) || type==0U)return 0U;
        return thing;
    }
    uintptr_t thing_model(uintptr_t thing)
    {
        uint32_t rd_type=0,model=0;
        // SithThing +0xC4 embeds rdThing; rdThing.type=1 is a 3DO and
        // its model3 pointer is rdThing+4. This is the *post-clone* model.
        if(!thing || !read_u32(thing+0xc4U,rd_type) || rd_type!=1U ||
           !read_u32(thing+0xc8U,model))return 0U;
        return model;
    }

    template<size_t I>
    void __cdecl invoke(void *ctx)
    {
        uintptr_t target=0U;
        if constexpr(I==0) // SetModelMesh(dstModel,dstSelector,srcModel,srcSelector)
            target=model_from_cog(ctx,4U);
        else if constexpr(I==2) // ChangeModelMaterial(dstModel,...)
            target=model_from_cog(ctx,3U);
        else if constexpr(I==16) // SetThingMesh(dstThing,...)
            target=thing_from_cog(ctx,4U);

        // The original mutator owns all stack consumption and return values.
        originals[I](ctx);
        ++generation;
        if constexpr(I==1) {
            // CloneModel allocates independent geometry: source untouched.
            return;
        } else {
            if constexpr(I==16)
                target=thing_model(target);
            if constexpr(I==0 || I==2 || I==16) {
                if(target!=0U) {
                    model_epochs[target]=generation;
                    return;
                }
            }
            // Unknown/unresolved destination: preserve correctness by
            // checking other cached models once, never every frame.
            fallback_epoch=generation;
            static std::array<uint32_t,mutator_count> warnings{};
            if(warnings[I]++<3U)
                LOG_WARNING("GPU 3DO revision unresolved: verb=",names[I],
                            " using one-time global revalidation");
        }
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
        bool const signatures_ok=supported_executable();
        complete=(registered_mask&required_mask)==required_mask &&
                 signatures_ok;
        size_t registered_count=0;
        for(size_t i=0;i<mutator_count;++i)
            if((registered_mask&(1U<<i))!=0U)++registered_count;
        if(complete)
            LOG_INFO("GPU 3DO model-scoped revision tracking active: ",
                     registered_count," known mutation verbs registered; ",
                     "unrelated models retain cached source signatures");
        else
            LOG_WARNING("GPU 3DO model revision tracking unavailable: ",
                        "required verb registration or executable signature ",
                        "missing; full per-frame source hashing retained; mask=",
                        registered_mask," exe_match=",signatures_ok?1:0);
    }
    bool active() {return complete;}
    uint64_t current() {return generation;}
    uint64_t current_for_model(uintptr_t model)
    {
        auto const it=model_epochs.find(model);
        uint64_t const local=it==model_epochs.end()?0U:it->second;
        return local>fallback_epoch?local:fallback_epoch;
    }
    void reset_cache_state()
    {
        model_epochs.clear();
        fallback_epoch=0U;
    }
}
