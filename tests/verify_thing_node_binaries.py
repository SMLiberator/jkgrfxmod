#!/usr/bin/env python3
"""Read-only JKGM/JK.dll compatibility check for native ThingNode COG getters.

Usage: python tests/verify_thing_node_binaries.py path/to/jkgm.exe path/to/JK.dll
No external packages or executable modification are required.
"""

import argparse
import hashlib
from pathlib import Path
import struct


class PE:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 0x100 or self.data[:2] != b"MZ":
            raise ValueError(f"{path}: not an MZ executable")
        pe = self.u32(0x3C)
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError(f"{path}: missing PE signature")
        machine, count, _, _, _, optional_size, _ = struct.unpack_from(
            "<HHIIIHH", self.data, pe + 4
        )
        optional = pe + 24
        if machine != 0x14C or self.u16(optional) != 0x10B:
            raise ValueError(f"{path}: expected an x86 PE32 image")
        self.image_base = self.u32(optional + 28)
        self.export_rva = self.u32(optional + 96)
        self.sections = []
        section_start = optional + optional_size
        for i in range(count):
            off = section_start + i * 40
            name = self.data[off:off + 8].split(b"\0")[0].decode("ascii", "replace")
            virtual_size, rva, raw_size, raw_ptr = struct.unpack_from(
                "<IIII", self.data, off + 8
            )
            self.sections.append((name, rva, virtual_size, raw_ptr, raw_size))

    def u16(self, off):
        return struct.unpack_from("<H", self.data, off)[0]

    def u32(self, off):
        return struct.unpack_from("<I", self.data, off)[0]

    def rva_offset(self, rva, length=1):
        for name, base, virtual_size, raw_ptr, raw_size in self.sections:
            relative = rva - base
            if relative >= 0 and relative + length <= raw_size and relative < max(virtual_size, raw_size):
                return raw_ptr + relative
        raise ValueError(f"{self.path}: RVA 0x{rva:X} is not file-backed")

    def va_bytes(self, va, length):
        return self.data[self.rva_offset(va - self.image_base, length):][:length]

    def cstring(self, rva):
        offset = self.rva_offset(rva)
        end = self.data.index(0, offset)
        return self.data[offset:end].decode("ascii")

    def exports(self):
        if not self.export_rva:
            return {}
        ed = self.rva_offset(self.export_rva, 40)
        ordinal_base = self.u32(ed + 16)
        func_count = self.u32(ed + 20)
        name_count = self.u32(ed + 24)
        funcs = self.u32(ed + 28)
        names = self.u32(ed + 32)
        ordinals = self.u32(ed + 36)
        results = {}
        for i in range(name_count):
            name_rva = self.u32(self.rva_offset(names + i * 4, 4))
            ordinal_index = self.u16(self.rva_offset(ordinals + i * 2, 2))
            if ordinal_index >= func_count:
                raise ValueError("Malformed export table")
            fn_rva = self.u32(self.rva_offset(funcs + ordinal_index * 4, 4))
            results[self.cstring(name_rva)] = (ordinal_base + ordinal_index, fn_rva)
        return results


def check_game(exe):
    # These are the exact prologues used by renderer/thing_node_native.hpp.
    signatures = {
        "rdModel3_GetMeshMatrix": (0x00445FD0, bytes.fromhex(
            "56 8b 74 24 08 57 8b 7c 24 14 8b 46 04 3b 78 54")),
        "rdMatrix_ExtractAngles34": (0x00440950, bytes.fromhex(
            "83 ec 20 8b 44 24 24 56 8b 48 0c 8b 50 10 89 4c")),
    }
    if exe.image_base != 0x400000:
        raise ValueError(f"Unexpected game image base 0x{exe.image_base:X}")
    for label, (address, expected) in signatures.items():
        actual = exe.va_bytes(address, len(expected))
        if actual != expected:
            raise ValueError(f"{label} at 0x{address:X}: signature mismatch")
        print(f"  PASS {label} at 0x{address:X}")

    # Confirm the getter checks node count, frame state, and calls the
    # pose-refresh routine, not just a lookalike prologue.
    body = exe.va_bytes(0x00445FD0, 0x50)
    if (bytes.fromhex("3b 78 54") not in body or
            bytes.fromhex("a1 d8 03 57 00") not in body or
            bytes.fromhex("e8 62 91 ff ff") not in body or
            bytes.fromhex("b9 0c 00 00 00") not in body):
        raise ValueError("Native node accessor body no longer matches the audited function")
    print("  PASS hierarchy count, frame counter, puppet refresh, 12-float copy")

    # Separate identification of the JKGM collision routine. These checks
    # deliberately do NOT authorize a runtime detour: callers, ABI and
    # output collision records require independent verification.
    collision_va = 0x00508360
    collision_end = 0x005085FA
    collision = exe.va_bytes(collision_va, collision_end - collision_va)
    signature = bytes.fromhex("83 ec 4c 8b 44 24 68 53 55 56 57 8b 7c 24 74 a8 80")
    if not collision.startswith(signature) or collision[-1] != 0xC3:
        raise ValueError("JKGM collision entry/body does not match the audited build")
    for label, pattern in (
        ("face collision compare", bytes.fromhex("83 7f 28 03")),
        ("checked collider size", bytes.fromhex("8b 57 30")),
        ("initial sphere test call", bytes.fromhex("e8 05 04 00 00")),
        ("per-mesh intersection call", bytes.fromhex("e8 0c 01 00 00")),
    ):
        if pattern not in collision:
            raise ValueError(f"JKGM collision body changed: {label}")
    print("  PASS likely sphere/face collision entry at 0x508360 (no live hook)")


def check_patch(dll):
    required = {"InitNewCogVerbs": 1, "InitNewCogMessages": 12}
    exports = dll.exports()
    for name, expected_ordinal in required.items():
        ordinal, fn_rva = exports.get(name, (None, None))
        if ordinal != expected_ordinal or not fn_rva:
            raise ValueError(f"JK.dll: expected {name} at ordinal {expected_ordinal}")
        print(f"  PASS {name} export #{ordinal}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="Patched jkgm.exe")
    parser.add_argument("patch", type=Path, help="Community-patch JK.dll")
    args = parser.parse_args()
    for path in (args.game, args.patch):
        pe = PE(path)
        print(f"{path.name}: sha256 {hashlib.sha256(pe.data).hexdigest()}")
        if path == args.game:
            check_game(pe)
        else:
            check_patch(pe)
    print("PASS: binaries match the audited ThingNode and collision entry prerequisites")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, IndexError, struct.error, OSError) as exc:
        raise SystemExit(f"FAIL: {exc}")
