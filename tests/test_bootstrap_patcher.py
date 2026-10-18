import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import patch_jkgm_bootstrap as p

def main():
    data = bytearray(0x220000)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\x00\x00"
    coff, opt, sections = 0x84, 0x98, 0x178
    for off, val, fmt in [
        (coff, 0x14c, "<H"), (coff+2, 7, "<H"), (coff+16, 0xe0, "<H"),
        (opt, 0x10b, "<H"), (opt+0x10, 0x113cc0, "<I"),
        (opt+0x1c, 0x400000, "<I"), (opt+0x20, 0x1000, "<I"),
        (opt+0x24, 0x200, "<I"), (opt+0x38, 0x5c1000, "<I"),
        (opt+0x3c, 0x400, "<I"),
    ]:
        struct.pack_into(fmt, data, off, val)
    layout = [
        (".text", 0x1000, 0x120200, 0x400, 0x120200),
        (".rdata", 0x122000, 0x2200, 0x120600, 0x2200),
        (".data", 0x125000, 0x3ca6f0, 0x122800, 0x2d800),
        (".idata", 0x4f0000, 0x2000, 0x150000, 0x1300),
        (".rsrc", 0x4f2000, 0x2000, 0x151400, 0x1c00),
        (".vbuf", 0x4f4000, 0xc000, 0x153000, 0xc000),
        (".rgbx", 0x500000, 0xc0f70, 0x15f000, 0xc1000),
    ]
    for i, (name, va, vs, rp, rs) in enumerate(layout):
        struct.pack_into("<8sIIII", data, sections+i*40,
            name.encode().ljust(8, b"\x00"), vs, va, rs, rp)
    data[0x150300:0x15030d] = b"LoadLibraryA\x00"
    data[0x150350:0x150357] = b"JK.dll\x00"
    old = bytes(data)
    true_sha = p.ORIGINAL_SHA256
    p.ORIGINAL_SHA256 = hashlib.sha256(old).hexdigest()
    try:
        new = p.patch_executable(old)
    finally:
        p.ORIGINAL_SHA256 = true_sha
    assert len(new) == len(old)+0x200
    assert new[0x400:len(old)] == old[0x400:]
    assert struct.unpack_from("<H", new, coff+2)[0] == 8
    assert struct.unpack_from("<I", new, opt+0x10)[0] == 0x5c1000
    assert struct.unpack_from("<I", new, opt+0x38)[0] == 0x5c2000
    code = new[0x220000:0x220020]
    assert code[0] == 0x68 and struct.unpack_from("<I", code, 1)[0] == 0x9c1010
    assert code[5:7] == b"\xff\x15"
    assert struct.unpack_from("<I", code, 7)[0] == 0x8f0598
    assert code[11] == 0xe9
    assert 0x9c1010 + struct.unpack_from("<i", code, 12)[0] == 0x513cc0
    assert code[16:29] == b"renderer.dll\x00"
    try:
        p.patch_executable(new)
    except ValueError:
        pass
    else:
        raise AssertionError("Unexpected repeat patch acceptance")
    print("PE bootstrap patch test passed")

if __name__ == "__main__":
    main()
