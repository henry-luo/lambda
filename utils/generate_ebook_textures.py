#!/usr/bin/env python3
"""Generate the seamless paper textures of the EPUB reader (lambda.doc.ebook).

Each texture is an RGBA PNG: a constant warm-brown ink whose alpha is tileable
fractal value noise, laid over a CSS paper color by the reader. The
output is deterministic (fixed seed) so regenerating gives identical files.

    python3 -I utils/generate_ebook_textures.py
"""
import os
import random
import struct
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lmd", "package", "doc", "ebook", "textures")
SIZE = 256


def lattice(rng, cells):
    return [[rng.random() for _ in range(cells)] for _ in range(cells)]


def smooth(t):
    return t * t * (3.0 - 2.0 * t)


def value_noise(grid, cells, x, y):
    # wraps at the tile edge, so every octave tiles seamlessly
    fx, fy = x * cells / SIZE, y * cells / SIZE
    x0, y0 = int(fx) % cells, int(fy) % cells
    x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
    tx, ty = smooth(fx - int(fx)), smooth(fy - int(fy))
    top = grid[y0][x0] + (grid[y0][x1] - grid[y0][x0]) * tx
    bottom = grid[y1][x0] + (grid[y1][x1] - grid[y1][x0]) * tx
    return top + (bottom - top) * ty


def fractal(seed, octaves):
    """octaves: (cells per tile, weight) pairs; returns SIZE*SIZE values in 0..1."""
    rng = random.Random(seed)
    grids = [(lattice(rng, cells), cells, weight) for cells, weight in octaves]
    total = sum(weight for _, _, weight in grids)
    return [sum(value_noise(g, c, x, y) * w for g, c, w in grids) / total
            for y in range(SIZE) for x in range(SIZE)]


def write_png(path, ink, alphas):
    rows = b"".join(b"\x00" + bytes(v for a in alphas[y * SIZE:(y + 1) * SIZE] for v in (*ink, a))
                    for y in range(SIZE))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)  # 8-bit RGBA
    with open(path, "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                  chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def alpha(values, low, high, levels, contrast=1.0):
    # stretch around the mean, then quantize so the PNG stays small
    mean = sum(values) / len(values)
    out = []
    for v in values:
        t = min(1.0, max(0.0, 0.5 + (v - mean) * contrast))
        out.append(int(round(low + round(t * (levels - 1)) / (levels - 1) * (high - low))))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    # fine paper grain: fibers at a few pixels plus a little per-pixel speckle
    rng = random.Random(7)
    grain = fractal(11, [(128, 1.0), (64, 0.8), (32, 0.4)])
    grain = [0.75 * g + 0.25 * rng.random() for g in grain]
    write_png(os.path.join(OUT, "grain.png"), (96, 70, 36), alpha(grain, 0, 30, 10, 2.4))
    # soft mottling: large stains, drawn scaled up by the reader
    mottle = fractal(23, [(4, 1.0), (8, 0.6), (16, 0.3)])
    write_png(os.path.join(OUT, "mottle.png"), (138, 98, 44), alpha(mottle, 0, 56, 16, 2.2))
    for name in ("grain.png", "mottle.png"):
        print(name, os.path.getsize(os.path.join(OUT, name)), "bytes")


if __name__ == "__main__":
    main()
