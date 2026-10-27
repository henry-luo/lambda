"""Write studio.hdr: a synthetic 1024 x 512 equirectangular HDR studio for the image-environment
fixtures (Radiant_Scene3d_PBR_Shadows.md M5). A grey room with a brown floor, a warm key soft box,
a cool fill, a bright window strip and a ring of top light, with values well above 1. Scanlines use
adaptive run-length encoding. Run from the repository root: python3 test/scene3d/assets/hdr/_generate.py
"""
import math, os

W, H = 1024, 512

def smooth(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3 - 2 * t)

def window(x, lo, hi, soft):
    # 1 inside lo..hi, easing to 0 over `soft` beyond each edge
    return smooth(lo - soft, lo, x) * (1.0 - smooth(hi, hi + soft, x))

def radiance(phi, lat):
    y = math.sin(lat)
    if y >= 0:
        base = [0.18 + 0.3 * y, 0.18 + 0.3 * y, 0.2 + 0.32 * y]
    else:
        base = [0.12, 0.08, 0.05]
    lights = [  # (phi range, lat range, color)
        ((-1.1, -0.5), (0.3, 0.75), (12.0, 11.0, 9.5)),
        ((1.8, 2.3), (0.0, 0.45), (3.0, 3.4, 4.2)),
        ((2.95, 3.25), (-0.1, 0.3), (20.0, 22.0, 26.0)),
    ]
    color = list(base)
    for (p0, p1), (l0, l1), c in lights:
        w = window(phi, p0, p1, 0.04) * window(lat, l0, l1, 0.04)
        for k in range(3):
            color[k] += w * c[k]
    ring = smooth(1.25, 1.3, lat)
    return [color[k] + ring * 4.0 for k in range(3)]

def rgbe(c):
    f = max(c)
    if f < 1e-32:
        return [0, 0, 0, 0]
    m, e = math.frexp(f)
    scale = m * 256.0 / f
    return [min(255, int(c[0] * scale)), min(255, int(c[1] * scale)), min(255, int(c[2] * scale)), e + 128]

def rle(data):
    out, i, n = bytearray(), 0, len(data)
    while i < n:
        run = 1
        while i + run < n and run < 127 and data[i + run] == data[i]:
            run += 1
        if run >= 4:
            out += bytes([128 + run, data[i]])
            i += run
            continue
        j = i
        while j < n and j - i < 128:
            r = 1
            while j + r < n and r < 4 and data[j + r] == data[j]:
                r += 1
            if r >= 4:
                break
            j += 1
        out += bytes([j - i]) + bytes(data[i:j])
        i = j
    return out

def main():
    out = bytearray(b"#?RADIANCE\n# synthetic studio for scene3d image environments\nFORMAT=32-bit_rle_rgbe\n\n")
    out += b"-Y %d +X %d\n" % (H, W)
    for row in range(H):
        lat = (0.5 - (row + 0.5) / H) * math.pi
        texels = [rgbe(radiance((((col + 0.5) / W) - 0.5) * 2 * math.pi, lat)) for col in range(W)]
        out += bytes([2, 2, W >> 8, W & 255])
        for ch in range(4):
            out += rle([t[ch] for t in texels])
    path = os.path.join(os.path.dirname(__file__), "studio.hdr")
    open(path, "wb").write(out)
    print(path, len(out), "bytes")

main()
