# Individual current-camera RLU COG getters

The original JK.dll verb `GetCurrentCameraRLUVecs()` pushes three
world-space vectors from the **current logical SithCamera**, in
U, L, R stack order. These new verbs return the exact same data
**one vector per call**, without calculating angles or rebuilding the matrix.

| COG verb | World-space orientation vector |
| --- | --- |
| `GetCurrentCameraRVec()` | Right (+R), SithCamera.orient.rvec |
| `GetCurrentCameraLVec()` | Look/forward (+L), SithCamera.orient.lvec |
| `GetCurrentCameraUVec()` | Up (+U), SithCamera.orient.uvec |

```c
vector look;
look = GetCurrentCameraLVec();
// Can be used as a forward offset / direction.
```

They read the current logical camera (the same source as
`GetCurrentCameraRLUVecs`), **not** the separate active rdCamera
used by rendering passes and camera-relative depth calculations. On rare
temporary camera overrides these two may differ. Invalid camera index,
unreadable memory, or nonfinite data return `(0, 0, 0)`.

The implementation requires no new native hook or configuration flag.
