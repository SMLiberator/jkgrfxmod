# Opt-in JkGR COG symbol declaration extensions

Installed by renderer.dll on the **tested** jkgm(5).exe only (SHA-256 begins `afb4605c`). Not part of the vanilla COG language. Every hook validates native instructions before installation; unsupported versions retain their original parsing behavior.

Inside a COG `symbols` section, **no semicolons**:

```c
symbols
vector direction=(0/1/1) local
vector JklOverride=(0/0/1)
sprite smoke=smoke.spr local
soundclass footsteps=ky.snd local
puppet animation=ky.pup local
end
```

- `vector`: initializer uses the native `(x/y/z)` format. Local symbols keep the declared vector; nonlocal declarations supply a default that a JKL-supplied value can override.
- `sprite`, `soundclass`, `puppet`: ordinary integer resource references, resolved through the game resource loaders. Their `local` initializers are resolved at COG activation; nonlocal symbols can receive values from JKL COG parameters instead. Invalid resources become `-1`.
- `puppet` means the **.PUP animation class**, not the per-Thing running animation object.
- Existing `model`, `sound`, `keyframe`, `material`, `thing`, and other declarations remain unchanged. Scripts without these new declarations follow the native parser.
- Strings are intentionally out of scope.

These changes do not extend SendMessageEx or change the COG interpreter's value representation. Native COG initialization argument storage retains its existing **31-character maximum plus terminator**.

**Runtime testing required**: nonlocal vs local precedence, resources from both current/static worlds, invalid/missing resources, and save/load. Keep a working binary artifact for rollback until validated.
