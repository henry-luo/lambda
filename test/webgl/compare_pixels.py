#!/usr/bin/env python3
"""Compare an actual native gallery capture with the pinned browser canvas."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

reference = Path(__file__).resolve().parent.parent / 'demo/scene3d/reference'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('native', type=Path)
parser.add_argument('--reference', type=Path, default=reference / 'three-gallery-chromium.png')
parser.add_argument('--manifest', type=Path, default=reference / 'manifest.json')
parser.add_argument('--tolerance', type=int, default=8)
parser.add_argument('--minimum', type=float, default=0.99)
args = parser.parse_args()
rect = json.loads(args.manifest.read_text())['canvasRect']
box = (rect['x'], rect['y'], rect['x'] + rect['width'], rect['y'] + rect['height'])
native = Image.open(args.native).convert('RGB')
browser = Image.open(args.reference).convert('RGB')
if native.size != browser.size:
    raise SystemExit(f'Capture size mismatch: {native.size} vs {browser.size}')
difference = ImageChops.difference(native.crop(box), browser.crop(box))
histogram = difference.histogram()
samples = difference.width * difference.height * 3
matched = sum(sum(histogram[c * 256:c * 256 + args.tolerance + 1]) for c in range(3)) / samples
print(json.dumps({
    'canvas_rectangle': box,
    'mean_absolute_error_rgb': ImageStat.Stat(difference).mean,
    'channel_samples_within_tolerance_fraction': matched,
    'tolerance': args.tolerance,
    'native_sha256': hashlib.sha256(args.native.read_bytes()).hexdigest(),
    'browser_sha256': hashlib.sha256(args.reference.read_bytes()).hexdigest(),
}, indent=2))
if matched < args.minimum:
    raise SystemExit(f'Pixel comparison failed: {matched:.4%} < {args.minimum:.4%}')
