# Native gameplay and asset extensions

These additions run in the supported patched JK23/JKGM environment; they do not change the base JkGfxMod material-pack workflow.

## SPR variants and rendering

A `.spr` may contain several ordinary eleven-field SPR definitions, one per line. When a Thing is created, a random valid entry can be selected for that Thing; the selection remains fixed for the Thing's lifetime. Each entry may use a different MAT, dimensions, sprite type or placement offset.

Extended sprite types preserve selected orientation axes; compatible bits include `0x04` (Up), `0x08` (Look) and `0x10` (Right). The supplied sprite-type patcher validates a supported game executable before extending the accepted type range.

`SetSpriteInfo`, `GetSpriteInfo`, `SetSpriteRoll` and `GetSpriteRoll` control size, opacity and roll on individual sprite Things without editing their shared SPR resource.

## Projected SPR decals

With `enable_decals=true` (default), SPR type 3 can project clipped geometry onto nearby **world architecture**. The sprite's Up vector defines the projection direction (`-Up`). Attached decals can use the attached surface normal to align with the receiving world face.

Projection uses cached world geometry and respects material opacity and sprite scale/rotation. If no valid world receiver exists, the sprite falls back to ordinary drawing. Animated 3DO receiving meshes are not supported by this world-decal path.

## Optional 3DO mesh collision

`enable_mesh_collision=true` opts into patched `collide=2` 3DO mesh/sphere-tree collision. The hook uses current animated node transforms and cached per-mesh geometric bounds. Unsupported collision cases retain native fallback behavior. This is independent of GPU caching and is disabled by default.

## Face attachments and slippery 3DOs

`GetThingAttachNormal(thing)` returns the world-space normal of an attached world or Thing face. A zero vector indicates that no valid face attachment is present.

A standable 3DO Thing may set ThingFlag `0x80000000` to apply icy friction to physics Things attached to its faces:

```ini
thingflags=0x80000040
collide=3
```

The flag uses the Icy friction coefficient while face-attached. It does not change ordinary Thing-to-Thing attachments or world-surface friction settings.

## Earth impact sound

On the supported executable, impacts on eligible Earth surfaces use the `hitearth` SoundClass response while retaining the normal priority of metal impacts and existing collision callbacks. This does not add a separate scraping sound system.
