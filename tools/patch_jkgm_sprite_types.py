#!/usr/bin/env python3
"""Enable extended .SPR type bits in the inspected, SHA-locked JK23 executable.

Only changes the immediate in sithSprite_Load's cmp edi,2 at 0x004F27BA to
63. This does not implement the new sprite rendering modes; renderer.dll must
also interpret the extended types. Keeps the original EXE untouched.
"""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

ORIGINAL_SHA256 = "220f150ec14f1d2de35125d5f345ba69fff2ff43cfe474e8f18b97577b1d556b"
PATCHED_SHA256 = "bf782538f665da0d34231d77382827851a4210da0af2eb74d7958645bc814803"
SPR_TYPE_CHECK_VA = 0x004F27BA
BEFORE = bytes.fromhex("83 FF 02 0F 87 83 00 00 00")
AFTER = bytes.fromhex("83 FF 3F 0F 87 83 00 00 00")


def va_to_offset(blob: bytes, address: int) -> int:
    if blob[:2] != b"MZ" or len(blob) < 256:
        raise ValueError("Not a PE executable")
    pe = struct.unpack_from("<I", blob, 0x3C)[0]
    if pe + 24 >= len(blob) or blob[pe:pe+4] != b"PE\0\0":
        raise ValueError("Invalid PE header")
    machine, section_count = struct.unpack_from("<HH", blob, pe + 4)
    optional_size = struct.unpack_from("<H", blob, pe + 20)[0]
    optional = pe + 24
    if machine != 0x14C or struct.unpack_from("<H", blob, optional)[0] != 0x10B:
        raise ValueError("Expected a 32-bit x86 PE")
    base = struct.unpack_from("<I", blob, optional + 28)[0]
    rva = address - base
    for i in range(section_count):
        off = optional + optional_size + i*40
        if off+40 > len(blob):
            break
        vsize, vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII", blob, off+8)
        if vaddr <= rva < vaddr + min(vsize, raw_size):
            target = raw_ptr + rva - vaddr
            if target + len(BEFORE) <= len(blob):
                return target
    raise ValueError(f"VA {address:#x} is not in a mapped code section")


def patch_bytes(blob: bytes) -> tuple[bytes, bool]:
    checksum = hashlib.sha256(blob).hexdigest()
    off = va_to_offset(blob, SPR_TYPE_CHECK_VA)
    original = blob[off:off + len(BEFORE)]
    if original == AFTER and checksum == PATCHED_SHA256:
        return blob, False
    if original != BEFORE or checksum != ORIGINAL_SHA256:
        raise ValueError("Unexpected SPR instructions or unsupported executable checksum")
    patched = bytearray(blob)
    patched[off:off + len(BEFORE)] = AFTER
    output = bytes(patched)
    if hashlib.sha256(output).hexdigest() != PATCHED_SHA256:
        raise ValueError("Patched executable checksum mismatch")
    return output, True


def main() -> None:
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("source", type=Path, help="Original patched JK23/jkgm.exe")
    cli.add_argument("destination", type=Path, help="Separate output EXE")
    args = cli.parse_args()
    if args.source.resolve() == args.destination.resolve():
        cli.error("Refusing to overwrite the source EXE")
    try:
        output, changed = patch_bytes(args.source.read_bytes())
    except ValueError as e:
        cli.error(str(e))
    args.destination.write_bytes(output)
    print(f"{'Patched' if changed else 'Already patched'}: {args.destination}")
    print("SPR types up to 63 now load; extended rendering requires a supporting renderer.dll.")


if __name__ == "__main__":
    main()
