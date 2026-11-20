# Native template-spawn parent inheritance

Enabled automatically in renderer.dll on the supplied **jkgm(5).exe** only.
The patch is not a replacement for JK's Thing ownership semantics: it makes
inconsistent template-generated Things follow the same **root-owner-or-source**
policy already used by ordinary projectile `explode` and `fleshhit`.

| Creation path | Parent assigned to generated Thing |
|---|---|
| `creatething` | Existing supplied owner, or the newly created source Thing if it has no parent |
| `explode` on actor death | Source actor's valid root owner; otherwise the dying actor |
| `debris` emitted by explosion | Explosion's valid root owner; otherwise explosion itself |
| Hitscan weapon `explode` (two paths) | Weapon's valid root owner; otherwise weapon itself |
| Ordinary projectile `explode` and `fleshhit` | **Unchanged**: native `sithThing_GetThingParent(projectile)` |

In JK, `GetThingParent()` traverses parent+signature links to return the
highest valid ancestor, or the Thing itself if it has no parent.

The parent pointer and matching signature are assigned in the original
`sithThing_CreateThingAtPos()` *before* the generated Thing's synchronous
`created:` message runs. Hence a COG can obtain it immediately:

```c
created:
    parent = GetThingParent(GetSenderRef());
    Return;
```

A source that is destroyed shortly after spawning may not remain a valid
parent later; scripts should cache the index/identity during `created:`
if they need it after a yield or future frame.

## Implementation

Five validated CALL instructions inside native JKGM are redirected to ABI-
compatible assembly thunks that supply the missing parent argument and then
tail-jump into the *original* native creation routine. No extra Thing
registry, changes to the COG VM, extra call to `sithThing_Initialize`, or
replacement of the normal weapon explosion/fleshhit logic are involved.

The thunks use native `sithThing_GetThingParent()` for correct root-owner
resolution, including the original parent-signature checks. Only the
recursive `creatething` thunk preserves its already-provided parent,
falling back to the just-created Thing.

All 5 callsites, their original CALL targets, surrounding stack setup,
and both native function prologues are signature checked before patching.
Either all modifications are applied or any partial patching is rolled back.
Restoration occurs during renderer.dll teardown. No user config required.

Runtime checks: each path with and without a parent; `created:` gets
expected parent; source Thing removed after spawn; recursive `creatething`;
explosion damage/shooter-exclusion parity; no regression in vanilla effects.
