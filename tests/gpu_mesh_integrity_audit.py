"""Regression checks: mesh integrity hashing is an opt-in timed diagnostic."""
from pathlib import Path
import json

root = Path(__file__).resolve().parents[1]
main = (root / "renderer/main.cpp").read_text(encoding="utf-8")
cfg_h = (root / "common/config.hpp").read_text(encoding="utf-8")
cfg_cpp = (root / "common/config.cpp").read_text(encoding="utf-8")
revision = (root / "renderer/native_mesh_revision.cpp").read_text(encoding="utf-8")
sample = json.loads((root / "assets/jkgm.json").read_text(encoding="utf-8"))

assert sample["gpu_mesh_integrity_audit_interval"] == 0.0
assert "gpu_mesh_integrity_audit_interval = 0.0;" in cfg_h
assert 'j.contains("gpu_mesh_integrity_audit_interval")' in cfg_cpp
assert "gpu_frame_serial%32U" not in main
assert "gpu_mesh_integrity_audit_frame==gpu_frame_serial" in main
assert "gpu_mesh_integrity_audit_interval>0.0" in main
assert "gpu_mesh_next_audit_ms" in main
assert "GPU 3DO integrity audit complete:" in main
assert "UNTRACKED source change:" in main
assert "gpu_3do_source_detail(m)" in main
assert "mesh_revision::report_untracked" not in main
assert "periodic signature audit every 32 frames" not in revision
assert "bool active() {return complete;}" in revision
print("Opt-in GPU mesh integrity audit wiring verified")
