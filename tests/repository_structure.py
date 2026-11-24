"""Repository inventory, VS project/header parity, sample config and CI hygiene.

This is an organizational test: it intentionally does not assert formatting of
shader/C++ functions, and runs on both development and cleanup CI.
"""
from pathlib import Path
import json
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
proj = root / "renderer" / "renderer.vcxproj"
filters = root / "renderer" / "renderer.vcxproj.filters"
ns = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}

def entries(path: Path, kind: str) -> list[str]:
    tree = ET.parse(path)
    return [elem.attrib["Include"] for elem in tree.findall(
        f".//ms:{kind}", ns) if "Include" in elem.attrib]

# Every translation unit in renderer/ must be part of the Release build.
# A successful build alone does not catch a newly extracted, unlisted .cpp.
sources = {p.name for p in (root / "renderer").glob("*.cpp")}
project_sources = entries(proj, "ClCompile")
filter_sources = entries(filters, "ClCompile")
assert len(project_sources) == len(set(project_sources)), "duplicate project source"
assert len(filter_sources) == len(set(filter_sources)), "duplicate filter source"
assert sources == set(project_sources), f"project source mismatch: {sources ^ set(project_sources)}"
assert sources == set(filter_sources), f"filter source mismatch: {sources ^ set(filter_sources)}"

headers = {p.name for p in (root / "renderer").glob("*.hpp")}
project_headers = entries(proj, "ClInclude")
filter_headers = entries(filters, "ClInclude")
assert len(project_headers) == len(set(project_headers)), "duplicate project header"
assert len(filter_headers) == len(set(filter_headers)), "duplicate filter header"
assert headers == set(project_headers), f"project header mismatch: {headers ^ set(project_headers)}"
assert headers == set(filter_headers), f"filter header mismatch: {headers ^ set(filter_headers)}"

# A sample JSON configuration must be valid and refer only to known keys.
# The documentation intentionally covers JkGRfxMod additions/changes, not
# every inherited JkGfxMod option or a precise source-text layout.
sample = json.loads((root / "assets" / "jkgm.json").read_text(encoding="utf-8"))
cfg = (root / "common" / "config.hpp").read_text(encoding="utf-8")
unknown = [key for key in sample if not re.search(
    rf"\b{re.escape(key)}\b", cfg)]
assert not unknown, f"unknown sample configuration keys: {unknown}"

# Avoid accidentally running the same standalone test twice in CI.
ci = (root / ".github" / "workflows" / "main.yml").read_text(encoding="utf-8")
build_job = ci.split("\n  prune-old-runs:", 1)[0]
invoked = re.findall(r"tests[/\\]+([\w-]+\.(?:cpp|py))\b", build_job)
repeated = sorted({name for name in invoked if invoked.count(name) > 1})
assert not repeated, f"duplicate CI test invocations: {repeated}"

print(f"Repository structure OK: {len(sources)} compiled units, {len(headers)} headers, {len(sample)} sample keys")
