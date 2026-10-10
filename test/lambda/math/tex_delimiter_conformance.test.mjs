// TeX82 var_delimiter oracle: actual TeX boxes and shipped DVI component baselines.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { parse_lambda_json } from './lambda_math_renderer.mjs';
import { reference_environment } from './texcmp_reference.mjs';
import { read_dvi } from './tex_dvi.mjs';

const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const missing = ['pdflatex', 'kpsewhich'].filter((tool) =>
  spawnSync(tool, ['--version']).status !== 0);

test('bundled delimiters agree with TeX boxes and shipped components', {
  skip: missing.length ? `missing tools: ${missing.join(', ')}` : false,
}, async (t) => {
  fs.mkdirSync(path.join(ROOT, 'temp'), { recursive: true });
  const dir = fs.mkdtempSync(path.join(ROOT, 'temp', 'math-delimiter-oracle-'));
  t.diagnostic(`artifacts: ${dir}`);
  const cases = [];
  const delimiters = ['(', ')', '[', ']', '\\lfloor', '\\rfloor', '\\lceil', '\\rceil',
    '\\{', '\\}', '\\langle', '\\rangle', '|', '\\Vert', '/', '\\backslash',
    '\\uparrow', '\\downarrow', '\\updownarrow', '\\Uparrow', '\\Downarrow', '\\Updownarrow',
    '\\lgroup', '\\rgroup', '\\lmoustache', '\\rmoustache'];
  for (const delimiter of delimiters) for (const style of ['display', 'text', 'script', 'scriptscript']) {
    for (const size of ['big', 'Big', 'bigg', 'Bigg'])
      cases.push({ delimiter, source: `\\${style}style\\${size}l${delimiter}`, name: `${style} ${size} ${delimiter}` });
    for (const height of [0.05, 1.3, 6])
      cases.push({ delimiter, source: `\\${style}style\\left${delimiter}\\vphantom{\\rule{0pt}{${height * 10}pt}}\\right.`,
        name: `${style} automatic ${delimiter}, ${height}em` });
  }
  for (const delimiter of ['(', '[', '\\{', '\\langle', '\\Vert', '\\uparrow'])
    for (const style of ['display', 'text', 'script', 'scriptscript']) {
      const body = '\\vphantom{\\rule{0pt}{60pt}}';
      for (const [kind, expression] of [
        ['cramped', `\\overline{\\left${delimiter}${body}\\right.}`],
        ['middle', `\\left${delimiter}${body}\\middle|\\right.`],
        ['nested', `\\left${delimiter}\\left[${body}\\right]\\right.`],
      ]) cases.push({ delimiter, source: `\\${style}style${expression}`, name: `${style} ${kind} ${delimiter}` });
    }
  for (const style of ['display', 'text', 'script', 'scriptscript'])
    for (const source of [String.raw`\left.\right.`, String.raw`\left(\middle|\right)`,
      String.raw`\left(\scriptstyle\vphantom{\rule{0pt}{60pt}}\middle|\right)`,
      String.raw`\left(\scriptstyle\vphantom{\rule{0pt}{60pt}}\middle|\vphantom{\rule{0pt}{60pt}}\right)`,
      String.raw`\left(\scriptstyle1\vphantom{\rule{0pt}{60pt}}\middle|1\right)`])
      cases.push({ source: `\\${style}style${source}`, name: `${style} boundary ${source}` });
  const script = `import math: lambda.doc.math.math
import font: lambda.doc.math.font
import svg: ~~.~~.test.lambda.math.mod_svg_snapshot
let cases = parse(${JSON.stringify(JSON.stringify(cases))}, 'json')^
format([for (item in cases) (
 let ast = parse(item.source,{type:"math",flavor:"latex"})^,
 let b = math.render_box(ast,{font_size:960.0 / 72.27})^,
 let p = font.prepare(ast,{})^,
 {width:b.width,height:b.height,depth:b.depth,
 glyphs:[for (g in svg.geometry(b.element) where g.text != "") (
  let data = [for (piece in p.delimiter_data.pieces where piece.codepoint == ord(g.text) and piece.family == g.node["font-family"]) piece][0],
  {text:g.text,family:g.node["font-family"],size:g.node["font-size"],sx:g.sx,sy:g.sy,
   x:g.x / 1000.0,y:(g.y - (data.shift or 0.0) * g.node["font-size"] / 1000.0) / 1000.0})]})], 'json')
`;
  const scriptPath = path.join(dir, '_probe.ls');
  fs.writeFileSync(scriptPath, script);
  const lambdaRun = spawnSync(path.join(ROOT, 'lambda.exe'), ['--no-log', scriptPath],
    { cwd: ROOT, encoding: 'utf8', timeout: 180000, maxBuffer: 32 * 1024 * 1024 });
  fs.writeFileSync(path.join(dir, 'lambda.log'), lambdaRun.stdout + lambdaRun.stderr);
  assert.ifError(lambdaRun.error); assert.equal(lambdaRun.status, 0, lambdaRun.stderr);
  const lambda = parse_lambda_json(lambdaRun.stdout);
  assert.equal(lambda.length, cases.length);

  // The declared family-0 companion is scaled CM10, not the optical cmr7/cmr5 designs.
  // Family 2 keeps CMSY10/7/5; family 3 keeps CMEX10 at 10pt in all styles, as plain TeX.
  const tex = String.raw`\documentclass[10pt]{article}
\usepackage{amsmath}
\font\OracleSeven=cmr10 at7pt
\font\OracleFive=cmr10 at5pt
\font\OracleExtension=cmex10 at10pt
\begin{document}
` + cases.map((c, i) => `\\setbox0=\\hbox{\\special{lambda-base}$` +
    '\\scriptfont0=\\OracleSeven \\scriptscriptfont0=\\OracleFive ' +
    '\\textfont3=\\OracleExtension \\scriptfont3=\\OracleExtension \\scriptscriptfont3=\\OracleExtension ' +
    `${c.source}$}\n\\typeout{DIM:${i}:\\the\\wd0:\\the\\ht0:\\the\\dp0}\n\\shipout\\box0\n`).join('') +
    '\\end{document}\n';
  fs.writeFileSync(path.join(dir, 'reference.tex'), tex);
  const texRun = spawnSync('pdflatex', ['-output-format=dvi', '-no-shell-escape', '-halt-on-error',
    '-interaction=nonstopmode', 'reference.tex'],
  { cwd: dir, env: reference_environment(dir), encoding: 'utf8', timeout: 60000, maxBuffer: 8 * 1024 * 1024 });
  fs.writeFileSync(path.join(dir, 'pdflatex.log'), texRun.stdout + texRun.stderr);
  assert.ifError(texRun.error); assert.equal(texRun.status, 0, texRun.stdout.slice(-3000) + texRun.stderr);
  const dimensions = new Map([...texRun.stdout.matchAll(/DIM:(\d+):([\d.-]+)pt:([\d.-]+)pt:([\d.-]+)pt/g)]
    .map((m) => [Number(m[1]), { width: Number(m[2]) / 10, height: Number(m[3]) / 10, depth: Number(m[4]) / 10 }]));
  const dvi = read_dvi(path.join(dir, 'reference.dvi'));
  assert.equal(dimensions.size, cases.length); assert.equal(dvi.pages.length, cases.length);
  const close = (a, b, name) => assert.ok(Number.isFinite(a) && Math.abs(a - b) < 0.0003,
    `${name}: Lambda ${a}, TeX ${b}`);
  for (const [i, c] of cases.entries()) await t.test(c.name, () => {
    const box = lambda[i], reference = dimensions.get(i), page = dvi.pages[i];
    for (const field of ['width', 'height', 'depth']) close(box[field], reference[field], field);
    const origin = page.markers.find((m) => m.value === 'lambda-base');
    assert.ok(origin, 'shipped baseline marker');
    assert.equal(box.glyphs.length, page.glyphs.length, 'component count');
    for (const [j, actual] of box.glyphs.entries()) {
      const expected = page.glyphs[j];
      close(actual.x, (expected.x - origin.x) / 655360, `component ${j} x`);
      close(actual.y, (expected.y - origin.y) / 655360, `component ${j} baseline`);
      close(actual.size / 1000, expected.size / 655360, `component ${j} size`);
      assert.equal(actual.sx, 1); assert.equal(actual.sy, 1, 'no anisotropic stretching');
      assert.match(actual.family, expected.font === 'cmex10' ? /^KaTeX_Size[1-4]$/ : /^(KaTeX_Main|Computer Modern Serif)$/);
    }
  });
  const hash = (file) => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  fs.writeFileSync(path.join(dir, 'evidence.json'), JSON.stringify({ cases, lambda,
    tex: [...dimensions], pages: dvi.pages, referenceVersion: texRun.stdout.split('\n').slice(0, 6),
    tfm: dvi.fonts.map(({ tfm, ...f }) => ({ ...f, sha256: hash(f.tfmPath) })),
    sources: Object.fromEntries(['lambda.exe', 'lmd/package/math/font.ls', 'lmd/package/math/typeset.ls',
      'lmd/package/math/tex_metrics.ls', 'lmd/package/math/stretch.ls',
      'test/lambda/math/tex_delimiter_conformance.test.mjs', 'test/lambda/math/tex_dvi.mjs']
      .map((file) => [file, hash(path.join(ROOT, file))])),
  }, null, 2));
});
