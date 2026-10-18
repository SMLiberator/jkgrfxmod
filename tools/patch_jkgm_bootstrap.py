#!/usr/bin/env python3
"""
Patch the user's known JK 2.3 RGB/smooth-normal executable to load renderer.dll
before its original entry point. No external packages or injection program.

The EXE already imports kernel32!LoadLibraryA and JK.dll. We add a tiny PE
section, change AddressOfEntryPoint, and leave every existing section intact.
Loading renderer.dll is optional at OS level (NULL => continue original JK).
The DLL itself checks use_vanilla_renderer in jkgm.json before installing hooks.

WARNING: This patch is ONLY for the precisely identified user-supplied build.
Other versions require separate address/signature analysis. No binary is
distributed with this script.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys

ORIGINAL_SHA256 = "1610d070112b7f3a78668ede5deab9aeebc758e83ec4c69aa705c2e91f64a0da"
ORIGINAL_ENTRY_RVA = 0x00113CC0
LOAD_LIBRARY_A_IAT_VA = 0x008F0598
IMAGE_BASE = 0x00400000
SECTION_NAME = b".jkgm"
DLL_NAME = b"renderer.dll\x00"


def u16(blob: bytes | bytearray, index: int) -> int:
    return struct.unpack_from("<H", blob, index)[0]


def u32(blob: bytes | bytearray, index: int) -> int:
    return struct.unpack_from("<I", blob, index)[0]


def put16(blob: bytearray, index: int, value: int) -> None:
    struct.pack_into("<H", blob, index, value)


def put32(blob: bytearray, index: int, value: int) -> None:
    struct.pack_into("<I", blob, index, value)


def align(n: int, alignment: int) -> int:
    if not alignment or alignment & (alignment - 1):
        raise ValueError("Alignment must be a power of two")
    return (n + alignment - 1) & ~(alignment - 1)


def patch_executable(source: bytes) -> bytes:
    digest = hashlib.sha256(source).hexdigest()
    if digest != ORIGINAL_SHA256:
        raise ValueError(
            "Wrong executable build or already patched: SHA256 " + digest +
            " (expected " + ORIGINAL_SHA256 + ")"
        )
    data = bytearray(source)
    if data[:2] != b"MZ":
        raise ValueError("Not a DOS/PE executable")
    pe = u32(data, 0x3C)
    if pe + 0x100 > len(data) or data[pe:pe + 4] != b"PE\x00\x00":
        raise ValueError("Missing PE header")
    coff = pe + 4
    section_count = u16(data, coff + 2)
    optional = coff + 20
    optional_size = u16(data, coff + 16)
    sections_at = optional + optional_size
    if u16(data, coff) != 0x014C or u16(data, optional) != 0x10B:
        raise ValueError("Only PE32 x86 is supported")
    if u32(data, optional + 0x1C) != IMAGE_BASE:
        raise ValueError("Unexpected image base")
    if u32(data, optional + 0x10) != ORIGINAL_ENTRY_RVA:
        raise ValueError("Unexpected original entrypoint")
    section_alignment = u32(data, optional + 0x20)
    file_alignment = u32(data, optional + 0x24)
    size_of_headers = u32(data, optional + 0x3C)
    if sections_at + (section_count + 1) * 40 > size_of_headers:
        raise ValueError("No section-header room")
    if section_count != 7 or section_alignment != 0x1000 or file_alignment != 0x200:
        raise ValueError("Unexpected section layout")
    # Validate the old import table and IAT. VA 0x8F0598 is LoadLibraryA
    # in THIS specific binary; it is resolved by the Windows PE loader.
    if b"LoadLibraryA\x00" not in data or b"JK.dll\x00" not in data:
        raise ValueError("Expected kernel32/JK.dll imports are missing")
    if len(data) != 0x220000 or u32(data, optional + 0x38) != 0x5C1000:
        raise ValueError("Unexpected image end; refusing to overwrite data")

    highest_va = 0
    highest_raw = 0
    for i in range(section_count):
        sh = sections_at + 40 * i
        va, virtual_size = u32(data, sh + 12), u32(data, sh + 8)
        raw_size, raw_ptr = u32(data, sh + 16), u32(data, sh + 20)
        highest_va = max(highest_va, align(va + max(raw_size, virtual_size),
                                          section_alignment))
        highest_raw = max(highest_raw, raw_ptr + raw_size)
    new_rva = highest_va
    new_va = IMAGE_BASE + new_rva
    new_raw = align(highest_raw, file_alignment)
    if new_rva != 0x5C1000 or new_raw != 0x220000:
        raise ValueError("Unexpected existing sections")

    # x86:
    #   push offset aRendererDll
    #   call dword ptr [__imp__LoadLibraryA]  ; if unavailable, continue vanilla
    #   jmp original_JK_entrypoint
    #   aRendererDll db 'renderer.dll', 0
    string_va = new_va + 16
    return_from_jump = new_va + 16
    displacement = IMAGE_BASE + ORIGINAL_ENTRY_RVA - return_from_jump
    stub = (b"\x68" + struct.pack("<I", string_va) +
            b"\xFF\x15" + struct.pack("<I", LOAD_LIBRARY_A_IAT_VA) +
            b"\xE9" + struct.pack("<i", displacement) + DLL_NAME)
    assert len(stub) <= file_alignment

    name = SECTION_NAME.ljust(8, b"\x00")
    sec = sections_at + section_count * 40
    # Name, VirtualSize, RVA, SizeOfRawData, PointerToRawData,
    # PointerToRelocations, PointerToLinenumbers, Counts, Characteristics
    struct.pack_into("<8sIIIIIIHHI", data, sec, name, len(stub), new_rva,
                     align(len(stub), file_alignment), new_raw,
                     0, 0, 0, 0, 0x60000020)
    put16(data, coff + 2, section_count + 1)
    put32(data, optional + 0x10, new_rva)
    put32(data, optional + 0x38, align(new_rva + len(stub), section_alignment))
    # PE CheckSum was zero in this EXE, so leave it as zero.
    if len(data) < new_raw:
        data.extend(b"\x00" * (new_raw - len(data)))
    data.extend(stub)
    data.extend(b"\x00" * (align(len(stub), file_alignment) - len(stub)))
    assert u32(data, optional + 0x10) == new_rva
    assert data[new_raw + 16:new_raw + 16 + len(DLL_NAME)] == DLL_NAME
    return bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Exact JK 2.3 RGB/smooth-normal patched exe")
    parser.add_argument("destination", type=Path, help="Output (e.g. jkgm.exe)")
    args = parser.parse_args()
    if args.source.resolve() == args.destination.resolve():
        parser.error("Destination must differ from source")
    try:
        patched = patch_executable(args.source.read_bytes())
        args.destination.write_bytes(patched)
    except (OSError, ValueError) as ex:
        print("JKGM patch failed:", ex, file=sys.stderr)
        return 1
    print("Wrote", args.destination, "(" + str(len(patched)) + " bytes)")
    print("SHA256", hashlib.sha256(patched).hexdigest())
    print("PE entrypoint now loads renderer.dll, then executes original JK startup.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
