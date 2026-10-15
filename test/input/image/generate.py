#!/usr/bin/env python3
"""Regenerate the original illustration, codec samples, and static HTML gallery."""
from html import escape
from pathlib import Path
import json
import subprocess

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
SAMPLES = Path(__file__).resolve().parent
WORK = ROOT / 'temp' / 'image-formats-gallery'
WIDTH, HEIGHT, SCALE = 256, 192, 4

# One scene supplies both the SVG reference and the antialiased raster source.
SCENE = [
    ('rect', (8, 8, 248, 160), '#233c66'),
    ('circle', (195, 53, 30), '#fbbf77', 0.15),
    ('circle', (195, 53, 22), '#ffd392'),
    ('circle', (37, 30, 1.5), '#f5ecd9'),
    ('circle', (79, 23, 1), '#f5ecd9'),
    ('circle', (123, 38, 1.5), '#f5ecd9'),
    ('circle', (227, 25, 1), '#f5ecd9'),
    ('polygon', (8, 132, 67, 62, 127, 129, 174, 91, 248, 139, 248, 160, 8, 160), '#8785ac'),
    ('polygon', (67, 62, 88, 87, 68, 80, 53, 88), '#e8e4ed'),
    ('polygon', (8, 146, 109, 83, 173, 138, 220, 111, 248, 133, 248, 160, 8, 160), '#286474'),
    ('polygon', (109, 83, 133, 104, 111, 99, 96, 106), '#b5d9db'),
    ('rect', (8, 136, 248, 160), '#4c929e'),
    ('rect', (24, 144, 92, 146), '#90c8ca', 0.6),
    ('rect', (122, 150, 213, 152), '#90c8ca', 0.6),
    ('rect', (175, 140, 231, 142), '#90c8ca', 0.6),
]
SWATCHES = [('circle', (x, 178, 8), color, alpha) for x, color, alpha in (
    (56, '#ec936b', 1), (92, '#287b91', 1), (128, '#287b91', 0.75),
    (164, '#287b91', 0.5), (200, '#287b91', 0.25))]

FORMATS = [
    ('PNG', 'png', 'Lossless color and full alpha.', {'lossless': True}),
    ('JPEG', 'jpg', 'Quality 92; transparency flattened onto cream.', {'quality': 92}),
    ('GIF', 'gif', '255-color palette and one-bit transparency.', None),
    ('WebP', 'webp', 'Lossless color and full alpha.', {'lossless': True}),
    ('AVIF', 'avif', 'AV1 color at quality 80 with full alpha.', {'quality': 80}),
    ('BMP', 'bmp', '24-bit RGB; transparency flattened onto cream.', {}),
    ('TIFF', 'tiff', 'Single-page DEFLATE with full alpha.', {}),
    ('ICO', 'ico', 'One 256 × 192 PNG representation with full alpha.', {}),
    ('SVG', 'svg', 'Scalable vector reference for the same illustration.', None),
]


def paint(image, shape):
    kind, coords, color, *opacity = shape
    alpha = round(255 * (opacity[0] if opacity else 1))
    layer = Image.new('RGBA', image.size)
    draw = ImageDraw.Draw(layer)
    fill = color + f'{alpha:02x}'
    if kind == 'circle':
        x, y, radius = coords
        bounds = (x - radius, y - radius, x + radius, y + radius)
        draw.ellipse(tuple(round(v * SCALE) for v in bounds), fill=fill)
    elif kind == 'rect':
        # SVG rectangles exclude their far edge; Pillow rectangles include it.
        x1, y1, x2, y2 = (round(v * SCALE) for v in coords)
        draw.rectangle((x1, y1, x2 - 1, y2 - 1), fill=fill)
    else:
        draw.polygon(tuple(round(v * SCALE) for v in coords), fill=fill)
    image.alpha_composite(layer)


def svg_shape(shape):
    kind, coords, color, *opacity = shape
    attrs = f'fill="{color}"' + (f' opacity="{opacity[0]}"' if opacity else '')
    if kind == 'circle':
        x, y, radius = coords
        return f'<circle cx="{x}" cy="{y}" r="{radius}" {attrs}/>'
    if kind == 'rect':
        x1, y1, x2, y2 = coords
        return f'<rect x="{x1}" y="{y1}" width="{x2-x1}" height="{y2-y1}" {attrs}/>'
    points = ' '.join(f'{x},{y}' for x, y in zip(coords[::2], coords[1::2]))
    return f'<polygon points="{points}" {attrs}/>'


