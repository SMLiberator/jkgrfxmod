"""Source guards for Camera-type 3DO alpha compositing stability."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
renderer = (root / "renderer/renderer.cpp").read_text(encoding="utf-8")
hud = (root / "renderer/camera_hud_thing.hpp").read_text(encoding="utf-8")
frag = (root / "assets/shaders/gpu_3do_preview.frag").read_text(encoding="utf-8")

# The post-FX Camera render remains a single premultiplied alpha pass.
assert "draw_gpu_3do_preview(true,true);" in renderer
assert "gl::disable(gl::capability::depth_test);" in renderer
assert "gl::set_depth_mask(false);" in renderer
assert "if(transparent && post_exempt) {" in renderer

# Do not sort nearly coincident overlay layers by their camera depth.
# The config option is restricted to 0x20 world-depth UI instances.
assert "a_world_depth!=b_world_depth" in hud
assert "if(a_world_depth && sort_world_depth && a_depth!=b_depth)" in hud
assert "return a_order<b_order;" in hud
assert "camera_hud_draw_precedes(" in renderer
assert "bool const sort_world_depth=the_config->camera_hud_depth_sort;" in renderer
assert "float const wx=post_exempt ? m[9] :" in renderer

# GL clips close HUD models; unstable native CPU bounds and face-normal
# rejections must never make their alpha layers appear/disappear framewise.
assert "if(!inst.camera_hud && have_perspective_frustum &&" in renderer
assert "if(!inst.camera_hud && can_cpu_cull &&" in renderer

# World-depth HUDs still compare against the preserved opaque scene depth.
assert "post_exempt_pass!=0 && camera_hud_thing==2" in frag
assert "texelFetch(scene_depth,ivec2(gl_FragCoord.xy),0).r" in frag

print("Camera HUD alpha pass stability source guards passed")
