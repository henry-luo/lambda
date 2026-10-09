// Compare grayscale ink after cropping page margins and aligning by translation.
import fs from 'node:fs';
import { PNG } from 'pngjs';

export function read_ink(file) {
  return crop_ink(PNG.sync.read(fs.readFileSync(file)));
}

export function crop_ink(image) {
  const gray = new Uint8Array(image.width * image.height);
  let left = image.width, top = image.height, right = -1, bottom = -1;
  for (let y = 0; y < image.height; y++) {
    for (let x = 0; x < image.width; x++) {
      const i = (y * image.width + x) * 4;
      // Composite alpha onto white so transparent SVG backgrounds are not ink.
      const alpha = image.data[i + 3] / 255;
      const value = Math.round(255 * (1 - alpha) + alpha *
        (0.2126 * image.data[i] + 0.7152 * image.data[i + 1] + 0.0722 * image.data[i + 2]));
      gray[y * image.width + x] = value;
      if (value < 250) {
        left = Math.min(left, x); right = Math.max(right, x);
        top = Math.min(top, y); bottom = Math.max(bottom, y);
      }
    }
  }
  if (right < left) throw new Error('rendered image contains no visible ink');
  const width = right - left + 1, height = bottom - top + 1;
  const data = new Uint8Array(width * height);
  for (let y = 0; y < height; y++)
    data.set(gray.subarray((y + top) * image.width + left,
      (y + top) * image.width + left + width), y * width);
  return { width, height, data, crop: { x: left, y: top } };
}

export function compare_ink(lambda, reference, { radius = 12, tolerance = 16 } = {}) {
  // Correlate actual ink, avoiding scores dominated by white page backgrounds.
  const ink = [];
  for (let y = 0; y < lambda.height; y++)
    for (let x = 0; x < lambda.width; x++) {
      const weight = 255 - lambda.data[y * lambda.width + x];
      if (weight > 5) ink.push({ x, y, weight });
    }
  let dx = 0, dy = 0, best = -1;
  for (let sy = -radius; sy <= radius; sy++) {
    for (let sx = -radius; sx <= radius; sx++) {
      let overlap = 0;
      for (const point of ink) {
        const x = point.x + sx, y = point.y + sy;
        if (x >= 0 && x < reference.width && y >= 0 && y < reference.height)
          overlap += point.weight * (255 - reference.data[y * reference.width + x]);
      }
      if (overlap > best || (overlap === best && sx * sx + sy * sy < dx * dx + dy * dy)) {
        best = overlap; dx = sx; dy = sy;
      }
    }
  }
  const lx = Math.max(dx, 0), ly = Math.max(dy, 0);
  const rx = Math.max(-dx, 0), ry = Math.max(-dy, 0);
  const width = Math.max(lx + lambda.width, rx + reference.width);
  const height = Math.max(ly + lambda.height, ry + reference.height);
  const overlay = new PNG({ width, height });
  let difference = 0, inkMass = 0, union = 0, intersection = 0, mismatched = 0;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const l = pixel(lambda, x - lx, y - ly), r = pixel(reference, x - rx, y - ry);
      const delta = Math.abs(l - r);
      difference += delta;
      inkMass += 255 - Math.min(l, r);
      if (l < 250 || r < 250) { union++; if (delta > tolerance) mismatched++; }
      if (l < 250 && r < 250) intersection++;
      const i = (y * width + x) * 4;
      // LaTeX-only ink is green; Lambda-only ink is red, as in upstream texcmp.
      overlay.data[i] = r; overlay.data[i + 1] = l;
      overlay.data[i + 2] = Math.min(l, r); overlay.data[i + 3] = 255;
    }
  }
  return {
    overlay,
    metrics: {
      offset: { x: dx, y: dy }, alignment_radius: radius,
      alignment_at_boundary: radius > 0 && (Math.abs(dx) === radius || Math.abs(dy) === radius),
      lambda_size: { width: lambda.width, height: lambda.height },
      reference_size: { width: reference.width, height: reference.height },
      // Normalizing by union ink mass makes missing/extra symbols count strongly.
      ink_error: difference / inkMass,
      ink_overlap: intersection / union,
      mismatched_pixels: mismatched, union_ink_pixels: union,
      mismatch_fraction: mismatched / union, pixel_tolerance: tolerance,
    },
  };
}

function pixel(image, x, y) {
  return x >= 0 && x < image.width && y >= 0 && y < image.height
    ? image.data[y * image.width + x] : 255;
}

export function write_ink(file, image) {
  const png = new PNG({ width: image.width, height: image.height });
  for (let i = 0; i < image.data.length; i++) {
    png.data[i * 4] = png.data[i * 4 + 1] = png.data[i * 4 + 2] = image.data[i];
    png.data[i * 4 + 3] = 255;
  }
  fs.writeFileSync(file, PNG.sync.write(png));
}

export function write_overlay(file, overlay) {
  fs.writeFileSync(file, PNG.sync.write(overlay));
}
