"""Generate the original raster landscape; Python standard library, no downloads."""
import math
from pathlib import Path
import struct
import zlib

WIDTH, HEIGHT = 960, 480


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


rows = bytearray()
for y in range(HEIGHT):
    rows.append(0)
    for x in range(WIDTH):
        u, v = x / WIDTH, y / HEIGHT
        sky = v / 0.7
        rgb = tuple(round(a + (b - a) * sky) for a, b in zip((27, 65, 92), (232, 169, 136)))
        if (u - 0.69) ** 2 + (v - 0.32) ** 2 < 0.095 ** 2:
            rgb = (255, 215, 159)
        for layer, color in enumerate([(83, 112, 124), (51, 94, 111), (33, 72, 90), (18, 49, 66)]):
            ridge = 0.52 + layer * 0.105 + 0.085 * math.sin(u * 9 + layer * 1.7)
            ridge += 0.025 * math.sin(u * 24 + layer * 2.1)
            if v > ridge:
                rgb = color
        rows.extend((*rgb, 255))

header = struct.pack('>IIBBBBB', WIDTH, HEIGHT, 8, 6, 0, 0, 0)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')
Path(__file__).with_name('landscape.png').write_bytes(png)
