import assert from 'node:assert/strict';
import test from 'node:test';
import { compare_ink, crop_ink } from './texcmp_pixels.mjs';

function gray(width, height, points) {
  const data = new Uint8Array(width * height).fill(255);
  for (const [x, y] of points) data[y * width + x] = 0;
  return { width, height, data };
}

test('identical ink has zero error', () => {
  const image = gray(3, 3, [[0, 0], [1, 1], [2, 2]]);
  const { metrics } = compare_ink(image, image);
  assert.equal(metrics.ink_error, 0);
  assert.equal(metrics.mismatch_fraction, 0);
  assert.deepEqual(metrics.offset, { x: 0, y: 0 });
});

test('translation is aligned without resizing or dropping extra ink', () => {
  const image = gray(3, 3, [[0, 0], [1, 1], [2, 2]]);
  const translated = gray(6, 5, [[2, 1], [3, 2], [4, 3]]);
  const aligned = compare_ink(image, translated, { radius: 3 }).metrics;
  assert.deepEqual(aligned.offset, { x: 2, y: 1 });
  assert.equal(aligned.ink_error, 0);
  const reverse = compare_ink(translated, image, { radius: 3 }).metrics;
  assert.deepEqual(reverse.offset, { x: -2, y: -1 });
  assert.equal(reverse.ink_error, 0);
  translated.data[4 * translated.width + 5] = 0;
  const changed = compare_ink(image, translated, { radius: 3 }).metrics;
  assert.equal(changed.mismatched_pixels, 1);
  assert.equal(changed.ink_error, 0.25);
});

test('overlay colors identify which renderer supplied the ink', () => {
  const lambda = gray(3, 1, [[0, 0], [1, 0]]);
  const reference = gray(3, 1, [[1, 0], [2, 0]]);
  const { overlay } = compare_ink(lambda, reference, { radius: 0 });
  assert.deepEqual([...overlay.data], [255, 0, 0, 255, 0, 0, 0, 255, 0, 255, 0, 255]);
});

test('transparent margins are white and blank renders are rejected', () => {
  const image = { width: 3, height: 1, data: Buffer.from([
    0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255,
  ]) };
  const cropped = crop_ink(image);
  assert.equal(cropped.width, 1);
  assert.deepEqual(cropped.crop, { x: 1, y: 0 });
  image.data[7] = 0;
  assert.throws(() => crop_ink(image), /no visible ink/);
});

test('pixel tolerance affects mismatch counts without hiding measured error', () => {
  const lambda = gray(1, 1, [[0, 0]]);
  const reference = { width: 1, height: 1, data: Uint8Array.of(8) };
  const tolerant = compare_ink(lambda, reference, { tolerance: 16 }).metrics;
  const exact = compare_ink(lambda, reference, { tolerance: 0 }).metrics;
  assert.equal(tolerant.mismatched_pixels, 0);
  assert.equal(exact.mismatched_pixels, 1);
  assert.equal(tolerant.ink_error, 8 / 255);
});
