"""Repository inventory, VS project/header parity, config docs, and CI hygiene.

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

sample = json.loads((root / "assets" / "jkgm.json").read_text(encoding="utf-8"))
cfg = (root / "common" / "config.hpp").read_text(encoding="utf-8")
docs = (root / "doc" / "config.md").read_text(encoding="utf-8")
missing_fields = [name for name in sample if not re.search(
    rf"\b{re.escape(name)}\b", cfg)]
assert not missing_fields, f"sample keys missing from config struct: {missing_fields}"
missing_docs = [name for name in sample if not re.search(
    rf"\b{re.escape(name)}\b", docs)]
assert not missing_docs, f"sample keys undocumented: {missing_docs}"

# Keep the documented public configuration in sync with the real config class.
# A mention in explanatory prose is not sufficient: every active option must
# have exactly one entry in the primary or supplementary reference tables.
config_body = cfg.split("class config {", 1)[1].split("\n    };", 1)[0]
config_fields = set(re.findall(
    r"^\s*(?:std::(?:optional|tuple)<[^;\n]+>|std::string|uint64_t|"
    r"unsigned int|bool|float|double|int)\s+(\w+)\s*(?:=|;)",
    config_body, re.MULTILINE))
rows = re.findall(
    r"^\|\s*(?:\*\*(\w+)\*\*|`(\w+)`)\s*\|",
    docs, re.MULTILINE)
documented = [bold or code for bold, code in rows]
duplicates = sorted({key for key in documented if documented.count(key) > 1})
assert not duplicates, f"duplicate configuration reference rows: {duplicates}"
missing_rows = sorted(config_fields - set(documented))
stale_rows = sorted(set(documented) - config_fields)
assert not missing_rows, f"active config fields missing from tables: {missing_rows}"
assert not stale_rows, f"obsolete config options still documented as active: {stale_rows}"

# A single build job should not run the same test more than once. Tests in
# the independent cleanup source-contract job remain intentionally separate.
ci = (root / ".github" / "workflows" / "main.yml").read_text(encoding="utf-8")
build_job = ci.split("\n  source-contracts:", 1)[0]
invoked = re.findall(r"tests[/\\]+([\w-]+\.(?:cpp|py))\b", build_job)
repeated = sorted({name for name in invoked if invoked.count(name) > 1})
assert not repeated, f"duplicate CI test invocations: {repeated}"

print(f"Repository structure OK: {len(sources)} compiled units, {len(headers)} headers, {len(sample)} sample keys, {len(config_fields)} documented config fields")
