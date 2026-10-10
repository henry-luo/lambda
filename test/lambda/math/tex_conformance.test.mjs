// Independent AMS/LaTeX execution: compare glue deltas and shipped numerator positions,
// rather than absolute advances of different painted fonts or screenshot similarity.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { parse_lambda_json } from './lambda_math_renderer.mjs';
import { reference_environment } from './texcmp_reference.mjs';

const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const missing = spawnSync('pdflatex', ['--version']).status !== 0;
const styles = ['display', 'text', 'script', 'scriptscript'];

test('AMS primitives agree with independently executed TeX boxes and positions', {
  skip: missing ? 'missing tool: pdflatex' : false,
}, async (t) => {
  fs.mkdirSync(path.join(ROOT, 'temp'), { recursive: true });
  const dir = fs.mkdtempSync(path.join(ROOT, 'temp', 'math-conformance-'));
  t.diagnostic(`artifacts: ${dir}`);
  const cases = [];
  const checks = [];
  function add(source, context = {}) {
    const id = cases.length;
    cases.push({ id, source, display: false, style: 'text', size: '', ...context });
    return id;
  }
  function delta(name, left, right, context) {
    checks.push({ name, kind: 'delta', left: add(left, context), right: add(right, context) });
  }
  for (const display of [false, true]) for (const style of styles) {
    const context = { display, style };
    const label = `${display ? 'display mode' : 'inline mode'}, ${style} style`;
    for (const [name, left, right] of [
      ['binary modulo', String.raw`a\bmod b`, String.raw`a\mathbin{\mathrm{mod}}b`],
      ['initial modulo', String.raw`\bmod b`, String.raw`\mathrm{mod}b`],
      ['final modulo', String.raw`a\bmod`, String.raw`a\mathrm{mod}`],
      ['modulo before relation', String.raw`a\bmod=b`, String.raw`a\mathrm{mod}=b`],
      ['parenthesized modulo', String.raw`a\pmod{x}`, String.raw`a(\mathrm{mod}\mkern6mu x)`],
      ['parenthesized unary sign', String.raw`a\pod{+x}`, String.raw`a(+x)`],
      ['unparenthesized modulo', String.raw`a\mod{x}=b`, String.raw`a\mathrm{mod}\mkern6mu x=b`],
      ['modulo argument relation', String.raw`\mod{=}x`, String.raw`\mathrm{mod}\mkern6mu =x`],
      ['scoped pod style', String.raw`a\pod{\scriptstyle a+b}+c`, String.raw`a(\scriptstyle a+b)+c`],
      ['scoped pmod style', String.raw`a\pmod{\scriptstyle a+b}+c`, String.raw`a(\mathrm{mod}\mkern6mu\scriptstyle a+b)+c`],
      ['scoped mod style', String.raw`a\mod{\scriptstyle a+b}+c`, String.raw`a\mathrm{mod}\mkern6mu\scriptstyle a+b+c`],
    ]) delta(`${name}: ${label}`, left, right, context);
  }
  delta('array cells inherit the surrounding AMS display flag',
    String.raw`\begin{array}{c}\displaystyle a\pmod{x}\end{array}`,
    String.raw`\begin{array}{c}\displaystyle a(\mathrm{mod}\mkern6mu x)\end{array}`, { display: true });
  for (const style of styles) {
    const context = { style };
    delta(`mathstrut ordinary atom: ${style}`, String.raw`a\mathstrut+b`, String.raw`a+b`, context);
    delta(`phantom ordinary atom: ${style}`, String.raw`a\phantom\sum b`, String.raw`a\phantom{\sum}b`, context);
    delta(`smash ordinary atom: ${style}`, String.raw`a\smash+ b`, String.raw`a{+}b`, context);
    checks.push({ name: `parenthesis phantom: ${style}`, kind: 'strut',
      left: add(String.raw`\mathstrut`, context), right: add(String.raw`\vphantom(`, context) });
    delta(`sized relation spacing: ${style}`, String.raw`a\bigm\uparrow b`, String.raw`a\big\uparrow b`, context);
    for (const align of ['l', 'c', 'r']) {
      const source = `\\cfrac[${align}]{1}{12345}`;
      const marked = `\\cfrac[${align}]{\\markpos{num}1}{12345}`;
      checks.push({ name: `continued fraction ${align}: ${style}`, kind: 'fraction',
        left: add(source, { ...context, marked }), right: add(String.raw`\dfrac{1}{12345}`, context) });
    }
  }
  for (const size of ['tiny', 'small', 'large', 'Large', 'Huge']) {
    checks.push({ name: `continued fraction text strut: ${size}`, kind: 'fraction',
      left: add(String.raw`\cfrac[r]{1}{12345}`, { size, style: 'script',
        marked: String.raw`\cfrac[r]{\markpos{num}1}{12345}` }),
      right: add(String.raw`\dfrac{1}{12345}`, { size, style: 'script' }) });
  }

  const script = `import math: lambda.doc.math.math
import svg: ~~.~~.test.lambda.math.mod_svg_snapshot
let cases = parse(${JSON.stringify(JSON.stringify(cases))}, 'json')^
let results = [for (item in cases) (
    let source = (if (item.size != "") "\\\\" ++ item.size ++ " " else "") ++
        "\\\\" ++ item.style ++ "style " ++ item.source,
    let b = math.render_box(parse(source, {type:"math",flavor:"latex"})^,
        {display:item.display, font_size:960.0 / 72.27})^,
    {id:item.id, width:b.width, height:b.height, depth:b.depth,
        glyphs:[for (g in svg.geometry(b.element) where g.text != "")
            {text:g.text, x:g.x / 1000.0, y:g.y / 1000.0}]})];
format(results, 'json')
`;
  const scriptPath = path.join(dir, '_probe.ls');
  fs.writeFileSync(scriptPath, script);
  const lambdaRun = spawnSync(path.join(ROOT, 'lambda.exe'), ['--no-log', scriptPath],
    { cwd: ROOT, encoding: 'utf8', timeout: 60000, maxBuffer: 8 * 1024 * 1024 });
  fs.writeFileSync(path.join(dir, 'lambda.log'), lambdaRun.stdout + lambdaRun.stderr);
  assert.ifError(lambdaRun.error);
  assert.equal(lambdaRun.status, 0, lambdaRun.stderr);
  const lambda = parse_lambda_json(lambdaRun.stdout);
  assert.equal(lambda.length, cases.length);
  for (const result of lambda) for (const field of ['width', 'height', 'depth'])
    assert.ok(Number.isFinite(result[field]), `case ${result.id} missing ${field}`);

  // The bundled companion uses scaled CM10 text parameters at named sizes, rather than
  // LaTeX's optical-size cmsy5/9/etc. Match that declared profile for the size-only probes.
  const sizedFonts = String.raw`\font\OracleRoman=cmr10 at\f@size pt
\font\OracleSymbols=cmsy10 at\f@size pt
\font\OracleExtension=cmex10 at\f@size pt
`;
  const tex = String.raw`\documentclass[10pt]{article}
\usepackage{amsmath}
\makeatletter
\newcommand{\markpos}[1]{\pdfsavepos\write16{POS:\caseid:#1:\the\pdflastxpos:\the\pdflastypos}}
\begin{document}
` + cases.map((c) => String.raw`\begingroup` +
    (c.size ? `\\${c.size}\n${sizedFonts}` : '') + `\n\\def\\caseid{${c.id}}\n` +
    `\\setbox0=\\hbox{$` + (c.size ? '\\textfont0=\\OracleRoman \\textfont2=\\OracleSymbols \\textfont3=\\OracleExtension ' : '') +
    `\\@display${c.display ? 'true' : 'false'}\\${c.style}style ` +
    `\\markpos{base}${c.marked ?? c.source}$}\n` +
    `\\typeout{DIM:${c.id}:\\the\\wd0:\\the\\ht0:\\the\\dp0}\n` +
    '\\shipout\\box0\n\\endgroup\n').join('') + '\\end{document}\n';
  fs.writeFileSync(path.join(dir, 'reference.tex'), tex);
  const texRun = spawnSync('pdflatex', ['-no-shell-escape', '-halt-on-error',
    '-interaction=nonstopmode', 'reference.tex'],
  { cwd: dir, env: reference_environment(dir), encoding: 'utf8', timeout: 30000, maxBuffer: 8 * 1024 * 1024 });
  fs.writeFileSync(path.join(dir, 'pdflatex.log'), texRun.stdout + texRun.stderr);
  assert.ifError(texRun.error);
  assert.equal(texRun.status, 0, texRun.stdout.slice(-3000) + texRun.stderr);
  const dimensions = new Map([...texRun.stdout.matchAll(/DIM:(\d+):([\d.]+)pt:([\d.]+)pt:([\d.]+)pt/g)]
    .map((m) => [Number(m[1]), { width: Number(m[2]) / 10, height: Number(m[3]) / 10, depth: Number(m[4]) / 10 }]));
  const positions = new Map([...texRun.stdout.matchAll(/POS:(\d+):(base|num):(\d+):(\d+)/g)]
    .map((m) => [`${m[1]}-${m[2]}`, { x: Number(m[3]) / 655360, y: Number(m[4]) / 655360 }]));
  assert.equal(dimensions.size, cases.length, 'every TeX box must report dimensions');
  function close(actual, expected, detail, tolerance = 0.0003) {
    assert.ok(Math.abs(actual - expected) < tolerance, `${detail}: Lambda ${actual}, TeX ${expected}`);
  }
  for (const check of checks) await t.test(check.name, () => {
    const l = lambda[check.left], r = lambda[check.right];
    const a = dimensions.get(check.left), b = dimensions.get(check.right);
    if (check.kind === 'strut') {
      for (const field of ['width', 'height', 'depth']) {
        close(l[field], r[field], `phantom ${field}`);
        // Optical-size font designs may differ; the macro's zero width and own-parenthesis relation may not.
        close(a[field], b[field], `TeX phantom ${field}`);
      }
      assert.deepEqual(l.glyphs, []);
    } else {
      close(l.width - r.width, a.width - b.width, 'advance delta');
      if (check.kind === 'fraction') {
        close(l.height, a.height, 'strut-controlled fraction height');
        const base = positions.get(`${check.left}-base`), num = positions.get(`${check.left}-num`);
        assert.ok(base && num, 'shipped numerator positions must be present');
        close(l.glyphs[0].x, num.x - base.x, 'painted numerator x');
        close(l.glyphs[0].y, base.y - num.y, 'painted numerator baseline');
      }
    }
  });
  const hash = (file) => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const amsPath = spawnSync('kpsewhich', ['amsmath.sty'], { encoding: 'utf8' });
  assert.equal(amsPath.status, 0, 'record the actual installed AMS definition');
  const amsFile = amsPath.stdout.trim();
  fs.writeFileSync(path.join(dir, 'evidence.json'), JSON.stringify({
    cases, lambda, tex: [...dimensions], positions: [...positions], checks,
    referenceVersion: texRun.stdout.split('\n').slice(0, 6),
    amsmath: { path: amsFile, sha256: hash(amsFile) },
    sources: Object.fromEntries(['lambda.exe', 'lmd/package/math/typeset.ls', 'lmd/package/math/font.ls',
      'test/lambda/math/tex_conformance.test.mjs'].map((file) => [file, hash(path.join(ROOT, file))])),
  }, null, 2));
});
