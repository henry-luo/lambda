"""Rebuild the original four-color PNG used to check projective texture UVs."""
from pathlib import Path
import struct
import zlib


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]
rows = [b"\0" + bytes(channel for x in range(64)
    for channel in colors[(y // 32) * 2 + x // 32]) for y in range(64)]
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 64, 64, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b"")
Path(__file__).resolve().parents[1].joinpath("tests/render/uv-grid.png").write_bytes(png)
