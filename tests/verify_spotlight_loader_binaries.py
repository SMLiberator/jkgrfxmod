#!/usr/bin/env python3
"""Verify the native JKGM MAT-cache preloader ABI against the audited EXE.

No third-party packages, memory hooks or writes. Uses PE section mappings
rather than assuming virtual addresses equal file offsets. A successful
signature check is a static prerequisite, not proof of safe in-game execution.
"""
import hashlib
import pathlib
import struct
import sys

AUDITED_SHA256 = "afb4605c8259075c151ad6b034b98562be87ebe90369d4d9abaee09b396ed50f"
CHECKS = {
    # rdMaterial_AddToTextureCache(pMaterial, rdTexture, mip, no_alpha)
    0x0044ACC0: "8b4c24088b44240c568b7424148b5481",
    # Alpha and opaque branches call the same 4-arg std3D uploader.
    0x0044ACF7: "e834f6fdff",
    0x0044AD3C: "e8eff5fdff",
    0x0042A330: "558bec81ec58010000538b5d08",
    # Native JK rdCache caller pushes exactly four args and cleans 16 bytes.
    0x0043B84C: "8b45b050575653e868f4000083c410",
    0x0043B90D: "8b45b050575653e8a7f3000083c410",
}


def main(filename: str) -> int:
    data = pathlib.Path(filename).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != AUDITED_SHA256:
        print("FAIL: executable hash differs from the audited JKGM build:", digest)
        return 1
    if data[:2] != b"MZ":
        print("FAIL: not an MZ executable")
        return 1
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\\0\\0":
        print("FAIL: invalid PE signature")
        return 1
    count = struct.unpack_from("<H", data, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    image_base = struct.unpack_from("<I", data, pe_offset + 24 + 28)[0]
    sections = []
    for i in range(count):
        off = pe_offset + 24 + optional_size + i * 40
        virtual_size, virtual_addr, raw_size, raw_ptr = struct.unpack_from(
            "<IIII", data, off + 8)
        sections.append((image_base + virtual_addr, virtual_size, raw_ptr, raw_size))

    def read_va(va: int, length: int) -> bytes:
        for base, size, raw, raw_size in sections:
            offset = va - base
            if 0 <= offset and offset + length <= min(size, raw_size):
                return data[raw + offset: raw + offset + length]
        raise ValueError(f"VA {va:#x} is not in a file-backed section")

    for addr, hexbytes in CHECKS.items():
        expected = bytes.fromhex(hexbytes)
        found = read_va(addr, len(expected))
        if found != expected:
            print(f"FAIL: {addr:#010x} bytes: got {found.hex()} expected {hexbytes}")
            return 1
        print(f"OK: {addr:#010x} ({len(expected)} bytes)")
    bad = read_va(0x0044AA70, 3)
    if bad != bytes.fromhex("5d5bc3"):
        print("FAIL: prior crashing address changed unexpectedly")
        return 1
    print("PASS: JKGM preloader ABI and both native callsites verified.")
    print("Note: live renderer/Wine testing is still required.")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: verify_spotlight_loader_binaries.py /path/to/jkgm.exe")
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
