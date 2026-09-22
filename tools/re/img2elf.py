# Convert an ESP32 app image (.bin) into an ELF with one PT_LOAD per segment, for Ghidra.
import sys, struct
from esptool.bin_image import LoadFirmwareImage
img = LoadFirmwareImage("esp32", sys.argv[1])
segs = [(s.addr, bytes(s.data)) for s in img.segments]
ehsize, phsize = 52, 32
off = ehsize + phsize * len(segs)
ph, body = b"", b""
for addr, data in segs:
    flags = 5 if 0x40000000 <= addr < 0x40400000 or 0x400D0000 <= addr < 0x40400000 else 6
    ph += struct.pack("<IIIIIIII", 1, off + len(body), addr, addr, len(data), len(data), flags, 4)
    body += data
eh = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
eh += struct.pack("<HHIIIIIHHHHHH", 2, 94, 1, img.entrypoint, ehsize, 0, 0x300, ehsize, phsize, len(segs), 40, 0, 0)
open(sys.argv[2], "wb").write(eh + ph + body)
for a, d in segs: print(hex(a), len(d))
