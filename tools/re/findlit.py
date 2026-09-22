#!/usr/bin/env python3
"""Find 32-bit constants in the literal pools of an ESP32 app image.

Xtensa code loads constants from literal pools, so Ghidra shows e.g. SERIAL_8N1 (0x800001c)
as DAT_400d0024. This prints the pool addresses that hold a value, to grep the decompiled code.

Usage: findlit.py app.bin 0x800001c 9600 115200
"""
import struct
import sys

from readmem import load

if len(sys.argv) < 3:
    sys.exit(__doc__)
segs, _, _ = load(sys.argv[1])
values = [int(x, 0) for x in sys.argv[2:]]
for base, data in segs:
    if not (0x400d0000 <= base < 0x40400000 or 0x40080000 <= base < 0x400a0000):
        continue
    for v in values:
        pattern = struct.pack("<I", v)
        i = data.find(pattern)
        while i >= 0:
            if (base + i) % 4 == 0:
                print(hex(v), "DAT_%08x" % (base + i))
            i = data.find(pattern, i + 1)
