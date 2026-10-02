#!/usr/bin/env python3
"""Generate the Lambda application icon set under lambda/asset/.

This script is the single source of the icon geometry. It writes three SVG
variants and rasterises them into the per-platform containers:

  lambda-icon.svg        macOS grid: 824px body on a 1024 canvas, baked shadow
  lambda-icon-flat.svg   full-bleed body, no shadow (Windows / Linux, >= 64px)
  lambda-icon-small.svg  full-bleed body, enlarged glyph snapped to the pixel
                         grid at 16/24/32/48px
  lambda.icns            macOS (needs iconutil, so only built on macOS)
  lambda.ico             Windows: 16-64px as 32-bit DIB, 256px as PNG
  png/lambda-<n>.png     Linux hicolor sizes and runtime window icons

It also writes radiant/app_icon_data.h, the PNGs that radiant/app_icon.cpp
installs at runtime (macOS Dock tile, X11 window icon). Windows takes
its icon from lambda.rc, which embeds lambda.ico in the executable.

Requires rsvg-convert (librsvg). Usage: python3 utils/generate_icon.py
"""
import os
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "lambda", "asset")
DATA_HEADER = os.path.join(ROOT, "radiant", "app_icon_data.h")
TMP = os.path.join(ROOT, "temp", "icon_build")

# brand colours, shared with site/css (--color-primary / --color-accent)
VIOLET, SKY = "#7c6ef6", "#38bdf8"

# lambda glyph on the 1024 canvas: both strokes lean 22.8deg (slope 0.42), 118 wide,
# cap line y=236, baseline y=788, x extent 282..742 so it centres on 512
GLYPH = ("M742 788 L624 788 L512 521.5 L400 788 L282 788 L452.9 381 L446.6 366 "
         "C434 336 414 326 380 326 C352 326 328 336 308 352 L298 268 "
         "C328 246 366 236 408 236 C458 236 520 258 549.5 330 Z")
GLYPH_TOP, GLYPH_BASE = 236, 788

BODY_SIZE, BODY_RADIUS = 824, 185.4   # macOS icon grid body and its corner radius
FULL_BLEED = 1.2                      # body scale that fills the canvas, leaving ~1.7% margin
# glyph scale for small sizes: cap line and baseline land on y=128/896 and the sides on
# x=192/832, which are whole pixels at 16, 24, 32 and 48px
SMALL_GLYPH = (896 - 128) / (GLYPH_BASE - GLYPH_TOP)


def body_path():
    """Rounded square with continuous-curvature corners (the macOS icon shape)."""
    x0 = y0 = (1024 - BODY_SIZE) / 2
    x1 = y1 = x0 + BODY_SIZE
    r = BODY_RADIUS
    # one corner as three cubics, as offsets (along the incoming edge, into the corner) in radii
    arc = [((1.08849, 0), (0.86841, 0), (0.63149, 0.07491)),
           ((0.37282, 0.16906), (0.16906, 0.37282), (0.07491, 0.63149)),
           ((0, 0.86841), (0, 1.08849), (0, 1.52866))]
    corners = [  # each maps (a, b) to canvas coordinates, walking clockwise from the top edge
        lambda a, b: (x1 - a * r, y0 + b * r),
        lambda a, b: (x1 - b * r, y1 - a * r),
        lambda a, b: (x0 + a * r, y1 - b * r),
        lambda a, b: (x0 + b * r, y0 + a * r),
    ]
    d = ["M%.2f %.2f" % (x0 + 1.52866 * r, y0)]
    for corner in corners:
        d.append("L%.2f %.2f" % corner(1.52866, 0))
        for curve in arc:
            d.append("C" + " ".join("%.2f %.2f" % corner(a, b) for a, b in curve))
    return " ".join(d) + " Z"


def about_centre(scale, inner):
    return f'<g transform="translate(512 512) scale({scale:.4f}) translate(-512 -512)">{inner}</g>'


