import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import yaml from 'js-yaml';
import { read_ink, compare_ink } from './texcmp_pixels.mjs';
import { reference_environment, reference_tex, stage_reference_assets } from './texcmp_reference.mjs';

const TEMP = fileURLToPath(new URL('../../../temp/', import.meta.url));
const corpus = yaml.load(fs.readFileSync(new URL('./ss_data.yaml', import.meta.url), 'utf8'));
const unavailable = ['pdflatex', 'pdfinfo', 'pdftoppm'].filter((tool) =>
  spawnSync(tool, [tool === 'pdflatex' ? '--version' : '-v']).status !== 0);

test('pdfLaTeX references compile and rasterize', {
  skip: unavailable.length ? `missing tools: ${unavailable.join(', ')}` : false,
}, async (t) => {
  fs.mkdirSync(TEMP, { recursive: true });
  const run = fs.mkdtempSync(path.join(TEMP, 'mathcmp-reference-tests-'));
  stage_reference_assets(run);
  const env = reference_environment(run);
  t.diagnostic(`artifacts: ${run}`);

  function compile(name, item, definitions = '', preamble = '') {
    const dir = path.join(run, 'cases', name);
    fs.mkdirSync(dir);
    fs.writeFileSync(path.join(dir, 'reference.tex'), reference_tex(item, definitions, preamble));
    const result = spawnSync('pdflatex', ['-no-shell-escape', '-halt-on-error',
      '-interaction=nonstopmode', 'reference.tex'], { cwd: dir, env, encoding: 'utf8', timeout: 15000 });
    fs.writeFileSync(path.join(dir, 'pdflatex.log'), (result.stdout || '') + (result.stderr || ''));
    return { dir, ...result };
  }

  function check_pdf(result) {
    assert.ifError(result.error);
    assert.equal(result.status, 0, (result.stdout + result.stderr).slice(-2400));
    const info = spawnSync('pdfinfo', ['reference.pdf'], { cwd: result.dir, env, encoding: 'utf8' });
    assert.equal(info.status, 0, info.stderr);
    assert.match(info.stdout, /^Pages:\s+1\s*$/m);
    const png = spawnSync('pdftoppm', ['-png', '-singlefile', '-r', '72.27',
      'reference.pdf', 'reference'], { cwd: result.dir, env, encoding: 'utf8', timeout: 15000 });
    assert.equal(png.status, 0, png.stderr);
    const ink = read_ink(path.join(result.dir, 'reference.png'));
    assert.ok(ink.width > 1 && ink.height > 1);
  }

  // Exercise upstream fixtures, including all fourteen remaining reference failures.
  const names = ['Arrays', 'BoldSpacing', 'Boxed', 'Cases', 'CD', 'DelimiterSizing',
    'ExtensibleArrows', 'GreekUnicode', 'HorizontalBrackets', 'Lap', 'Mapsfrom', 'MathAtom',
    'MathDefaultFonts', 'MathBb', 'MathBf', 'MathCal', 'MathIt', 'MathNormal',
    'MathRm', 'MathSf', 'MathSfIt', 'MathtoolsMatrix', 'MathTt', 'NegativeSpace',
    'OverUnderline', 'Phantom', 'Phase', 'Reflectbox', 'RlapBug', 'StrikeThrough',
    'AccentsText', 'Includegraphics', 'MathFrak', 'MathScr', 'SurrogatePairs',
    'RelativeUnits', 'SvgReset', 'Verb',
    'Integrands', 'LowerAccent', 'ReactionArrows', 'StretchyAccent', 'StretchyAccentColor', 'Unicode',
    'StrikeThroughColor', 'Symbols1', 'Align', 'DisplayMode', 'Tag', 'TextSpace'];
  for (const name of names) await t.test(name, () => {
    const value = corpus[name];
    assert.ok(value, `unknown fixture: ${name}`);
    check_pdf(compile(name, typeof value === 'string' ? { tex: value } : value));
  });

  await t.test('verbatim special characters survive box capture', () => {
    check_pdf(compile('VerbatimCapture', { tex: String.raw`\verb|A # % & \foo { }|` }));
  });
  await t.test('trailing comments and custom definitions compile', () => {
    check_pdf(compile('CustomDefinitions', { tex: String.raw`\fixture + \custom % final comment` },
      String.raw`\def\fixture{x^2}`, String.raw`\newcommand{\custom}{\sqrt{y}}`));
  });
  await t.test('mu rules scale with math style while em rules retain their text size', () => {
    const result = compile('RuleDimensions', { tex: String.raw`\CheckRule{\textstyle}{text}
      \CheckRule{\scriptstyle}{script}\CheckRule{\scriptscriptstyle}{scriptscript}x` }, '', String.raw`
      \newcommand\CheckRule[2]{\begingroup
        \setbox0=\hbox{$#1\ReferenceRule{18mu}{1mu}$}\typeout{RULE-#2-MU=\the\wd0}
        \setbox0=\hbox{$#1\ReferenceRule{1em}{1em}$}\typeout{RULE-#2-EM=\the\wd0}
        \setbox0=\hbox{$#1\mkern18mu$}\typeout{NATIVE-#2-MU=\the\wd0}
        \setbox0=\hbox{$#1\kern1em$}\typeout{NATIVE-#2-EM=\the\wd0}
        \endgroup}`);
    check_pdf(result);
    const dimensions = {};
    for (const style of ['text', 'script', 'scriptscript']) {
      for (const unit of ['MU', 'EM']) {
        const values = ['RULE', 'NATIVE'].map((kind) => {
          const match = result.stdout.match(new RegExp(`${kind}-${style}-${unit}=([\\d.]+)pt`));
          assert.ok(match, `${kind} ${style} ${unit} dimension missing`);
          return Number(match[1]);
        });
        // Native mu kern conversion truncates each mu to scaled points before multiplication.
        assert.ok(Math.abs(values[0] - values[1]) < 0.001, `${style} ${unit}: ${values}`);
        dimensions[`${style}-${unit}`] = values[0];
      }
    }
    assert.ok(dimensions['text-MU'] > dimensions['script-MU']);
    assert.ok(dimensions['script-MU'] > dimensions['scriptscript-MU']);
    assert.equal(dimensions['text-EM'], dimensions['scriptscript-EM']);
  });
  await t.test('alphabet fallbacks retain the ordinary glyphs outside their repertoire', () => {
    const ordinary = compile('OrdinaryAlphabet', { tex: String.raw`x2\breve{a}\omega\Omega\imath` });
    const script = compile('ScriptFallback', { tex: String.raw`\mathscr{x2\breve{a}\omega\Omega\imath}` });
    const fraktur = compile('FrakturFallback', { tex: String.raw`\mathfrak{\omega\Omega\imath}` });
    const symbols = compile('OrdinarySymbols', { tex: String.raw`\omega\Omega\imath` });
    for (const result of [ordinary, script, fraktur, symbols]) check_pdf(result);
    for (const [left, right] of [[ordinary, script], [symbols, fraktur]]) {
      const compared = compare_ink(read_ink(path.join(left.dir, 'reference.png')),
        read_ink(path.join(right.dir, 'reference.png')), { radius: 0 });
      assert.equal(compared.metrics.ink_error, 0);
    }
  });
  await t.test('Unicode alphabet glyphs retain their style inside text and roman math', () => {
    // Individual glyphs avoid comparing text-mode spacing with math italic corrections.
    for (const [index, glyph] of [...'𝐀𝑎𝑨𝗔𝘼'].entries()) {
      const ordinary = compile(`UnicodeMath${index}`, { tex: glyph });
      check_pdf(ordinary);
      for (const [name, tex] of [['UnicodeText', `\\text{${glyph}}`], ['UnicodeRoman', `\\mathrm{${glyph}}`]]) {
        const nested = compile(`${name}${index}`, { tex });
        check_pdf(nested);
        assert.doesNotMatch(nested.stdout, /Font shape .OT1\/(?:cmss|lmss)\/.*undefined/);
        const compared = compare_ink(read_ink(path.join(ordinary.dir, 'reference.png')),
          read_ink(path.join(nested.dir, 'reference.png')), { radius: 0 });
        assert.equal(compared.metrics.ink_error, 0, `${name}: ${glyph}`);
      }
    }
  });
  await t.test('undefined commands remain fatal', () => {
    const result = compile('UndefinedCommand', { tex: String.raw`\UndefinedReferenceCommand` });
    assert.equal(result.status, 1);
    assert.match(result.stdout, /Undefined control sequence/);
  });
  await t.test('missing glyphs remain fatal', () => {
    const result = compile('MissingGlyph', { tex: String.raw`\text{\font\missing=cmr10 \missing\char255}` });
    assert.equal(result.status, 1);
    assert.match(result.stdout, /Missing character/);
  });
});
