# GPU-cached animated explosion sprites: binary-verified native cel

**Reference executable:** user-supplied `jkgm(5).exe`, 32-bit PE.

At `0x0046D359`, the executable's native `rdSprite_Draw` copies
`[rdThing+0x2C]` into `[rdProcEntry+0x30]`. The instruction sequence
is `8B 51 2C 8B 4F 60 89 56 30`. The `+0x30` destination is on a
*different struct* and must not be used as `rdThing.wallCel`.

The GPU SPR path now samples the live per-Thing cel from `+0x2C`.
Explosion Things with `typeflags & 0x1` are animated by JK's own
native animation system: their MAT cel count and duration determine
the progression. The GPU renderer does not need to derive frames from
a duplicate timer or guess any SithThing explosionParams offsets.

The explicit cel is stored on the per-MAT/per-cel shared GPU quad and
resolved through the existing native MAT texture cache. If a requested
MAT cel is not yet resident, JK's normal sprite submission warms it
and GPU drawing can resume later.

An earlier temporary native-only workaround and lifetime-based cel
estimator were removed. The cache toggles are unchanged. Runtime
signature validation also checks the nine-byte native cel-copy
instruction sequence to avoid silently trusting this offset on a
different executable.

For unrelated ordinary SPR native fallbacks, enable
`profile_renderer`; the renderer emits a bounded sample of GPU
eligibility failures, missing SPR authored-type registrations, and
MAT readiness details (stage, material, cel).
