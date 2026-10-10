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

async function compareCases(t, names) {
  const output = fs.mkdtempSync(path.join(ROOT, 'temp', 'mathcmp-lambda-tests-'));
  const run = spawnSync(process.execPath, [path.join(ROOT, 'test/lambda/math/run_texcmp.mjs'),
    '--out', output, ...names.flatMap((name) => ['--case', name])],
  { cwd: ROOT, encoding: 'utf8', timeout: 180000, maxBuffer: 8 * 1024 * 1024 });
  fs.writeFileSync(path.join(output, 'runner.log'), (run.stdout || '') + (run.stderr || ''));
  assert.ifError(run.error);
  assert.equal(run.status, 0, run.stdout + run.stderr);
  const dir = path.join(output, fs.readdirSync(output).find((name) => name.startsWith('run-')));
  t.diagnostic(`artifacts: ${dir}`);
  const report = JSON.parse(fs.readFileSync(path.join(dir, 'report.json'), 'utf8'));
  assert.equal(report.summary.compared, names.length);
  assert.equal(report.summary.errors, 0);
  return dir;
}

function paintedText(svg) {
  return [...svg.matchAll(/<text\b[^>]*>([^<]*)<\/text>/gu)].map((match) => match[1]);
}

test('native math comparison paints graphics and reaction arrows', {
  skip: missing.length ? `missing tools: ${missing.join(', ')}` : false,
}, async (t) => {
  const dir = await compareCases(t, ['Includegraphics', 'ReactionArrows']);

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
    const text = paintedText(svg);
    assert.equal(text.filter((glyph) => ['→', '←', '⇀', '↽'].includes(glyph)).length, 12);
    assert.doesNotMatch(text.join(''), /xrightleftarrows|xrightequilibrium|xleftequilibrium/);
    const png = PNG.sync.read(fs.readFileSync(path.join(dir, 'cases/ReactionArrows/lambda.png')));
    assert.ok(png.width < 1000, `literal command names expanded the formula to ${png.width}px`);
  });
});

test('native math comparison paints style choices, sizes and AMS environments', {
  skip: missing.length ? `missing tools: ${missing.join(', ')}` : false,
}, async (t) => {
  const names = ['MathChoice', 'Sizing', 'SizingBaseline', 'VerticalSpacing',
    'MathtoolsMatrix', 'Gathered', 'Alignedat', 'MathAtom', 'MathOp', 'RelativeUnits'];
  const dir = await compareCases(t, names);
  const svg = (name) => fs.readFileSync(path.join(dir, `cases/${name}/lambda.svg`), 'utf8');
  const png = (name) => PNG.sync.read(fs.readFileSync(path.join(dir, `cases/${name}/lambda.png`)));
  assert.equal(paintedText(svg('MathChoice')).join(''), 'DTSSSXSSS');
  for (const name of names) assert.doesNotMatch(paintedText(svg(name)).join(''),
    /mathchoice|Huge|normalsize|scriptsize|mathrel|mathbin|matrix\*|alignedat/);
  assert.equal((svg('MathtoolsMatrix').match(/data-math-kind="matrix"/g) || []).length, 2);
  assert.equal((svg('Gathered').match(/data-math-kind="matrix"/g) || []).length, 2);
  assert.ok(png('MathChoice').width < 400, 'all four branches must not paint at once');
  assert.ok(png('SizingBaseline').height > 100, 'large glyphs must survive rasterization');
  assert.ok(png('VerticalSpacing').height > 90, 'large superscripts must not clip');
  assert.ok(png('Alignedat').height > 130, 'alignment rows need struts and jot');
});
