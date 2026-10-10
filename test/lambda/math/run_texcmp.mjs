#!/usr/bin/env node
// D7.2.4: exercise lambda.doc.math, then compare native PNGs with real pdfLaTeX.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import yaml from 'js-yaml';
import { render_lambda_math } from './lambda_math_renderer.mjs';
import { read_ink, compare_ink, write_ink, write_overlay } from './texcmp_pixels.mjs';
import { reference_environment, reference_preamble, reference_tex, stage_reference_assets } from './texcmp_reference.mjs';
import { prepare_reference } from './texcmp_dialect.mjs';

const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const TEMP = path.join(ROOT, 'temp');
const SOURCE = 'https://github.com/KaTeX/KaTeX/blob/main/test/screenshotter/ss_data.yaml';

function arguments_for(argv) {
  const opts = {
    data: fileURLToPath(new URL('./ss_data.yaml', import.meta.url)),
    lambda: path.join(ROOT, 'lambda.exe'), out: path.join(TEMP, 'math_texcmp'),
    cases: [], limit: null, list: false, pixels: 64, radius: 12, tolerance: 16,
    timeout: 60000, maxInkError: null, preamble: null,
  };
  const values = { '--data': 'data', '--lambda': 'lambda', '--out': 'out',
    '--limit': 'limit', '--font-pixels': 'pixels', '--align-radius': 'radius',
    '--pixel-tolerance': 'tolerance', '--timeout': 'timeout',
    '--max-ink-error': 'maxInkError', '--preamble': 'preamble' };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--help' || arg === '-h') {
      console.log(`Usage: node test/lambda/math/run_texcmp.mjs [options]
  --case NAME          Select a named YAML case (repeatable; default: all)
  --limit N            Run only the first N selected cases
  --list               List cases, including upstream nolatex reasons
  --out DIR            Artifact parent beneath ./temp (default: temp/math_texcmp)
  --lambda PATH        Lambda executable (default: ./lambda.exe)
  --data PATH          KaTeX YAML corpus (default: adjacent ss_data.yaml)
  --font-pixels N      Pixels per 10pt reference em (default: 64)
  --align-radius N     Maximum translation in pixels per axis (default: 12)
  --pixel-tolerance N  Grayscale delta for mismatch counts (default: 16)
  --max-ink-error N    Optional [0,1] error threshold; differences then fail
  --timeout MS         Timeout for each external command (default: 60000)
  --preamble PATH      Additional LaTeX preamble, after texcmp_preamble.tex

Without --max-ink-error, visual differences are reported for review.
Rendering errors exit 1; upstream nolatex cases are explicit skips.`);
      process.exit(0);
    } else if (arg === '--list') opts.list = true;
    else if (arg === '--case') {
      if (!argv[i + 1] || argv[i + 1].startsWith('--')) throw new Error('missing value for --case');
      opts.cases.push(argv[++i]);
    } else if (values[arg]) {
      if (!argv[i + 1] || argv[i + 1].startsWith('--')) throw new Error(`missing value for ${arg}`);
      opts[values[arg]] = argv[++i];
    } else throw new Error(`unknown argument: ${arg}`);
  }
  for (const key of ['pixels', 'radius', 'tolerance', 'timeout', 'limit', 'maxInkError']) {
    if (opts[key] == null) continue;
    opts[key] = Number(opts[key]);
    if (!Number.isFinite(opts[key])) throw new Error(`invalid numeric option: ${key}`);
  }
  for (const key of ['radius', 'tolerance', 'timeout', 'limit'])
    if (opts[key] != null && (!Number.isInteger(opts[key]) || opts[key] < (key === 'radius' || key === 'tolerance' ? 0 : 1)))
      throw new Error(`invalid integer option: ${key}`);
  if (opts.pixels <= 0 || opts.tolerance > 255 ||
      (opts.maxInkError != null && (opts.maxInkError < 0 || opts.maxInkError > 1)))
    throw new Error('font pixels must be positive, tolerance <= 255, and ink error in [0,1]');
  for (const key of ['data', 'lambda', 'out', 'preamble'])
    if (opts[key] != null) opts[key] = path.resolve(opts[key]);
  return opts;
}