def write_source():
    image = Image.new('RGBA', (WIDTH * SCALE, HEIGHT * SCALE))
    for shape in SCENE:
        paint(image, shape)
    mask = Image.new('L', image.size)
    ImageDraw.Draw(mask).rounded_rectangle((8*SCALE, 8*SCALE, 248*SCALE-1, 160*SCALE-1),
                                           radius=18*SCALE, fill=255)
    image.putalpha(ImageChops.multiply(image.getchannel('A'), mask))
    for shape in SWATCHES:
        paint(image, shape)
    image = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    image.save(WORK / 'source.png')
    svg = '\n'.join([
        '<svg xmlns="http://www.w3.org/2000/svg" width="256" height="192" viewBox="0 0 256 192">',
        '<title>Twilight mountains</title>',
        '<desc>A warm sun, mountain peaks, water, and five color and opacity swatches.</desc>',
        '<defs><clipPath id="landscape"><rect x="8" y="8" width="240" height="152" rx="18"/></clipPath></defs>',
        '<g clip-path="url(#landscape)">', *(svg_shape(shape) for shape in SCENE), '</g>',
        *(svg_shape(shape) for shape in SWATCHES), '</svg>', '',
    ])
    (SAMPLES / 'sample.svg').write_text(svg)
    # Reserve an unused palette slot for GIF's single transparent color.
    gif = image.convert('RGB').quantize(colors=255)
    alpha = image.getchannel('A')
    gif.paste(255, mask=alpha.point(lambda value: 255 if value < 128 else 0))
    gif.save(SAMPLES / 'sample.gif', transparency=255, optimize=False)


def encode_samples():
    binary = ROOT / 'lambda-cli.exe'
    if not binary.exists():
        binary = ROOT / 'lambda.exe'
    lines = ['let image = load("temp/image-formats-gallery/source.png")^']
    for name, suffix, _, options in FORMATS:
        if options is None:
            continue
        request = {'path': f'test/input/image/sample.{suffix}', 'format': name.lower(),
                   'background': [248, 245, 236], **options}
        fields = ', '.join(f'{key}:{json.dumps(value)}' for key, value in request.items())
        lines.append(f'save(image, {{{fields}}})^')
    script = WORK / 'encode.ls'
    script.write_text('\n'.join(lines) + '\n')
    run = subprocess.run([str(binary), str(script), '--no-log'], cwd=ROOT, capture_output=True, text=True)
    (WORK / 'encode.log').write_text(run.stdout + run.stderr)
    run.check_returncode()
    expected = sum(options is not None for _, _, _, options in FORMATS)
    if run.stdout.splitlines() != ['true'] * expected:
        raise RuntimeError(f'An image encoder failed; see {WORK / "encode.log"}')
    for _, suffix, _, _ in FORMATS:
        (SAMPLES / f'sample.{suffix}').chmod(0o644)


def write_gallery():
    cards = []
    for name, suffix, description, _ in FORMATS:
        filename = f'sample.{suffix}'
        size = (SAMPLES / filename).stat().st_size
        size_text = f'{size / 1024:.1f} KiB' if size >= 1024 else f'{size} bytes'
        cards.append(f'''    <article class="card" id="format-{suffix}">
      <div class="card-heading"><h2>{name}</h2><span class="extension">.{suffix}</span></div>
      <a class="preview" href="../input/image/{filename}" aria-label="Open {name} sample">
        <img src="../input/image/{filename}" width="256" height="192" alt="Twilight mountains in {name}, with color and opacity swatches">
      </a>
      <p class="description">{escape(description)}</p>
      <div class="file-info"><span>256 × 192</span><a href="../input/image/{filename}">{size_text} ↗</a></div>
    </article>''')
    template = (SAMPLES / 'gallery.template.html').read_text()
    (ROOT / 'test' / 'html' / 'image_formats_gallery.html').write_text(
        template.replace('<!-- FORMAT_CARDS -->', '\n'.join(cards)))


if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True)
    write_source()
    encode_samples()
    write_gallery()
