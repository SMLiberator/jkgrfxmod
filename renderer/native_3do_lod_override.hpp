#pragma once
// Stage 0 of automatic GPU 3DO LOD: source-of-truth is native GeoSet 0.
//
// rdModel3_Draw normally reads rdThing.geosetSelect (+0x30), using the
// mutable rdModel3.geosetSelect (+0x50) when that per-Thing value is -1.
// Override ONLY the per-Thing field during rdModel3_Draw, before its mesh
// callbacks. The caller restores the original field immediately afterward.
// This never changes collision geometry, authored model data, or shared
// rdModel3 state. The actual GPU simplification/LOD selection comes later.
#include <cstdint>

namespace jkgm {
    struct native_3do_lod0_plan {
        uintptr_t selection_address=0;
        int32_t previous=-1;
        bool valid=false;
    };

    template<class Read>
    native_3do_lod0_plan prepare_native_3do_lod0_override(
        Read const &read,uintptr_t rdthing,uintptr_t expected_model)
    {
        native_3do_lod0_plan result{};
        if(rdthing<0x10000U || rdthing>0x7ff00000U-0x48U ||
           expected_model<0x10000U ||
           expected_model>0x7ff00000U-0x60U)return result;
        uint32_t actual_model=0,geosets=0;
        int32_t selected=-1;
        if(!read(rdthing+0x04U,&actual_model,sizeof(actual_model)) ||
           actual_model!=expected_model ||
           !read(expected_model+0x44U,&geosets,sizeof(geosets)) ||
           !read(rdthing+0x30U,&selected,sizeof(selected)) ||
           geosets==0U || geosets>4U ||
           (selected!= -1 && (selected<0 || uint32_t(selected)>=geosets)))
            return result;
        result.selection_address=rdthing+0x30U;
        result.previous=selected;
        result.valid=true;
        return result;
    }
} // namespace jkgm
