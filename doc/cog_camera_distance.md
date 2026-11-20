# Camera-relative Thing COG verbs

All three verbs accept a **Thing** and return a floating-point value in JK
world units (squared units for `DistSq`). They query the native active
`rdCamera` view matrix, matching JK's camera-relative projection, rather
than using `SithThing.transformedPos`, which is cached during rendering and
can be stale for invisible Things or COG callbacks before a render.

| Verb | Meaning | Cost |
| --- | --- | --- |
| `GetThingCameraDepth(thing)` | **Signed camera-forward depth**, i.e. native view-space Y; negative behind the camera | No square root |
| `GetThingCameraDistSq(thing)` | Squared true 3D distance from the camera to Thing.position | No square root |
| `GetThingCameraDist(thing)` | True 3D distance from the camera to Thing.position | One square root |

```c
// Efficient approximate screen-size/distance policy:
float depth = GetThingCameraDepth(effect);
if (depth > 0.0 && depth < 30.0) {
    // In front of the camera and closer than 30 forward-world-units.
}

// Euclidean proximity threshold, including off-axis Things:
if (GetThingCameraDistSq(effect) < 30.0 * 30.0) {
    // Within a 30-world-unit radius of the camera.
}

float exactDistance = GetThingCameraDist(effect);
```

**Important:** depth is not Euclidean distance; an object far off to the
side may have small depth. Negative depth indicates a Thing behind the camera,
not an error. Queries return `-1.0` when the Thing or native camera is not
available/valid. Thus exactly `-1.0` is ambiguous for signed depth; use the
distance-squared variant to distinguish a missing camera/Thing if necessary.

No per-Thing render-frame dependency, new native detour or cache is required.
The implementation uses the existing COG interpreter pop/push helpers and
checks the tested game executable before reading native camera memory.
