"""Retro CMP is the last gameplay rendering stage, including both HUD layers.

Keep the independent menu path unquantized, and keep trails' history
scene-only. This checks integration across basic and bloom compositors.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
renderer=read("renderer/renderer.cpp")
blend=read("renderer/frameblend.hpp")
opengl=read("renderer/opengl_state.hpp")
basic=read("renderer/renderer_compositor_basic.cpp")
bloom=read("renderer/renderer_compositor_bloom.cpp")
shader=read("assets/shaders/post_frame_blend.frag")

assert "std::unique_ptr<post_buffer> retro_present_buffer;" in opengl
assert "if(presenting_menu) {" in renderer
menu=renderer.split("if(presenting_menu) {",1)[1].split("SwapBuffers(hDC);",1)[0]
assert "ogs->retro_mode_active=false;" in menu
assert "ogs->menu_present_program.use_program();" in menu
assert "frame_compositor->end_frame(ogs.get(),conf_scr_res,scene);" in renderer
final=renderer.split("frame_compositor->end_frame(ogs.get(),conf_scr_res,scene);",1)[1]
assert final.index("draw_post_exempt();")<final.index("draw_hud();")
assert final.index("draw_hud();")<final.index("present_retro_after_hud();")
assert final.index("present_retro_after_hud();")<final.index("SwapBuffers(hDC);")
assert renderer.count("present_retro_after_hud();")==1
assert "retro_present_buffer->tex" in renderer
assert "retro_present_buffer->fbo" in renderer
assert "gl::disable(gl::capability::framebuffer_srgb);" in renderer
assert "gl::enable(gl::capability::framebuffer_srgb);" in renderer
assert "ogs->post_frame_blend.set_retro_palette(2,true);" in renderer
assert "ogs->post_frame_blend.set_retro_palette(2,false);" in blend
assert "ogs->retro_present_buffer->fbo" in blend
assert "ogs->post_frame_blend.set_retro_palette(2,ogs->retro_mode_active)" not in blend
assert "inline bool frame_blend_can_present_directly(opengl_state const *ogs)" in blend
assert "if(ogs->retro_mode_active)return false;" in blend
assert "if(!ogs->enable_trails)return true;" in blend
assert "blend.value()[0]<=0.0f && blend.target()[0]<=0.0f" in blend
for compositor in (basic,bloom):
    assert "bool const direct_present=frame_blend_can_present_directly(ogs);" in compositor
    assert "ogs->frame_blend_history_valid=false;" in compositor
    assert "present_frame_blend(ogs, screen_size);" in compositor
assert "palette_quantize(current.rgb)" in shader
print("Retro palette applied last over GPU and DirectDraw HUD; menus bypassed")
