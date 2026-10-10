// Execute TeX independently and retain shipped geometry plus exact resource provenance.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { parse_lambda_json } from './lambda_math_renderer.mjs';
import { reference_environment } from './texcmp_reference.mjs';
import { read_dvi } from './tex_dvi.mjs';

const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
export const missing_tex_tools = ['pdflatex', 'kpsewhich'].filter((tool) =>
  spawnSync(tool, ['--version']).status !== 0);

export function tex_geometry(cases, prefix, sourceFiles) {
  fs.mkdirSync(path.join(ROOT, 'temp'), { recursive: true });
  const dir = fs.mkdtempSync(path.join(ROOT, 'temp', prefix));
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
  let small = [for (recipe in p.delimiter_data.recipes where recipe.codepoint == ord(g.text) and g.node["font-family"] == "KaTeX_Main") recipe.small_shift][0],
  {text:g.text,family:g.node["font-family"],size:g.node["font-size"],sx:g.sx,sy:g.sy,
   x:g.x / 1000.0,y:(g.y - (data.shift or small or 0.0) * g.node["font-size"] / 1000.0) / 1000.0})],
 rules:[for (g in svg.geometry(b.element) where name(g.node) == 'rect' and g.node.width > 0 and g.node.height > 0)
   {x:g.x / 1000.0,y:g.y / 1000.0,width:g.node.width * g.sx / 1000.0,height:g.node.height * g.sy / 1000.0}]})], 'json')
`;
  const scriptPath = path.join(dir, '_probe.ls');
  fs.writeFileSync(scriptPath, script);
  const lambdaRun = spawnSync(path.join(ROOT, 'lambda.exe'), ['--no-log', scriptPath],
    { cwd: ROOT, encoding: 'utf8', timeout: 180000, maxBuffer: 32 * 1024 * 1024 });
  fs.writeFileSync(path.join(dir, 'lambda.log'), lambdaRun.stdout + lambdaRun.stderr);
  assert.ifError(lambdaRun.error); assert.equal(lambdaRun.status, 0, lambdaRun.stderr);
  const lambda = parse_lambda_json(lambdaRun.stdout);
  assert.equal(lambda.length, cases.length);

  // Match the declared companion: scaled CM10 roman, CMSY10/7/5, fixed CMEX10.
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
  const hash = (file) => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const referencePackages = ['amsmath.sty', 'fontmath.ltx'].map((name) => {
    const lookup = spawnSync('kpsewhich', [name], { encoding:'utf8' });
    assert.equal(lookup.status, 0, `reference definition ${name}`);
    const file = lookup.stdout.trim();
    return { name, path:file, sha256:hash(file) };
  });
  const resources = ['cmr10','cmsy10','cmsy7','cmsy5','cmex10'].map((name) =>
    `lmd/package/math/fonts/tex/${name}.tfm`).concat(['Main','Size1','Size2','Size3','Size4'].map((name) =>
    `lmd/package/math/fonts/KaTeX_${name}-Regular.woff2`));
  fs.writeFileSync(path.join(dir, 'evidence.json'), JSON.stringify({ cases, lambda,
    tex: [...dimensions], pages: dvi.pages, referenceVersion: texRun.stdout.split('\n').slice(0, 6),
    tfm: dvi.fonts.map(({ tfm, ...f }) => ({ ...f, sha256: hash(f.tfmPath) })),
    referencePackages, resources:Object.fromEntries(resources.map((file) => [file,hash(path.join(ROOT,file))])),
    sources: Object.fromEntries(['lambda.exe', 'lmd/package/math/font.ls', 'lmd/package/math/typeset.ls',
      'lmd/package/math/tex_metrics.ls', 'lmd/package/math/stretch.ls', 'lmd/package/math/svg_box.ls',
      'lmd/package/math/util.ls',
      'test/lambda/math/tex_geometry_oracle.mjs', 'test/lambda/math/tex_dvi.mjs', ...sourceFiles]
      .map((file) => [file, hash(path.join(ROOT, file))])),
  }, null, 2));
  return { dir, lambda, dimensions, dvi };
}

export function assert_tex_geometry(actual, reference, page, includeRules = false) {
  const close = (a, b, name) => assert.ok(Number.isFinite(a) && Math.abs(a - b) < 0.0003,
    `${name}: Lambda ${a}, TeX ${b}`);
  for (const field of ['width', 'height', 'depth']) close(actual[field], reference[field], field);
  const origin = page.markers.find((m) => m.value === 'lambda-base');
  assert.ok(origin, 'shipped baseline marker');
  assert.equal(actual.glyphs.length, page.glyphs.length, 'component count');
  for (const [j, glyph] of actual.glyphs.entries()) {
    const expected = page.glyphs[j];
    close(glyph.x, (expected.x - origin.x) / 655360, `component ${j} x`);
    close(glyph.y, (expected.y - origin.y) / 655360, `component ${j} baseline`);
    close(glyph.size / 1000, expected.size / 655360, `component ${j} size`);
    assert.equal(glyph.sx, 1); assert.equal(glyph.sy, 1, 'no anisotropic stretching');
    assert.match(glyph.family, expected.font === 'cmex10' ? /^KaTeX_Size[1-4]$/ : /^(KaTeX_Main|Computer Modern Serif)$/);
  }
  if (includeRules) {
    assert.equal(actual.rules.length, page.rules.length, 'painted rule count');
    const expected = page.rules.map((r) => ({ x:(r.x - origin.x) / 655360,
      y:(r.y - origin.y - r.height) / 655360,width:r.width / 655360,height:r.height / 655360 }));
    const order = (a, b) => a.x - b.x || a.y - b.y;
    const rules = actual.rules.toSorted(order); expected.sort(order);
    for (const [j, rule] of rules.entries())
      for (const field of ['x','y','width','height']) close(rule[field], expected[j][field], `rule ${j} ${field}`);
  }
}
