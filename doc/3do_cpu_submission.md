# GPU 3DO submission and mesh caching

JkGRfxMod reuses GPU-cached 3DO geometry between eligible draw passes, including the main camera and independent reflection captures.

## Submission and batching

A visible Thing's mesh groups share their prepared modelview matrix, camera-local eye, winding and GPU mesh reference. The renderer resolves and uploads a mesh only when an accepted draw needs it.

Compatible adjacent index-buffer ranges are coalesced into fewer indexed draws while preserving the exact index sequence. Transparent ranges remain depth-sorted. Opaque ranges may use `enable_gpu_3do_instancing` to batch strictly compatible repeated geometry and material states; incompatible models retain ordinary submission.

**`CloneModel` does not guarantee identical geometry.** Clones can retain the original filename after their meshes have changed. Cached geometry and instancing use mesh identity, content/revision signatures and per-material compatibility, not the filename alone.

`experimental_packed_3do_vertices` reduces eligible vertex-buffer storage by packing normals, tint and light mode; incompatible vertices retain the uncompressed layout. `experimental_auto_3do_lod` can generate fallback levels for models without suitable authored LODs.

## CPU visibility work

`experimental_multithreading` distributes eligible main-camera and reflection-capture visibility preparation across CPU workers. The original JK/COG simulation, native Thing posing, material resolution, OpenGL submission and alpha ordering remain on the render thread.

Camera and reflection frustum/portal tests reject offscreen geometry before submitting it. A conservative Thing-level frustum check can avoid expensive pose work on rejected models.

## Cache invalidation and memory

Source mesh revisions invalidate affected cached geometry when it is next used. JKGM mesh-modification verbs update mutation epochs used by both the main camera and reflection discovery. Unused cache entries can be reclaimed under memory pressure without discarding active geometry; mesh IDs remain independent of native source filenames.

For diagnostics, enable `profile_renderer` to inspect cache hits, CPU submission, source hashing, vertex packing, draw-call grouping, instancing and reflection visibility work. These counters describe work performed; compare actual frame timings when evaluating optional optimizations.
