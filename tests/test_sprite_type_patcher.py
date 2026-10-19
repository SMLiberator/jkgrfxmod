"""Self-contained fake-PE tests: never need to distribute jkgm.exe in CI."""
import hashlib
import importlib.util
import struct
from pathlib import Path

source = Path(__file__).resolve().parents[1] / "tools" / "patch_jkgm_sprite_types.py"
spec = importlib.util.spec_from_file_location("sprite_patcher", source)
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)

def fixture():
    b = bytearray(0x800)
    b[:2] = b"MZ"
    struct.pack_into("<I", b, 0x3C, 0x80)
    b[0x80:0x84] = b"PE\x00\x00"
    struct.pack_into("<HH", b, 0x84, 0x14c, 1)
    struct.pack_into("<H", b, 0x94, 0xE0)
    struct.pack_into("<H", b, 0x98, 0x10B)
    struct.pack_into("<I", b, 0x98+28, 0x400000)
    off = 0x98+0xE0
    b[off:off+5] = b".text"
    struct.pack_into("<IIII", b, off+8, 0x200, 0xF2700, 0x200, 0x200)
    instruction = 0x200+(0x4F27BA-0x400000)-0xF2700
    b[instruction:instruction+len(p.BEFORE)] = p.BEFORE
    return bytes(b), instruction

def test():
    original, addr=fixture()
    assert p.va_to_offset(original,p.SPR_TYPE_CHECK_VA)==addr
    p.ORIGINAL_SHA256=hashlib.sha256(original).hexdigest()
    patched=bytearray(original)
    patched[addr:addr+len(p.BEFORE)]=p.AFTER
    p.PATCHED_SHA256=hashlib.sha256(patched).hexdigest()
    result, changed=p.patch_bytes(original)
    assert changed and result==bytes(patched)
    again, changed=p.patch_bytes(result)
    assert not changed and again==result
    wrong=bytearray(original)
    wrong[0x280]=17
    try:
        p.patch_bytes(bytes(wrong))
        assert False, "Unexpected checksum was accepted"
    except ValueError:
        pass
    wrong[addr:addr+len(p.BEFORE)]=b"\x90"*len(p.BEFORE)
    try:
        p.patch_bytes(bytes(wrong))
        assert False, "Unexpected opcode was accepted"
    except ValueError:
        pass
    print("SPR type patcher fixtures passed")

if __name__=="__main__":
    test()
