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

async function compareCases(t, names, data = null) {
  const output = fs.mkdtempSync(path.join(ROOT, 'temp', 'mathcmp-lambda-tests-'));
  const run = spawnSync(process.execPath, [path.join(ROOT, 'test/lambda/math/run_texcmp.mjs'),
    '--out', output, ...(data ? ['--data', data] : []), ...names.flatMap((name) => ['--case', name])],
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

function assertPaintedRows(png) {
  for (let y = 0; y < png.height; y++) {
    const row = png.data.subarray(y * png.width * 4, (y + 1) * png.width * 4);
    // crop_ink retains faint antialiased edge pixels below 250, including the tip's first row.
    assert.ok(row.some((v, i) => i % 4 !== 3 && v < 250), `unpainted seam at row ${y}`);
  }
}

test('native math painting retains radical recipes and finite wide accents', {
  skip: missing.length ? `missing tools: ${missing.join(', ')}` : false,
}, async (t) => {
  const names = ['SmallRoot', 'FiniteRoot', 'TallRoot', 'IndexedRoot', 'WideHatOne',
    'WideHatTwo', 'WideHatCap', 'WideTildeCap', 'StyleRoots'];
  const dir = await compareCases(t, names, path.join(ROOT, 'test/lambda/math/radical_cases.yaml'));
  for (const name of names) await t.test(name, () => {
    const svg = fs.readFileSync(path.join(dir, `cases/${name}/lambda.svg`), 'utf8');
    const text = paintedText(svg);
    const png = PNG.sync.read(fs.readFileSync(path.join(dir, `cases/${name}/lambda.png`)));
    assert.ok(png.data.some((v, i) => i % 4 !== 3 && v < 128), 'native ink must survive');
    if (name === 'TallRoot') {
      assert.equal(text[0], '\uE001');
      assert.equal(text.at(-1), '⎷');
      assert.ok(text.filter((ch) => ch === '\uE000').length > 1, 'repeat pieces must paint');
      assert.ok(png.height > 6 * 64, 'full radical extent must survive');
      assertPaintedRows(png);
    } else if (name.startsWith('Wide')) {
      assert.deepEqual(text, [name.startsWith('WideHat') ? 'ˆ' : '˜']);
      const size = name.endsWith('One') ? 1 : name.endsWith('Two') ? 2 : 3;
      assert.match(svg, new RegExp(`font-family="KaTeX_Size${size}"`));
      assert.ok(png.width > 20, 'designed accent must paint across its natural width');
      assertPaintedRows(png);
    } else if (name === 'StyleRoots') {
      assert.equal(text.filter((ch) => ch === '√').length, 4);
      assert.doesNotMatch(svg, /scale\(1 [^1]/, 'root signs retain natural proportions');
    } else if (name === 'IndexedRoot') {
      assert.ok(text.includes('3') && text.includes('a') && text.includes('d'));
    } else {
      assert.deepEqual(text, ['√']);
      assert.match(svg, new RegExp(`font-family="${name === 'SmallRoot' ? 'KaTeX_Main' : 'KaTeX_Size4'}"`));
    }
  });
});

test('native math painting retains tall CMEX delimiter assemblies', {
  skip: missing.length ? `missing tools: ${missing.join(', ')}` : false,
}, async (t) => {
  const names = ['TallParentheses', 'TallBrackets', 'TallBraces', 'FiniteAngles', 'PointRule'];
  const dir = await compareCases(t, names, path.join(ROOT, 'test/lambda/math/delimiter_cases.yaml'));
  for (const name of names) await t.test(name, () => {
    const svg = fs.readFileSync(path.join(dir, `cases/${name}/lambda.svg`), 'utf8');
    const text = paintedText(svg);
    const png = PNG.sync.read(fs.readFileSync(path.join(dir, `cases/${name}/lambda.png`)));
    if (name === 'PointRule') {
      assert.match(svg, /\bwidth="1em"/);
      const reference = PNG.sync.read(fs.readFileSync(path.join(dir, `cases/${name}/pdflatex.png`)));
      // A ten-point rule is one reference em; integer raster bounds differ by at most one pixel.
      assert.ok(Math.abs(png.width - reference.width) <= 1, 'authored points must use the same logical em');
      return;
    }
    assert.match(svg, /font-family="KaTeX_Size4"/);
    if (name === 'FiniteAngles') assert.deepEqual(text, ['⟨', '⟩']);
    else {
      assert.ok(text.length > 4, 'tips and repeated components must reach native painting');
      assert.ok(png.height > 6 * 64, 'full assemblies must survive beyond their finite designs');
      assertPaintedRows(png);
    }
  });
});

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

test('native math comparison paints formerly missing constructs', {
  skip: missing.length ? `missing tools: ${missing.join(', ')}` : false,
}, async (t) => {
  const names = ['Raisebox', 'Reflectbox', 'CD', 'ExtensibleArrows', 'StrikeThrough',
    'StrikeThroughColor', 'Arrays', 'FractionTest', 'HorizontalBrackets', 'StretchyAccent',
    'LowerAccent', 'TextWithMath', 'TextStacked', 'AccentsText', 'Verb', 'Colorbox',
    'OpLimits', 'Integrands', 'Mod', 'Mapsfrom', 'OperatorName', 'Tag', 'Phase', 'PrimeSuper'];
  const dir = await compareCases(t, names);
  const svg = (name) => fs.readFileSync(path.join(dir, `cases/${name}/lambda.svg`), 'utf8');
  const paint = (name) => paintedText(svg(name)).join('');
  for (const name of names.filter((n) => n !== 'Verb')) assert.doesNotMatch(paint(name),
    /raisebox|reflectbox|xRightarrow|xhookrightarrow|xtwoheadrightarrow|cancel|hline|subarray|genfrac|overbracket|underbracket|substack|intop|oiint|mapsfrom|operatorname|phase|\\textbf/);
  assert.equal((svg('Reflectbox').match(/scale\(-1 1\)/g) || []).length, 4);
  assert.match(svg('StrikeThrough'), /data-math-kind="xcancel"/);
  assert.match(svg('StrikeThrough'), /data-math-kind="sout"/);
  assert.match(svg('Arrays'), /stroke-dasharray="150 100"/);
  assert.match(svg('ExtensibleArrows'), /data-math-kind="extensible-arrow"/);
  assert.ok(paint('HorizontalBrackets').includes('note') && paint('HorizontalBrackets').includes('label'));
  assert.equal(paint('CD'), 'A←aB→bCcdDE→F');
  assert.doesNotMatch(paint('TextWithMath'), /\$/);
  assert.ok(paint('Verb').includes('&amp;'), 'verbatim ampersands must survive matrix parsing');
  const verbatimLetters = [...svg('Verb').matchAll(/<text\b([^>]*)>([^<]*)<\/text>/gu)]
    .filter((m) => /[A-Za-z]/.test(m[2]));
  assert.ok(verbatimLetters.length > 0);
  for (const match of verbatimLetters) assert.match(match[1], /font-family="Computer Modern Typewriter"/,
    `verbatim glyph ${match[2]} must paint in the selected typewriter face`);
  assert.match(svg('Colorbox'), /fill="red" stroke="blue"/);
  for (const name of names) {
    const png = PNG.sync.read(fs.readFileSync(path.join(dir, `cases/${name}/lambda.png`)));
    const ink = png.data.some((v, i) => i % 4 !== 3 && v < 128);
    assert.ok(ink && png.width > 10 && png.height > 10, `${name} must paint visible native pixels`);
  }
});
