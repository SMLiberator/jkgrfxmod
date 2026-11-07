"""Repository inventory, VS project/header parity, and sample config coverage.

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

headers = {p.name for p in (root / "renderer").glob("*.hpp")}
project_headers = entries(proj, "ClInclude")
filter_headers = entries(filters, "ClInclude")
assert len(project_headers) == len(set(project_headers)), "duplicate project header"
assert len(filter_headers) == len(set(filter_headers)), "duplicate filter header"
assert headers == set(project_headers), f"project header mismatch: {headers ^ set(project_headers)}"
assert headers == set(filter_headers), f"filter header mismatch: {headers ^ set(filter_headers)}"

sample = json.loads((root / "assets" / "jkgm.json").read_text(encoding="utf-8"))
cfg = (root / "common" / "config.hpp").read_text(encoding="utf-8")
docs = (root / "doc" / "config.md").read_text(encoding="utf-8")
missing_fields = [name for name in sample if not re.search(
    rf"\b{re.escape(name)}\b", cfg)]
assert not missing_fields, f"sample keys missing from config struct: {missing_fields}"
missing_docs = [name for name in sample if not re.search(
    rf"\b{re.escape(name)}\b", docs)]
assert not missing_docs, f"sample keys undocumented: {missing_docs}"
print(f"Repository structure OK: {len(headers)} headers, {len(sample)} sample config keys")