function read_cases(opts) {
  const raw = fs.readFileSync(opts.data);
  const entries = yaml.load(raw.toString('utf8'), { schema: yaml.JSON_SCHEMA });
  if (!entries || typeof entries !== 'object' || Array.isArray(entries))
    throw new Error('corpus must be a YAML mapping');
  let cases = Object.entries(entries).map(([name, value]) => {
    const item = typeof value === 'string' ? { tex: value } : value;
    if (!/^[A-Za-z0-9_-]+$/.test(name) || !item || typeof item.tex !== 'string')
      throw new Error(`invalid corpus case: ${name}`);
    return { ...item, name };
  });
  const unknown = opts.cases.filter((name) => !cases.some((item) => item.name === name));
  if (unknown.length) throw new Error(`unknown cases: ${unknown.join(', ')}`);
  if (opts.cases.length) cases = cases.filter((item) => opts.cases.includes(item.name));
  if (opts.limit != null) cases = cases.slice(0, opts.limit);
  if (!cases.length) throw new Error('no formulas selected');
  return { cases, sha256: createHash('sha256').update(raw).digest('hex') };
}

function command(executable, args, cwd, opts, log) {
  const result = spawnSync(executable, args, {
    cwd, encoding: 'utf8', timeout: opts.timeout, maxBuffer: 64 * 1024 * 1024,
    env: opts.env,
  });
  if (log) fs.writeFileSync(log, `$ ${[executable, ...args].join(' ')}\n${result.stdout || ''}${result.stderr || ''}`);
  if (result.error) throw new Error(`${path.basename(executable)}: ${result.error.message}`);
  if (result.status !== 0) {
    const detail = (result.stdout + result.stderr).trim().slice(-1400);
    throw new Error(`${path.basename(executable)} exited ${result.status ?? result.signal}: ${detail}`);
  }
  return result.stdout + result.stderr;
}

