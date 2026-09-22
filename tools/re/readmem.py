#!/usr/bin/env python3
"""Read memory of an ESP32 app image by virtual address.

Usage as tool:   readmem.py app.bin 0x400d0074 [0x... ...]   -> prints the 32-bit words
Usage as module: from readmem import load; segs, rd, u32 = load("app.bin")
Requires: pip install esptool
"""
import struct
import sys

from esptool.bin_image import LoadFirmwareImage


def load(path):
    img = LoadFirmwareImage("esp32", path)
    segs = [(s.addr, bytes(s.data)) for s in img.segments]

    def rd(addr, n):
        for base, data in segs:
            if base <= addr < base + len(data):
                return data[addr - base:addr - base + n]
        return None

    def u32(addr):
        b = rd(addr, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    return segs, rd, u32


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    _, _, u32 = load(sys.argv[1])
    for a in sys.argv[2:]:
        v = u32(int(a, 0))
        print(a, "->", hex(v) if v is not None else None)
