# JK.dll mesh mutation research (2026-11-14)

## Binary examined

User-supplied `JK.dll`, 786,432 bytes; PE32 x86, preferred image base
`0x10000000`; SHA-256
`d7fa73fa1e2fcb03fd1353033950691804c83ea902b4b26ba4099d9983916d14`.
The offsets below are **preferred virtual addresses in this exact file**, not
universal function addresses for other builds. Static analysis only; no
in-process test was performed.

## Registration: the key performance bug

The DLL's `InitNewCogVerbs` registration sequence includes:

| COG verb | Registration site | Handler |
| --- | --- | --- |
| `SetModelMesh` | `0x1005378B` | `0x1004E8A0` |
| `CloneModel` | `0x1005376E` | `0x1004E870` |
| `ChangeModelMaterial` | `0x10053751` | `0x1004E7B0` |
| `SetThingMesh` | `0x10053BF6` | `0x10050BC0` |

The **original** 16-verb tracker included only the first three and
required all 16 registrations, which left the revision fast path inactive
and forced full-per-frame hashing. The updated implementation recognizes
`SetThingMesh`, enables scoped tracking only when all four known DLL
registrations and the supplied executable signatures are present, and
instruments those handlers without changing COG arguments or results.
Other mutators remain optional: if they register and execute with unverified
argument semantics, they trigger a one-time global revalidation. Additional
registration paths in other modules have **not** yet been verified.
The integrity audit remains opt-in, with interval `0` disabling it.

## Handler behavior

- `CloneModel` (`0x1004E870`): calls native `PopModel3` at
  `0x004E3140`, then calls the DLL cloning routine at `0x10087020`,
  returning its new model identifier. `0x10087020` appends an
  independent model to the global model collection and deep-copies mesh
  arrays using per-model allocations. It does not appear to mutate the
  original model's existing mesh contents. A new clone therefore should
  not force source-geometry validation of unrelated live meshes;
  **reused native pointer addresses still require cache lifecycle handling**.
- `SetModelMesh` (`0x1004E8A0`): resolves source and destination
  mesh selectors and two models; reads model mesh counts at `+0x24`,
  mesh array pointers at `+0x28`, and geoset counts at `+0x44`.
  The submesh stride observed in the copy loop is `0x70`. For the
  selected destination slot the handler copies mesh fields and deep-copies
  dynamically allocated arrays (helper `0x10050DA8`); the loop traverses
  model geosets. One call may affect the corresponding slot **in multiple
  geosets**, not one isolated `rdMesh*`.
- `ChangeModelMaterial` (`0x1004E7B0`): changes material references
  inside a model, including existing mesh-face references. Geometry
  rebuild requirements should be separated from material-binding changes.
- `SetThingMesh` (`0x10050BC0`): resolves a `SithThing` and its model,
  with a clone-on-write path using `0x10087020`, then updates the
  thing's target mesh. The *post-clone* model identity must be used
  for targeted cache invalidation. This is **not merely conceptual**:
  its implementation jumps directly into `SetModelMesh`'s shared selector
  and mesh-copy block at `0x1004E937`; both paths use the actual mesh
  copying code at `0x1004EA0D`–`0x1004EA90`.

## Previously agreed invalidation scope (November 12–13)

The 16-entry registration array was a broad historical approximation,
**not** the agreed list of operations that should cause global geometry
rehashing. The prior design agreement was:

| Operation | Intended cache action |
| --- | --- |
| `SetModelMesh`, `SetThingMesh` | Revalidate/rebuild only the overwritten destination mesh slot(s), across affected geosets; Thing variants resolve the final model after copy-on-write |
| `ClearModelMesh` | Remove only the cleared mesh entries |
| `ClearModel`, `OverwriteModel`, `ReloadModel` | Invalidate/remove entries belonging to that model; never global |
| `ChangeModelMaterial`, `ReplaceModelMaterial`, `ReplaceModelMeshMaterial` | Refresh affected material references/render groups; do not rebuild position/UV buffers just because a material changed |
| `SwapModelMesh`, `SetModelNodeMesh` | Update changed mesh references/hierarchy mapping; do not treat pure swaps as geometry writes |
| `CloneModel` | Track creation/ownership of newly allocated model data, not changes to the original model |
| Append-only material/mesh/node verbs | Ensure new entries are discoverable on first use; no invalidation of unrelated existing entries |
| Thing-form wrappers | Reuse the corresponding model-level invalidation semantics, after resolving ownership/clone-on-write |

