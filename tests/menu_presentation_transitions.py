"""Guard GDI/DirectDraw menu presentation after a gameplay session.

JK can present its GDI menu while the renderer still reports the gameplay
display mode; filtering GdiFlush by renderer_mode broke returning to menus.
The original WaitForVerticalBlank present hook is also needed during menu
transitions, and a missing GDI DIB must not permanently freeze the GL screen.
"""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
main=(root/"renderer/main.cpp").read_text(encoding="utf-8")
renderer=(root/"renderer/renderer.cpp").read_text(encoding="utf-8")
ddraw=(root/"renderer/ddraw_impl.cpp").read_text(encoding="utf-8")

gdi=renderer.split("void present_menu_gdi() override",1)[1].split(
    "void present_menu_surface_body()",1)[0]
assert "menu_frame_pending=true;" in gdi
assert "mode != renderer_mode::menu" not in gdi
assert "wglGetCurrentContext() != hGLRC" not in gdi
assert "end_frame();" in gdi
assert "bool const presenting_menu = menu_frame_pending ||" in renderer

flush=main.split("BOOL WINAPI HookedGdiFlush()",1)[1].split(
    "using DeleteObject_type",1)[0]
assert "TrueGdiFlush()" in flush
assert "the_renderer->present_menu_gdi();" in flush
assert "LOG_DEBUG(\"CreateDIBSection hook called\");" in main

vblank=ddraw.split("WaitForVerticalBlank(DWORD a, HANDLE b)",1)[1]
assert "r->present_menu_surface_immediate();" in vblank

print("Gameplay-to-menu GDI and DirectDraw presentation contract passed")