function macro_definitions(macros = {}) {
  return Object.entries(macros).map(([name, expansion]) => {
    if (!/^\\[A-Za-z]+$/.test(name) || typeof expansion !== 'string')
      throw new Error(`invalid fixture macro: ${name}`);
    const parameters = expansion.replace(/##/g, '').match(/#[1-9]/g) || [];
    const count = Math.max(0, ...parameters.map((p) => Number(p[1])));
    return `\\def${name}${Array.from({ length: count }, (_, i) => `#${i + 1}`).join('')}{${expansion}}`;
  }).join('\n');
}

function render_case(item, opts) {
  const dir = path.join(opts.run, 'cases', item.name);
  fs.mkdirSync(dir);
  fs.writeFileSync(path.join(dir, 'case.json'), JSON.stringify(item, null, 2) + '\n');
  const result = { name: item.name, formula: item.tex, display: !!item.display,
    source_context: { pre: item.pre, post: item.post, styles: item.styles } };
  let stage = 'lambda';
  try {
    const definitions = macro_definitions(item.macros);
    const rendered = render_lambda_math(`${definitions}\n${item.tex}`, {
      display: !!item.display, lambda: opts.lambda, timeout: opts.timeout,
      script: path.join(dir, 'formula.ls'),
      base_uri: dir,
    });
    fs.writeFileSync(path.join(dir, 'lambda.json'), JSON.stringify(rendered, null, 2) + '\n');
    if (rendered.error !== 'no-error') throw new Error(rendered.error || 'Lambda returned no rendering');
    fs.writeFileSync(path.join(dir, 'lambda.svg'), rendered.html);
    const root = rendered.html.slice(0, rendered.html.indexOf('>'));
    const size = ['width', 'height'].map((name) => {
      const match = root.match(new RegExp(`\\b${name}="([0-9.]+)em"`));
      if (!match || !Number.isFinite(Number(match[1]))) throw new Error(`SVG missing ${name} in em`);
      return Math.max(1, Math.ceil(Number(match[1]) * opts.pixels)) + 64;
    });
    fs.writeFileSync(path.join(dir, 'lambda.html'), `<!doctype html><html><head><meta charset="utf-8">
<style>html,body{margin:0;background:white;color:black;}body{padding:32px;font-size:${opts.pixels}px;}svg{display:block;}</style>
</head><body>${rendered.html}</body></html>`);
    command(opts.lambda, ['--no-log', 'render', path.join(dir, 'lambda.html'), '-o',
      path.join(dir, 'lambda.raw.png'), '-vw', String(size[0]), '-vh', String(size[1])],
    ROOT, opts, path.join(dir, 'lambda-render.log'));
    stage = 'pdflatex';
    result.reference = prepare_reference(item.tex, definitions);
    fs.writeFileSync(path.join(dir, 'reference.formula.tex'), result.reference.tex);
    fs.writeFileSync(path.join(dir, 'reference.tex'), reference_tex(item, definitions, opts.preambleText, result.reference));
    command('pdflatex', ['-no-shell-escape', '-halt-on-error', '-interaction=nonstopmode', 'reference.tex'],
      dir, opts, path.join(dir, 'pdflatex.log'));
    const info = command('pdfinfo', ['reference.pdf'], dir, opts, path.join(dir, 'pdfinfo.log'));
    if (!/^Pages:\s+1\s*$/m.test(info)) throw new Error('reference PDF must have exactly one page');
    // 72.27 TeX points/inch: a 10pt em and Lambda's CSS em get equal pixel scales.
    command('pdftoppm', ['-png', '-singlefile', '-r', String(opts.pixels * 72.27 / 10),
      'reference.pdf', 'pdflatex.raw'], dir, opts, path.join(dir, 'rasterize.log'));
    stage = 'comparison';
    const lambda = read_ink(path.join(dir, 'lambda.raw.png'));
    const reference = read_ink(path.join(dir, 'pdflatex.raw.png'));
    write_ink(path.join(dir, 'lambda.png'), lambda);
    write_ink(path.join(dir, 'pdflatex.png'), reference);
    const comparison = compare_ink(lambda, reference, opts);
    write_overlay(path.join(dir, 'diff.png'), comparison.overlay);
    Object.assign(result, comparison.metrics, {
      status: opts.maxInkError != null && comparison.metrics.ink_error > opts.maxInkError ? 'mismatch' : 'compared',
      artifacts: Object.fromEntries(['lambda.png', 'pdflatex.png', 'diff.png', 'reference.pdf', 'lambda.svg']
        .map((name) => [name, `cases/${item.name}/${name}`])),
    });
  } catch (error) { Object.assign(result, { status: 'error', stage, error: error.message }); }
  return result;
}

function escape_html(value) {
  return String(value).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function html_report(report) {
  return `<!doctype html><html><head><meta charset="utf-8"><title>Lambda / pdfLaTeX math comparison</title>
<style>body{font:16px system-ui;margin:32px;color:#18202a}article{border-top:1px solid #ccc;padding:16px 0}pre{white-space:pre-wrap;overflow-wrap:anywhere}figure{margin:0;overflow:auto}figcaption{font-weight:600;margin:8px 0}img{max-width:none}.images{display:grid;gap:12px}code{background:#eee;padding:2px 4px}</style></head><body>
<h1>Lambda / pdfLaTeX math comparison</h1><p>${escape_html(JSON.stringify(report.summary))}</p>
<p>Formula-only comparison. Surrounding upstream HTML and CSS are retained in JSON, but excluded from rendering.
Equal em scales; white margins cropped; translation only. Black: overlap. Red: Lambda only. Green: pdfLaTeX only.
Ink error measures grayscale difference divided by union ink mass; it is not a semantic correctness score.</p>
${report.results.map((r) => `<article><h2>${escape_html(r.name)}: ${r.status}</h2><pre>${escape_html(r.formula)}</pre>
${r.reference?.features.length ? `<p>Supplementary reference features: ${escape_html(r.reference.features.join(', '))}.</p>` : ''}
${r.reference?.changes.length ? `<details><summary>pdfLaTeX syntax translations (${r.reference.changes.length})</summary><pre>${escape_html(JSON.stringify(r.reference.changes, null, 2))}</pre></details>` : ''}
${r.artifacts ? `<p>Ink error ${(r.ink_error * 100).toFixed(2)}%; mismatched ink pixels ${(r.mismatch_fraction * 100).toFixed(2)}%; offset (${r.offset.x}, ${r.offset.y})${r.alignment_at_boundary ? '; alignment reached search boundary' : ''}.</p>
<div class="images">${['lambda.png', 'pdflatex.png', 'diff.png'].map((name) => `<figure><figcaption>${escape_html(name)}</figcaption><img src="${escape_html(r.artifacts[name])}" alt="${escape_html(r.name + ' ' + name)}"></figure>`).join('')}</div>`
    : `<pre>${escape_html(r.error || r.reason)}</pre>`}</article>`).join('\n')}
</body></html>`;
}

function main() {
  const opts = arguments_for(process.argv.slice(2));
  const corpus = read_cases(opts);
  if (opts.list) {
    for (const item of corpus.cases) console.log(`${item.name}${item.nolatex ? `\tSKIP: ${item.nolatex}` : ''}`);
    console.log(`${corpus.cases.length} formulas`);
    return;
  }
  fs.mkdirSync(TEMP, { recursive: true });
  // Resolve ancestors before creating output so symlinks cannot redirect temp files.
  let ancestor = opts.out;
  while (!fs.existsSync(ancestor)) ancestor = path.dirname(ancestor);
  const realOut = path.resolve(fs.realpathSync(ancestor), path.relative(ancestor, opts.out));
  const relative = path.relative(fs.realpathSync(TEMP), realOut);
  if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('--out must be beneath ./temp');
  fs.mkdirSync(opts.out, { recursive: true });
  opts.run = fs.mkdtempSync(path.join(opts.out, 'run-'));
  stage_reference_assets(opts.run);
  opts.scratch = path.join(opts.run, '.work');
  fs.mkdirSync(opts.scratch);
  opts.env = reference_environment(opts.scratch);
  opts.preambleText = opts.preamble ? fs.readFileSync(opts.preamble, 'utf8') : '';
  const tools = Object.fromEntries(['pdflatex', 'pdftoppm', 'pdfinfo'].map((name) =>
    [name, command(name, [name === 'pdflatex' ? '--version' : '-v'], ROOT, opts).split('\n')[0]]));
  const report = {
    source: SOURCE, corpus: opts.data, corpus_sha256: corpus.sha256,
    lambda: opts.lambda, lambda_sha256: createHash('sha256').update(fs.readFileSync(opts.lambda)).digest('hex'),
    created_at: new Date().toISOString(), tools,
    comparison: { scope: 'formula only', font_pixels: opts.pixels, reference_font_pt: 10,
      reference_dpi: opts.pixels * 72.27 / 10, alignment_radius: opts.radius,
      pixel_tolerance: opts.tolerance, max_ink_error: opts.maxInkError,
      reference_preamble, preamble: opts.preambleText, texmf_home: opts.env.TEXMFHOME,
      grayscale: true },
    results: [],
  };
  for (const item of corpus.cases) {
    const result = item.nolatex
      ? { name: item.name, formula: item.tex, status: 'skipped', reason: String(item.nolatex) }
      : render_case(item, opts);
    report.results.push(result);
    console.log(`${result.status.toUpperCase()} ${item.name}: ${result.ink_error == null
      ? result.error || result.reason : `ink error ${(result.ink_error * 100).toFixed(2)}%`}`);
  }
  report.summary = {
    total: report.results.length,
    compared: report.results.filter((r) => r.ink_error != null).length,
    skipped: report.results.filter((r) => r.status === 'skipped').length,
    errors: report.results.filter((r) => r.status === 'error').length,
    visual_differences: report.results.filter((r) => r.ink_error > 0).length,
    threshold_failures: report.results.filter((r) => r.status === 'mismatch').length,
  };
  fs.writeFileSync(path.join(opts.run, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  fs.writeFileSync(path.join(opts.run, 'index.html'), html_report(report));
  console.log(`Summary: ${JSON.stringify(report.summary)}`);
  console.log(`Report: ${path.relative(ROOT, path.join(opts.run, 'index.html'))}`);
  if (report.summary.errors || report.summary.threshold_failures) process.exitCode = 1;
}

try { main(); }
catch (error) { console.error(`math-texcmp: ${error.message}`); process.exitCode = 1; }
