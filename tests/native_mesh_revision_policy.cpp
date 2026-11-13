#include <cassert>
#include <cstddef>

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
    static_assert(source_effect_for(12U)==source_full,
                  "OverwriteModel can replace existing source bytes");
    static_assert(source_effect_for(13U)==source_full,
                  "ReloadModel can replace existing source bytes");
    // Clones and appended resources are new cache keys. Mesh/node swaps
    // are caught by per-rdMesh header verification, not global hashing.
    for(std::size_t index: {0U,1U,4U,5U,6U,7U,8U,10U,11U,14U,15U})
        assert(source_effect_for(index)==source_none);
    assert(source_effect_for(99U)==source_none);
}
