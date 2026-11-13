#include <cassert>
#include <cstddef>
#include <initializer_list>

// This small, pure policy test runs independently of JK.dll and OpenGL.
// The COG interpreter ABI is Windows cdecl; only the policy is exercised.
#ifndef _MSC_VER
#define __cdecl
#endif
#include "renderer/native_mesh_revision.hpp"

int main()
{
    using namespace mesh_revision;
    static_assert(mutator_count==16U,"Update the mutation policy for new verbs");
    static_assert(source_effect_for(2U)==source_materials,
                  "ChangeModelMaterial edits face material pointers");
    static_assert(source_effect_for(3U)==source_materials,
                  "ReplaceModelMaterial edits face material pointers");
    static_assert(source_effect_for(9U)==source_materials,
                  "ReplaceModelMeshMaterial edits face material pointers");
    static_assert(source_effect_for(0U)==source_full,
                  "SetModelMesh may overwrite an existing rdMesh slot");
    static_assert(source_effect_for(11U)==source_full,
                  "ClearModelMesh can invalidate existing mesh contents");
    static_assert(source_effect_for(12U)==source_full,
                  "OverwriteModel can replace existing source bytes");
    static_assert(source_effect_for(13U)==source_full,
                  "ReloadModel can replace existing source bytes");
    // Clones and appended resources are new cache keys. Pure node swaps
    // and structurally clear model references use per-mesh header validation.
    for(std::size_t index: {1U,4U,5U,6U,7U,8U,10U,14U,15U})
        assert(source_effect_for(index)==source_none);
    assert(source_effect_for(99U)==source_none);
}