def icon_svg(variant):
    body = body_path()
    defs = [
        f'<linearGradient id="brand" x1="0" y1="0" x2="1" y2="1">'
        f'<stop offset="0" stop-color="{VIOLET}"/><stop offset="1" stop-color="{SKY}"/></linearGradient>',
        '<linearGradient id="gloss" x1="0" y1="0" x2="0" y2="1">'
        '<stop offset="0" stop-color="#fff" stop-opacity="0.11"/>'
        '<stop offset="0.55" stop-color="#fff" stop-opacity="0"/></linearGradient>',
    ]
    plate = f'<path d="{body}" fill="url(#brand)"/>'
    gloss = f'<path d="{body}" fill="url(#gloss)"/>'
    glyph = f'<path d="{GLYPH}" fill="#fff"/>'
    if variant == "mac":
        defs.append(
            '<filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">'
            '<feGaussianBlur in="SourceAlpha" stdDeviation="14"/><feOffset dy="12"/>'
            '<feComponentTransfer><feFuncA type="linear" slope="0.35"/></feComponentTransfer>'
            '<feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>')
        defs.append(
            '<filter id="lift" x="-20%" y="-20%" width="140%" height="140%">'
            '<feGaussianBlur in="SourceAlpha" stdDeviation="10"/><feOffset dy="8"/>'
            '<feComponentTransfer><feFuncA type="linear" slope="0.22"/></feComponentTransfer>'
            '<feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge></filter>')
        art = f'<g filter="url(#shadow)">{plate}</g>{gloss}<g filter="url(#lift)">{glyph}</g>'
    elif variant == "flat":
        # blurred shadows are dropped: they only smear at the sizes this variant serves
        art = about_centre(FULL_BLEED, plate + gloss + glyph)
    elif variant == "small":
        art = about_centre(FULL_BLEED, plate + gloss) + about_centre(SMALL_GLYPH, glyph)
    else:
        raise ValueError(variant)
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">\n'
            '<!-- generated by utils/generate_icon.py; edit the script, not this file -->\n'
            f'<defs>{"".join(defs)}</defs>\n{art}\n</svg>\n')


SVG_FILES = {"mac": "lambda-icon.svg", "flat": "lambda-icon-flat.svg", "small": "lambda-icon-small.svg"}


def flat_variant(size):
    return "small" if size <= 48 else "flat"


def render(variant, size, path):
    subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size),
                    os.path.join(OUT, SVG_FILES[variant]), "-o", path], check=True)


