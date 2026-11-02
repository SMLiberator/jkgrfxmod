# Verified JKGM SithWorld sky parameter layout

The GPU world-cache sky path accesses the **same SithWorld structure**
that JK's world loader fills, not the local OpenJKDF2 executable's
globals or function entry points.

The actual user-supplied `jkgm(5).exe` is PE32/i386 (image base
`0x00400000`). Its JKL section parser at **`0x004D05B0`**
contains the sky format strings and loads each sky value directly into
`esi` (the parsed `SithWorld *`). Its write-address instructions are:

| Native code VA | Instruction | Parsed JKL setting |
|---|---|---|
| `0x004D063B` | `lea edx,[esi+0xDC]` | `ceiling sky z %f` |
| `0x004D0663` | `lea ecx,[esi+0xE0]` | `horizon distance %f` |
| `0x004D068B` | `lea eax,[esi+0xE4]` | `horizon pixels per rev %f` |
| `0x004D06B9` | `lea eax,[esi+0xE8]` | `horizon sky offset` X |
| `0x004D06B3` | `lea edx,[esi+0xEC]` | `horizon sky offset` Y |
| `0x004D06E8` | `lea eax,[esi+0xF0]` | `ceiling sky offset` X |
| `0x004D06E2` | `lea edx,[esi+0xF4]` | `ceiling sky offset` Y |

For the 2D offset pairs, the parser pushes the Y address before X,
consistent with x86 reverse-order function arguments.

Thus seven consecutive float values comprise `+0xDC..+0xF7` (28
bytes). The offsets used by `renderer/world_cache_sky.hpp` are
**confirmed for this executable**. This verification does **not** imply
the OpenJKDF2 rendering function addresses are interchangeable with
the patched JK executable.

**Still to verify in gameplay:** whether horizon UV projection and
ceiling-sky projection in our GLSL match JK's visual output. The
`WORLD CACHE gpu_sky` log reports live sky parameters and draw counts
for that comparison.
