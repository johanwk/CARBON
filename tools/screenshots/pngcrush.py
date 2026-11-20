#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Losslessly recompress PNGs in place (re-deflate IDAT; pixels unchanged).

The C renderer writes stored (uncompressed) deflate blocks to avoid a zlib
dependency. Usage: pngcrush.py FILE.png...
"""
import struct, sys, zlib

def recompress(png):
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    i, head, idat = 8, [], b""
    while i < len(png):
        n = struct.unpack(">I", png[i:i+4])[0]
        t, body = png[i+4:i+8], png[i+8:i+8+n]
        if t == b"IDAT": idat += body
        elif t != b"IEND": head.append((t, body))
        i += 12 + n
    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
    raw = zlib.decompress(idat)
    return (png[:8] + b"".join(chunk(t, b) for t, b in head) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

if __name__ == "__main__":
    for path in sys.argv[1:]:
        data = open(path, "rb").read()
        out = recompress(data)
        open(path, "wb").write(out)
