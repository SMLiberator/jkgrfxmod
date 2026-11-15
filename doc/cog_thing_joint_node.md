# GetThingJointNode

`GetThingJointNode(thing, joint)` returns the 3DO model node index associated
with a given puppet joint for the supplied Thing.

It is equivalent to `GetPuppetJointNode(GetThingPuppet(thing), joint)` in the
supplied JK.dll, including current-world and static-world puppet classes.

```c
int node = GetThingJointNode(actor, 6);
if (node >= 0)
    AttachThingToThingNode(item, actor, node, VectorSet(0, 0, 0));
```

- `thing` is a COG Thing reference, not a puppet class.
- `joint` is an index from **0 through 9**, matching JK.dll's joint mapping.
- Returns the mapped model node index, or **-1** when the Thing lacks a valid
  puppet class, the puppet class ID cannot be resolved, or the joint is out
  of range. A valid puppet class may itself store a -1 mapping.
- This is a **puppet joint-to-model node mapping**, not the current position of
  the node. Use `GetThingNodePos(thing, node)` to query its live position.

Implementation uses the native Thing's puppet-class ID and the same world
puppet-class table layout audited from the supplied JK.dll. It does not add
new puppet animation data or modify the existing JK.dll verbs.
