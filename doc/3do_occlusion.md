# GPU 3DO and sprite occlusion culling

`experimental_3do_occlusion_culling` enables **asynchronous** main-camera GPU queries against completed opaque depth. Hidden results can skip eligible GPU-cached models and ordinary sprite quads during subsequent frames. The default is `false`.

Queries use the authored Thing/model radius for 3DO bounds. Ordinary sprites submit their real cached quad. Neither the Thing's simulation nor COG logic is stopped when a draw is culled.

## Settings

| Option | Behavior |
| --- | --- |
| `experimental_3do_occlusion_culling` | Enable GPU model and ordinary sprite occlusion queries. |
| `experimental_3do_native_occlusion_culling` | Also allow native model early-return when a recent, consistent GPU result proves it hidden. |
| `gpu_3do_occlusion_proxy_mode` | `0` = 3D box; `1` = nearest-depth screen rectangle; `2` = center-depth screen rectangle. |
| `gpu_3do_occlusion_query_budget` | Maximum new queries per frame (`128` by default; `0` = unlimited). |
| `gpu_3do_occlusion_min_triangles` | Minimum model triangles to query (`0` includes small models); sprites are exempt. |
| `gpu_3do_occlusion_debug_view` | `0` = off; `1` = wireframe; `2` = filled transparent query proxies. |

All results are polled without blocking for GPU completion. Records are invalidated or rechecked when their world, camera, Thing identity, bounds or geometry changes. Recently hidden Things are retested so moving doors or occluders can reveal them again.

Camera HUD, skybox Things, projected decals and unsupported fallback geometry are excluded where appropriate. Shadows and reflection capture continue to use their own visibility rules. An invalid or uncertain query result renders the Thing normally.

The native early-return option is a separate, opt-in optimization. It requires the GPU query option and does not disable any game's engine simulation.

`profile_renderer` reports query submissions, result polling, hidden skips and relevant CPU/GPU costs. The debug view shows the proxies that were actually tested.
