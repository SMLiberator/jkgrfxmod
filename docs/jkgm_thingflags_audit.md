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
| `0x80` | **Occupied: opt-in to RAYCAST_8-filtered Thing searches** | Confirmed at `0x004E86F0`–`0x004E870A`: the collision-search flags are masked with `0x8` and, only if set, `test BYTE PTR [esi],0x80` excludes candidates without ThingFlag `0x80`. The same code uses query bit `0x10` to require `SITH_TF_STANDABLE` (`0x40`). Unlike ThingFlag `0x80`, `RAYCAST_8` is a *call-specific search filter*, not an object's physical collision mode. The source reference declares `RAYCAST_8 = 0x8` and uses the check, but does not reveal a common hardcoded stock caller setting that bit. |
| `0x8000` | **Reserved (historically MP exclusion)** | Massassi's JediKnight Unofficial Specs identify `0x8000` as `Not in Multi Player`. This is historical documentation; we have not isolated that spawn filter in the supplied patched binary. Do not reuse. |
| `0x10000` | **Reserved (historically SP exclusion)** | Massassi lists `Not in Single Player`, consistent with the user's prior `never created` description. It was incorrectly assigned to icy friction initially and was withdrawn; do not reuse. |
| `0x1000000` | **Reserved (historically sound suppression)** | Massassi describes `Object makes no sounds`. That label has not been independently verified in the patched executable; preserve the bit. |
| `0x40000000` | **Provisional custom candidate** | OpenJKDF2 enumerates this value without any identified `SithThing.flags` consumer. A bare `0x40000000` test in the supplied executable around `0x0051F1B6` is in an unrelated numeric/runtime routine, not a proven Thing-flags check. This does not establish universal safety across mods. |
| `0x80000000` | **Reference-only hide check; NOT present in JKGM's native Thing-render loop** | OpenJKDF2 `sithRender.c` skips drawing a Thing when this bit is set. The actual JKGM `sithRender_RenderThings` loop at `0x004C7E84` loads `SithThing.flags`, checks `0x80012` (`0x2`, `0x10`, `0x80000`) and separately checks `0x8` at `0x004C7E91`; it does **not** test `0x80000000` before rendering. Other occurrences of the `0x80000000` immediate in JKGM arise in unrelated numeric/rasterization code; notably `0x00446B52` manipulates a framebuffer pixel line mask, NOT Thing flags. A controlled in-game test also observed no hiding. This means rendering suppression is **not a confirmed vanilla/JKGM behavior**; it is implemented in OpenJKDF2's reference path. Other possible readers of this flag have not been exhaustively excluded, so it is still an *unverified* candidate rather than proven free. |

### Why `0x80000000` does not make Things invisible in JKGM

The source-level check exists in OpenJKDF2, but the corresponding
**patched JKGM binary's native rendering path lacks it**. At
`0x004C7E84`, `SithThing.flags` is loaded from `[esi]`; the renderer
rejects bits `0x80012` and `0x8`, then continues through its rendering
path without checking `0x80000000`. There is no basis to call the
OpenJKDF2 check an original JK behavior. This conclusion concerns
**rendering specifically**: a claim that the bit is unused *everywhere*
would require a complete audit of the executable and third-party DLLs.

### How `RAYCAST_8` and ThingFlag `0x80` interact

- Search flags `& 0x8 == 0`: no ThingFlag-0x80 restriction.
- Search flags `& 0x8 != 0`: candidate Things must carry `0x80` to be
  admitted to that particular collision search.
- Search flags `& 0x10 != 0`: independent requirement that candidates
  carry ThingFlag `0x40` (standable).
- This does not mean every bullet, melee swing, movement query or AI
  query uses `RAYCAST_8`; the caller decides search flags each time.
- `RAYCAST_8` (`0x8` search bit), `SITH_TF_80` (`0x80` Thing bit), and
  `RAYCAST_80` (`0x80` *search* bit) are three distinct values/meanings.

A naive search for immediates such as `0x80000000` produces many
false positives: signed-number checks, float data, unrelated masks,
and collision-hit records can use the same number. To identify a
ThingFlag, establish data provenance back to `SithThing.flags`
(offset `0x00`) as well as an actual branch controlled by that bit.

### Current experimental icy flag

Only `0x80000000` is now reserved for **Icy Thing-face friction
(0.3x)** in the patched JKGM mod. User testing and native render-loop
inspection found no rendering-suppression behavior for this bit in
JKGM, unlike the OpenJKDF2 renderer. This is a custom reservation,
not a claim the flag was universally unused in every subsystem.

The earlier experimental assignments `0x20` and `0x40000000` have
been **released**: setting either no longer requests icy Thing-face
physics. `0x10000` also remains unused by the custom hook because
historical JK documentation identifies it as a single-player exclusion
bit. We removed the custom Very Icy ThingFlag; vanilla world-surface
flags `0x1000` and `0x2000` retain their Icy and Very Icy behavior.

In the native attached-physics code, world-face ice coefficients
`0.3` and `0.1` multiply `SurfDrag`, and values below or equal
to `0.8` prevent `StaticDrag` from being used as a speed
stopping threshold. Neither authored drag parameter is overwritten.

### Remaining verification

- Build a provenance-oriented disassembly/XREF map for all
  `SithThing.flags` reads in the full patched executable and its
  loaded extension DLLs before treating any high ThingFlag (including `0x80000000`)
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
