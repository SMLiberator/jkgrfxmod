# JKGM ThingFlags audit (patched game executable)

**Executable audited:** `jkgm(4).exe`, SHA-256
`afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f`.
All addresses below are virtual addresses in that executable.
The public reference engine is **OpenJKDF2**, which is a useful
comparison but not ABI-identical to our patched JKGM binary.

## Existing bit behavior

| ThingFlags bit | Status | Evidence and interpretation |
| --- | --- | --- |
| `0x20` | **Provisional custom candidate** | No confirmed read of this bit in a native `SithThing.flags` field was identified in the audited collision/physics paths or OpenJKDF2. JKGM tests at `0x004D39F5`, `0x004E753E`, `0x004F582A`, `0x004F5B15` refer to **collision-hit records**, not the underlying Thing flag field. OpenJKDF2 uses `SITH_TF_20` as a numeric mask of **explosionParams.flags** in `sithExplosion.c`, not `SithThing.flags`. That is NOT proof the Thing bit is unused in every native subsystem or other patches. |
| `0x80` | **Occupied: special raycast inclusion** | Confirmed in this JKGM executable at `0x004E8701`: if incoming search flags include `0x8`, `test BYTE PTR [esi],0x80` restricts the eligible Things. This matches OpenJKDF2 `sithCollision.c`, which labels the mask `SITH_TF_80` and the search flag `RAYCAST_8`. |
| `0x8000` | **Reserved (historically MP exclusion)** | Massassi's JediKnight Unofficial Specs identify `0x8000` as `Not in Multi Player`. This is historical documentation; we have not isolated that spawn filter in the supplied patched binary. Do not reuse. |
| `0x10000` | **Reserved (historically SP exclusion)** | Massassi lists `Not in Single Player`, consistent with the user's prior `never created` description. It was incorrectly assigned to icy friction initially and was withdrawn; do not reuse. |
| `0x1000000` | **Reserved (historically sound suppression)** | Massassi describes `Object makes no sounds`. That label has not been independently verified in the patched executable; preserve the bit. |
| `0x40000000` | **Provisional custom candidate** | OpenJKDF2 enumerates this value without any identified `SithThing.flags` consumer. A bare `0x40000000` test in the supplied executable around `0x0051F1B6` is in an unrelated numeric/runtime routine, not a proven Thing-flags check. This does not establish universal safety across mods. |
| `0x80000000` | **Occupied in reference: hide rendered Thing** | OpenJKDF2 `sithRender.c` checks `thingIter->flags & SITH_TF_80000000` and skips `sithRender_RenderThing`. This behavior has *not* been conclusively mapped to an equivalent branch in the patched JKGM exe; the highest bit is nevertheless unsafe to repurpose. |

A naive search for immediates such as `0x80000000` produces many
false positives: signed-number checks, float data, unrelated masks,
and collision-hit records can use the same number. To identify a
ThingFlag, establish data provenance back to `SithThing.flags`
(offset `0x00`) as well as an actual branch controlled by that bit.

### Current experimental icy flags

The bit allocations are **provisional** and JKGM-specific:

- `0x20` = icy Thing-face friction (0.3x)
- `0x40000000` = very icy Thing-face friction (0.1x)

The bit `0x10000` is **not used** by these hooks any more.
World-surface flags `0x1000` and `0x2000` retain native behavior.

### Remaining verification

- Build a provenance-oriented disassembly/XREF map for all
  `SithThing.flags` reads in the full patched executable and its
  loaded extension DLLs before treating `0x20` or `0x40000000`
  as universally available.
- Optionally instrument controlled changes in a sandbox level with
  single-player, multiplayer, special raycasts, and rendering.
- Validate known exclusion/no-sound meanings in the particular JKGM
  patched executable; do not conflate historical descriptions with
  new experimentally verified behavior.

**Sources:**
- [Massassi JediKnight Unofficial Specs – flag list](https://www.massassi.net/jkspecs/cog_flags.htm)
- [OpenJKDF2 `sithCollision.c`](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/Engine/sithCollision.c)
- [OpenJKDF2 `sithRender.c`](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/Engine/sithRender.c)
- [OpenJKDF2 `sithExplosion.c`](https://github.com/shinyquagsire23/OpenJKDF2/blob/master/src/World/sithExplosion.c)
