# GPU-cached animated explosion sprites

JK's native `sithExplosion_CreateThing` handles explosion Things with
`typeflags & 0x1` by scheduling a MAT-cel animation with a rate of
`num_texinfo / lifetime_seconds`. The initial lifetime comes from the
Thing's `timer` template property and is copied into
`explosionParams.msecLifeLeft`; native `sithSurface_Tick` writes
the current cel to the *individual* `rdThing.wallCel`.

GPU-cached ordinary sprites already use per-Thing `wallCel` and
per-cel immutable quad variants. For flagged explosion sprites, the
renderer now additionally handles the case where the native cel remains
zero: it obtains the original and remaining lifetime from the native
Thing and derives the cel using the same integer-millisecond interval
as JK (initial milliseconds / number of material cels). Once JK's
`wallCel` becomes nonzero, the native value takes priority.

This is **not** a second animation timer and does not use real-world
time or the renderer frame counter. It is computed only from current
native Thing state and does not consume animation slots.

A MAT cel not resident in the engine's texture cache still needs to
be warmed by JK. The existing native sprite fallback is retained, and
for animated explosions its one-call native `wallCel` is temporarily
set to the same requested cel so the correct texture can upload.
The field is restored immediately after native submission; native
animation state and timing remain untouched across frames.

Other sprite types, projected decals, and model/world cel animation
retain their existing paths. This feature does not change the
`use_sprite_cache` configuration default.
