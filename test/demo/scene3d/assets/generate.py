"""Generate the demo's original, deterministic CC0 texture fixtures."""
import hashlib
import json
import math
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).parent
SIZE = 128
albedo = Image.new('RGB', (SIZE, SIZE))
normal = Image.new('RGB', (SIZE, SIZE))
for y in range(SIZE):
    for x in range(SIZE):
        grain = 0.5 + 0.5 * math.sin(x * 1.7 + math.sin(y * 0.31))
        grid = x % 32 < 2 or y % 32 < 2
        shade = 38 if grid else round(108 + 12 * grain)
        albedo.putpixel((x, y), (shade, round(shade * 0.90), round(shade * 0.78)))
        dx = 0.12 * math.sin(2 * math.pi * x / 16)
        dy = 0.12 * math.cos(2 * math.pi * y / 16)
        normal.putpixel((x, y), (round(127.5 * (1 + dx)), round(127.5 * (1 + dy)),
                                round(127.5 * (1 + math.sqrt(1 - dx * dx - dy * dy)))))
albedo.save(ROOT / 'stone.jpg', quality=95, subsampling=0)
normal.save(ROOT / 'brushed-normal.png')
manifest = {'license': 'CC0-1.0', 'author': 'Lambda project',
            'generator': 'generate.py; Pillow; deterministic procedural pixels',
            'files': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                      for name in ['stone.jpg', 'brushed-normal.png']}}
(ROOT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
