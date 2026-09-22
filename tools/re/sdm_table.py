#!/usr/bin/env python3
"""Dump the Kinetos MID meter register table (12 entries of 12 bytes: register, scale, value).

Usage: sdm_table.py app.bin [table_pointer_literal=0x400d0074]
"""
import struct
import sys

from readmem import load

if len(sys.argv) < 2:
    sys.exit(__doc__)
_, rd, u32 = load(sys.argv[1])
table = u32(int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x400d0074)
print("table @", hex(table))
for i in range(12):
    reg, _, scale, value = struct.unpack("<HHff", rd(table + i * 12, 12))
    print("%2d  reg 0x%04x  scale %.1f" % (i, reg, scale))
