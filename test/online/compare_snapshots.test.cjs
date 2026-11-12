const assert = require('node:assert/strict');
const {test} = require('node:test');
const {compareImages} = require('./compare_snapshots.cjs');

function image(width, height, pixel) {
    const data = Buffer.alloc(width * height * 4);
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
        data.set(pixel(x, y), (y * width + x) * 4);
    }
    return {width, height, data};
}

test('identical images and transparent white have no difference', () => {
    const white = image(1200, 800, () => [255, 255, 255, 255]);
    const transparent = image(1200, 800, () => [0, 0, 0, 0]);
    assert.equal(compareImages(white, white).mean_error, 0);
    assert.equal(compareImages(white, transparent).mean_error, 0);
});

test('missing a large hero region is a major review candidate', () => {
    const empty = image(1200, 800, () => [255, 255, 255, 255]);
    const hero = image(1200, 800, (_, y) => y < 500 ? [20, 50, 80, 255] : [255, 255, 255, 255]);
    const result = compareImages(empty, hero);
    assert.equal(result.major_candidate, true);
    assert.equal(result.changed_area, 0.625);
});

test('small details do not qualify as a broad difference', () => {
    const empty = image(1200, 800, () => [255, 255, 255, 255]);
    const detail = image(1200, 800, (x, y) => x < 100 && y < 40 ? [0, 0, 0, 255] : [255, 255, 255, 255]);
    assert.equal(compareImages(empty, detail).major_candidate, false);
});

test('dimension mismatch stays explicit even when colors match', () => {
    const a = image(1200, 800, () => [255, 255, 255, 255]);
    const b = image(600, 400, () => [255, 255, 255, 255]);
    const result = compareImages(a, b);
    assert.equal(result.dimensions_match, false);
    assert.equal(result.mean_error, 0);
});

test('blank output remains a candidate against sparse document content', () => {
    const blank = image(1200, 800, () => [255, 255, 255, 255]);
    const document = image(1200, 800, (_, y) => y % 60 < 10
        ? [210, 210, 210, 255] : [255, 255, 255, 255]);
    const result = compareImages(blank, document);
    assert.equal(result.blank_mismatch, true);
    assert.equal(result.major_candidate, true);
    assert.ok(result.mean_error < 0.12);
});
