"""GPU 3DO ordinary opaque and alpha share one visible-group traversal."""
from pathlib import Path

render=(Path(__file__).resolve().parents[1]/"renderer/renderer.cpp").read_text(encoding="utf-8")
draw=render[render.index("void draw_gpu_3do_preview("):
            render.index("// Deliberate world-cache isolation test.")]

# The ordinary alpha pass consumes prepartitioned candidates rather than
# rerunning the expensive per-group frustum and backface checks.
assert draw.count("for(auto const &group:inst.geometry->groups)") == 1
assert "bool const ordinary_pass=!post_exempt && !decals_only;" in draw
assert "bool const prepare_both=ordinary_pass && !transparent;" in draw
assert "ordinary_alpha_serial==gpu_3do_present_serial;" in draw
assert "if(!reuse_alpha) for(auto const &inst:instances)" in draw
assert "ordinary_alpha_serial=gpu_3do_present_serial;" in draw
assert "draws.swap(ordinary_alpha_draws);" in draw
assert draw.count("ordinary_alpha_serial=0;") >= 2

# Decals render between the two ordinary passes. Their scratch matrices
# must never invalidate ordinary alpha candidates that borrow modelviews.
assert "static thread_local std::vector<gpu_3do_matrix34> ordinary_modelviews;" in draw
assert "static thread_local std::vector<gpu_3do_matrix34> isolated_modelviews;" in draw
assert "auto &modelviews=ordinary_pass" in draw
assert "?ordinary_modelviews:isolated_modelviews;" in draw
positions=[render.index("draw_gpu_3do_preview(false);"),
           render.index("draw_gpu_3do_preview(true,false,true);"),
           render.index("draw_gpu_3do_preview(true);")]
assert positions == sorted(positions)

# Mixed coverage must be present in both paths, without rearranging
# transparent faces or affecting dither, decals, and special saber trails.
assert "gpu_3do_draws_in_pass(policy,false);" in draw
assert "gpu_3do_draws_in_pass(policy,true);" in draw
assert "?(opaque_eligible || alpha_eligible)" in draw
assert "if(transparent || alpha_eligible)" in draw
assert "ordinary_alpha_draws.push_back(candidate);" in draw
assert "if(prepare_both && !opaque_eligible) continue;" in draw
assert "candidate.sprite_batch_group=index+1U;" in draw
assert "if(a.depth==b.depth) return a.order<b.order;" in draw
assert "return a.depth>b.depth;" in draw
assert "gpu_3do_trail_alpha_mode(" in draw

# Pending draws borrow the correct frame-cached MAT, and deferred HUD
# continues to use its independently retained material snapshot.
assert "gpu_3do_texture const *texture;" in draw
assert "pending_draw candidate{&inst,&group,&tex,policy," in draw
assert "auto const &material=*draw.texture;" in draw
assert "? deferred_gpu_hud_material_cache" in draw
assert ": gpu_frame_material_cache;" in draw
print("GPU 3DO shared opaque/alpha collection checks passed")