def read_png_rgba(path):
    """Decode an 8-bit RGBA, non-interlaced PNG (what rsvg-convert emits) to top-down rows."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            assert (depth, colour, interlace) == (8, 6, 0), "expected 8-bit RGBA, non-interlaced"
        elif kind == b"IDAT":
            idat += chunk
        pos += length + 12
    raw, stride, rows, prev = zlib.decompress(idat), width * 4, [], bytearray(width * 4)
    for y in range(height):
        start = y * (stride + 1)
        kind, row = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):  # undo the per-row PNG filter
            left = row[i - 4] if i >= 4 else 0
            up = prev[i]
            upleft = prev[i - 4] if i >= 4 else 0
            if kind == 1:
                row[i] = (row[i] + left) & 255
            elif kind == 2:
                row[i] = (row[i] + up) & 255
            elif kind == 3:
                row[i] = (row[i] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                pred = left if pa <= pb and pa <= pc else (up if pb <= pc else upleft)
                row[i] = (row[i] + pred) & 255
        rows.append(row)
        prev = row
    return width, height, rows


def ico_dib(path):
    """ICO image payload for a small size: BITMAPINFOHEADER + BGRA XOR bitmap + 1bpp AND mask."""
    width, height, rows = read_png_rgba(path)
    xor, mask = bytearray(), bytearray()
    mask_stride = ((width + 31) // 32) * 4
    for row in reversed(rows):  # DIBs are stored bottom-up
        bits = bytearray(mask_stride)
        for x in range(width):
            r, g, b, a = row[x * 4:x * 4 + 4]
            xor += bytes((b, g, r, a))
            if a == 0:  # AND mask marks fully transparent pixels for pre-alpha renderers
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    # the header height counts both bitmaps, hence the doubling
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0)
    return header + bytes(xor) + bytes(mask)


def write_ico(path, sizes):
    images = []
    for size in sizes:
        png = os.path.join(TMP, f"ico_{size}.png")
        render(flat_variant(size), size, png)
        # 256px is stored as PNG (the only size Windows expects compressed); smaller ones as DIB
        images.append(open(png, "rb").read() if size >= 256 else ico_dib(png))
    offset = 6 + 16 * len(sizes)
    out = struct.pack("<HHH", 0, 1, len(sizes))
    for size, image in zip(sizes, images):
        out += struct.pack("<BBBBHHII", size & 255, size & 255, 0, 0, 1, 32, len(image), offset)  # 256 -> 0
        offset += len(image)
    with open(path, "wb") as f:
        f.write(out + b"".join(images))


def write_icns(path):
    if not shutil.which("iconutil"):
        print("generate_icon: iconutil not found, skipping lambda.icns (macOS only)")
        return
    iconset = os.path.join(TMP, "lambda.iconset")
    os.makedirs(iconset, exist_ok=True)
    for size in (16, 32, 128, 256, 512):
        render("mac", size, os.path.join(iconset, f"icon_{size}x{size}.png"))
        render("mac", size * 2, os.path.join(iconset, f"icon_{size}x{size}@2x.png"))
    subprocess.run(["iconutil", "-c", "icns", iconset, "-o", path], check=True)


def c_array(name, path):
    data = open(path, "rb").read()
    lines = [", ".join("0x%02x" % b for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return f"static const unsigned char {name}[] = {{\n    " + ",\n    ".join(lines) + ",\n};\n"


def write_data_header(path, window_sizes):
    """Embed the runtime icons: one Dock image for macOS, a size ladder for other GLFW platforms."""
    dock = os.path.join(TMP, "dock_512.png")
    render("mac", 512, dock)
    out = ["// generated by utils/generate_icon.py from the artwork in lambda/asset/; do not edit.\n"
           "// PNG files embedded for radiant/app_icon.cpp, which is the only file that includes this.\n\n"
           "#include <stddef.h>\n\n"
           "#if defined(__APPLE__)\n\n"
           "// macOS grid artwork (margin and shadow included), 512px for a magnified Retina Dock\n",
           c_array("app_icon_png_dock", dock),
           "\n#else\n\n"]
    for size in window_sizes:
        out.append(c_array(f"app_icon_png_{size}", os.path.join(OUT, "png", f"lambda-{size}.png")) + "\n")
    out.append("typedef struct AppIconPng {\n    const unsigned char* data;\n    size_t length;\n} AppIconPng;\n\n"
               "static const AppIconPng app_icon_pngs[] = {\n")
    for size in window_sizes:
        out.append(f"    {{app_icon_png_{size}, sizeof(app_icon_png_{size})}},\n")
    out.append("};\n#define APP_ICON_PNG_COUNT %d\n\n#endif\n" % len(window_sizes))
    with open(path, "w") as f:
        f.write("".join(out))


def main():
    if not shutil.which("rsvg-convert"):
        sys.exit("generate_icon: rsvg-convert not found (install librsvg)")
    os.makedirs(os.path.join(OUT, "png"), exist_ok=True)
    os.makedirs(TMP, exist_ok=True)
    for variant, name in SVG_FILES.items():
        with open(os.path.join(OUT, name), "w") as f:
            f.write(icon_svg(variant))
    for size in (16, 24, 32, 48, 64, 128, 256, 512):
        render(flat_variant(size), size, os.path.join(OUT, "png", f"lambda-{size}.png"))
    write_ico(os.path.join(OUT, "lambda.ico"), (16, 20, 24, 32, 40, 48, 64, 256))
    write_icns(os.path.join(OUT, "lambda.icns"))
    write_data_header(DATA_HEADER, (16, 32, 48, 128, 256))
    print("generate_icon: wrote", os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
