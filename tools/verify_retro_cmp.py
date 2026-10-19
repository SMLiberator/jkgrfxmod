#!/usr/bin/env python3
"""Compare JkGRfxMod's logged Retro Mode palette against the level's first CMP.

The on-disk CMP RGB888 table starts at offset 0x40, while JK's in-memory
rdColormap::colors[256] starts at offset 0x10. This tool checks the exact
768 bytes, not a screenshot or a color-corrected approximation.
"""
import argparse
from pathlib import Path
import re
import struct
import sys

PALETTE_SIZE = 256 * 3
CMP_PALETTE_OFFSET = 0x40
DUMP_ROW = re.compile(r"Retro CMP #0 RGB\[(\d{1,3})\]:([0-9a-fA-F]{96})")


def fnv1a32(data):
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def load_cmp_palette(path):
    data = Path(path).read_bytes()
    if len(data) < CMP_PALETTE_OFFSET + PALETTE_SIZE:
        raise ValueError("CMP is too short to contain a full RGB888 palette")
    if data[:4] != b"CMP ":
        raise ValueError("Invalid CMP magic (expected 'CMP ')")
    version, = struct.unpack_from("<I", data, 4)
    if version != 0x1E:
        raise ValueError("Unsupported CMP version: %d" % version)
    return data[CMP_PALETTE_OFFSET:CMP_PALETTE_OFFSET + PALETTE_SIZE]


def last_complete_logged_palette(text):
    rows = {}
    palettes = []
    for line in text.splitlines():
        match = DUMP_ROW.search(line)
        if not match:
            continue
        start = int(match.group(1))
        if start > 240 or start % 16:
            continue
        if start == 0:
            rows = {}
        rows[start] = bytes.fromhex(match.group(2))
        if start == 240 and len(rows) == 16:
            palettes.append(b"".join(rows[i] for i in range(0, 256, 16)))
    if not palettes:
        raise ValueError(
            "No complete Retro CMP #0 dump found in log. "
            "Enable retro_mode and log_path, then load a level."
        )
    return palettes[-1]


def compare_palettes(cmp_palette, game_palette, max_differences=32):
    differences = []
    for i in range(256):
        expected = tuple(cmp_palette[i*3:i*3+3])
        actual = tuple(game_palette[i*3:i*3+3])
        if expected != actual:
            differences.append((i, expected, actual))
    print("CMP file palette FNV1a32: %u (0x%08X)" %
          (fnv1a32(cmp_palette), fnv1a32(cmp_palette)))
    print("JK memory palette FNV1a32: %u (0x%08X)" %
          (fnv1a32(game_palette), fnv1a32(game_palette)))
    print("RGB entries mismatched: %d / 256" % len(differences))
    if differences:
        for index, expected, actual in differences[:max_differences]:
            print("  index %3d: CMP %s vs JK %s" %
                  (index, expected, actual))
        if len(differences) > max_differences:
            print("  ... and %d more" % (len(differences)-max_differences))
    else:
        print("PASS: All 768 palette bytes match exactly.")
    return len(differences) == 0


def self_test():
    sample = bytes((i * 17 + 5) % 256 for i in range(PALETTE_SIZE))
    log = "\n".join(
        "INFO Retro CMP #0 RGB[%d]:%s" %
        (i, sample[i*3:i*3+48].hex().upper())
        for i in range(0, 256, 16)
    )
    assert last_complete_logged_palette(log) == sample
    assert last_complete_logged_palette(log + "\n" + log) == sample
    cmp_bytes = b"CMP " + struct.pack("<I", 0x1E) + bytes(0x38) + sample
    assert cmp_bytes[0x40:0x40 + PALETTE_SIZE] == sample
    assert fnv1a32(sample) != 0
    bad = bytearray(sample)
    bad[0] ^= 1
    assert sum(sample[i:i+3] != bad[i:i+3]
               for i in range(0, PALETTE_SIZE, 3)) == 1
    print("Retro CMP verification self-test passed.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cmp", nargs="?", type=Path,
                        help="Level's first CMP (index 0), extracted from GOB if necessary")
    parser.add_argument("log", nargs="?", type=Path,
                        help="JkGRfxMod log generated while retro_mode is true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.cmp is None or args.log is None:
        parser.error("Specify both the level's first .cmp and the JKGM log")
    try:
        expected = load_cmp_palette(args.cmp)
        actual = last_complete_logged_palette(
            args.log.read_text(encoding="utf-8", errors="replace"))
    except (OSError, ValueError) as err:
        print("ERROR: %s" % err, file=sys.stderr)
        return 2
    return 0 if compare_palettes(expected, actual) else 1


if __name__ == "__main__":
    sys.exit(main())