These are **design decisions**, not claims that every verb listed is
implemented/registered in the supplied DLL. In particular, the strings
`ClearModel`, `ClearModelMesh`, `OverwriteModel`,
`ReplaceModelMeshMaterial`, `SwapModelMesh`, etc. appear in
the supplied DLL's data, but there are **no direct code references to
their string addresses** in this binary, unlike the four confirmed
registrations. The actual implementations/registrations for those verbs
must be checked in the module(s) that provide them.

A mutation wrapper's mere invocation should **not** automatically
increment a global geometry generation. No-op/failed calls, cloning,
append-only operations and material-reference changes must be handled
according to their actual effects.

## Executable verification and first implementation (2026-11-14)

User-provided `jkgm(5).exe`: PE32 x86, base `0x00400000`, SHA-256
`afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f`.

Disassembly confirms the JK.dll native calls:

- `0x004E3140` — `PopModel3`; COG stack count at `ctx+0x498`,
  16-byte entries at `ctx+0x98`, symbol table at `ctx+0x94`.
- `0x004E2B50` — `PopThing`; same COG stack representation.
- `0x004FD660` — symbol lookup: 28-byte symbol entries, value +4,
  optionally the global symbol table from `0x008B5368`.
- Current/static world pointers `0x00833A10`/`0x00833A14`.
- `PopModel3` uses model count `world+0x64`, model array
  `world+0x6C`, stride `0x84`.
- `PopThing` uses Thing count `world+0xC0`, Thing array
  `world+0xC4`, stride `0x2B0`.
- The DLL's `SetThingMesh` reads the Thing's `rdThing.type`
  at `thing+0xC4` and the **post-clone** model pointer at `thing+0xC8`.

The implementation now records a revision for that destination **model**
without scanning any other model's mesh bytes, while `CloneModel`
skips invalidating source geometry entirely. For supported COG variables,
the original argument is read through the symbol table without popping it.
Unknown or unverified mutations conservatively advance a separate global
GPU fallback epoch. Type-3 collision revision tracking remains separate.

The implementation is **model-scoped**, not yet mesh-slot scoped. One
`SetModelMesh` call can update its selected slot across multiple geosets;
once that selector parsing is verified, the target epoch can be narrowed
further. Runtime confirmation and a Windows CI build are still required.

## Recommended cache design

1. Intercept mutation calls that **actually execute**, across **all
   registration sources**, not merely `JK.dll::InitNewCogVerbs`.
2. Preserve COG arguments/results and never pop or replay arguments
   purely for diagnostics.
3. On `SetModelMesh`, record destination model identity and selected
   destination mesh slot; mark that slot in each affected geoset. On
   `SetThingMesh`, record the final thing-owned model after copy-on-write.
4. Track clone allocation/lifetime separately rather than invalidating
   unrelated cached meshes globally. Handle recycled `rdMesh*` addresses.
5. Model-wide invalidation is an acceptable temporary fallback **only for
   that model** when a verb's affected submeshes cannot be identified.
6. Keep the optional integrity audit at interval `0` by default. If
   enabled, report exact mismatches rather than permanently switching to
   full hashing; investigate untracked writers and add specific hooks.
7. Instrument the selected mutation pathway only once target identity
   has been verified in the supported game executable. Native game
   `PopModel3` is referenced by the DLL at `0x004E3140`, but this
   **does not establish a safe detour signature** for every executable.

Static disassembly supports the listed addresses and layouts, but runtime
validation is still required. Model-level (not mesh-level) invalidation is
implemented with a signature-gated COG stack reader; no mesh-slot-specific
implementation is claimed.
