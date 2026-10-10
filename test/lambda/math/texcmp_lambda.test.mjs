import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { PNG } from 'pngjs';

const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const missing = ['pdflatex', 'pdftoppm', 'pdfinfo'].filter((tool) =>
  spawnSync(tool, [tool === 'pdflatex' ? '--version' : '-v']).status !== 0);

test('native math comparison paints graphics and reaction arrows', {
  skip: missing.length ? `missing tools: ${missing.join(', ')}` : false,
}, async (t) => {
  const output = fs.mkdtempSync(path.join(ROOT, 'temp', 'mathcmp-lambda-tests-'));
  const run = spawnSync(process.execPath, [path.join(ROOT, 'test/lambda/math/run_texcmp.mjs'),
    '--out', output, '--case', 'Includegraphics', '--case', 'ReactionArrows'],
  { cwd: ROOT, encoding: 'utf8', timeout: 180000, maxBuffer: 8 * 1024 * 1024 });
  fs.writeFileSync(path.join(output, 'runner.log'), (run.stdout || '') + (run.stderr || ''));
  assert.ifError(run.error);
  assert.equal(run.status, 0, run.stdout + run.stderr);
  const dir = path.join(output, fs.readdirSync(output).find((name) => name.startsWith('run-')));
  t.diagnostic(`artifacts: ${dir}`);
  const report = JSON.parse(fs.readFileSync(path.join(dir, 'report.json'), 'utf8'));
  assert.equal(report.summary.compared, 2);
  assert.equal(report.summary.errors, 0);

  await t.test('all eight logos survive native rasterization', () => {
    const svg = fs.readFileSync(path.join(dir, 'cases/Includegraphics/lambda.svg'), 'utf8');
    assert.equal((svg.match(/<image\b/g) || []).length, 8);
    assert.match(svg, /href="data:image\/png;base64,/);
    const png = PNG.sync.read(fs.readFileSync(path.join(dir, 'cases/Includegraphics/lambda.raw.png')));
    const colored = new Uint8Array(png.width * png.height);
    for (let i = 0; i < png.data.length; i += 4) {
      const [r, g, b, a] = png.data.subarray(i, i + 4);
      if (a && Math.max(r, g, b) - Math.min(r, g, b) > 40) colored[i / 4] = 1;
    }
    // Count separate colored rasters; SVG tags alone cannot detect failed decoding or clipping.
    const areas = [];
    for (let start = 0; start < colored.length; start++) {
      if (!colored[start]) continue;
      const pending = [start];
      colored[start] = 0;
      let area = 0;
      while (pending.length) {
        const pixel = pending.pop();
        area++;
        const x = pixel % png.width;
        const neighbors = [pixel - png.width, pixel + png.width,
          ...(x > 0 ? [pixel - 1] : []), ...(x + 1 < png.width ? [pixel + 1] : [])];
        for (const neighbor of neighbors) {
          if (neighbor < 0 || neighbor >= colored.length || !colored[neighbor]) continue;
          colored[neighbor] = 0;
          pending.push(neighbor);
        }
      }
      areas.push(area);
    }
    assert.equal(areas.filter((area) => area > 50).length, 8,
      `expected eight independently painted logos; component areas: ${areas}`);
  });

  await t.test('reaction formulas paint twelve arrow glyphs without command text', () => {
    const svg = fs.readFileSync(path.join(dir, 'cases/ReactionArrows/lambda.svg'), 'utf8');
    const text = [...svg.matchAll(/<text\b[^>]*>([^<]*)<\/text>/gu)].map((match) => match[1]);
    assert.equal(text.filter((glyph) => ['→', '←', '⇀', '↽'].includes(glyph)).length, 12);
    assert.doesNotMatch(text.join(''), /xrightleftarrows|xrightequilibrium|xleftequilibrium/);
    const png = PNG.sync.read(fs.readFileSync(path.join(dir, 'cases/ReactionArrows/lambda.png')));
    assert.ok(png.width < 1000, `literal command names expanded the formula to ${png.width}px`);
  });
});
