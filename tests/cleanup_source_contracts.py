"""Run Python-only source-contract checks during the cleanup refactor.

The main Windows CI covers build and selected C++ regression tests. This
additional suite exposes old source-contract tests that were not being run;
it never invokes tools requiring game binaries or native Windows compilers.
"""
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
cases=(
    "auto_exposure_cleanup.py",
    "camera_hud_emissive.py",
    "gpu_cache_configuration.py",
    "gpu_cmp_integration.py",
    "gpu_default_specular.py",
    "gpu_explosion_animation.py",
    "gpu_material_shared.py",
    "gpu_pixel_lighting.py",
    "gpu_procedural_integration.py",
    "gpu_sector_crossing.py",
    "native_game_shader_shared.py",
    "native_mesh_revision_wiring.py",
    "native_sector_color_hook_wiring.py",
    "native_mat_animation.py",
    "native_mat_warmup_latency.py",
    "native_mesh_fallback_guard.py",
    "postfx_rgb_split.py",
    "retro_final_presentation.py",
    "spotlight_sun_shadow_independence.py",
    "sprite_decal_geoset_integration.py",
    "sprite_lighting_modes.py",
    "sun_shadow_standard_optimizations.py",
    "world_cache_direct_specular.py",
)
failed=[]
for case in cases:
    print(f"\n===== tests/{case} =====",flush=True)
    status=subprocess.run([sys.executable,str(root/"tests"/case)],
                          cwd=root,check=False).returncode
    if status:
        failed.append(case)
print(f"\nSource-contract checks: {len(cases)-len(failed)}/{len(cases)} passed.")
if failed:
    print("FAILED: "+", ".join(failed))
    raise SystemExit(1)
