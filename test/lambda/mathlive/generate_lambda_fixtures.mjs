#!/usr/bin/env node
// Bake MathLive HTML fixtures for LaTeX expressions extracted from
// ./test/input/ documents.
//
// Input:  temp/lambda_mathlive_extracted.json (from extract_latex_exprs.mjs)
// Output: test/lambda/mathlive/__snapshots__/lambda_input_markup.snap
//
// Each output entry mirrors the existing markup.test.ts.snap format so the
// runner's parseSnapshots()/formulaFromSnapshotKey() pipeline can consume it:
//
//   exports[`<source-basename> <index>/ <formula> renders correctly 1`] = `
//   [
//     "<HTML>",
//     "<status>",
//   ]
//   `;
//
// Notes:
//   - Category = source-file basename (single token, no spaces) so the
//     runner's generic fallback regex /^[^ ]+ \d+\/ ... renders correctly 1$/
//     extracts the formula automatically.
//   - Display-mode expressions get wrapped in \[...\] so the runner's
//     normalizeFormula() flips display=true after extraction.
//   - Formula whitespace is normalized (newlines → space, runs collapsed).
//   - Class rename: ML__ → lm_ as the only post-processing on MathLive's HTML.
//   - defaultMode is left at MathLive's default ('math') since the bundle
//     does not visibly differentiate display vs inline at the HTML level.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const INPUT_PATH = path.join(PROJECT_ROOT, 'temp/lambda_mathlive_extracted.json');
const OUTPUT_PATH = path.join(
  PROJECT_ROOT,
  'test/lambda/mathlive/__snapshots__/lambda_input_markup.snap'
);
const MATHLIVE_SSR_PATH = path.join(
  PROJECT_ROOT,
  'ref/mathlive/dist/mathlive-ssr.min.mjs'
);

const { convertLatexToMarkup, validateLatex } = await import(MATHLIVE_SSR_PATH);

if (typeof convertLatexToMarkup !== 'function' || typeof validateLatex !== 'function') {
  console.error('MathLive SSR bundle missing expected exports.');
  process.exit(2);
}

// --- helpers ---------------------------------------------------------------

function normalizeFormula(latex) {
  // Collapse newlines + runs of whitespace; keep the math valid (LaTeX is
  // whitespace-insensitive outside \text{}).
  return latex.replace(/\s+/g, ' ').trim();
}

function categoryFor(sourcePath) {
  // basename without dots-as-separator: keep .tex / .md / .txt to disambiguate
  // same-stem files from different formats. Single token so the runner's
  // generic regex picks it up.
  return path.basename(sourcePath);
}

function applyClassRename(html) {
  return html.replace(/ML__/g, 'lm_');
}

function escapeForKey(s) {
  // Keys are extracted by the runner via regex then passed through
  // unescapeSnapshotText which does \\ -> \. So in the snap text we must
  // double backslashes so the runner re-collapses them. Backticks would
  // terminate the JS template literal — ban them.
  if (s.includes('`')) {
    throw new Error('backtick found in snap key; cannot encode');
  }
  return s.replace(/\\/g, '\\\\').replace(/\$\{/g, '\\${');
}

function escapeForValue(s) {
  // HTML and status strings are extracted by the runner via naive slice(3, -2)
  // with NO unescaping. So whatever appears in the snap text is what Lambda
  // must produce verbatim. The only thing we cannot allow is a backtick (would
  // terminate the surrounding JS template literal).
  if (s.includes('`')) {
    throw new Error('backtick found in snap value; cannot encode');
  }
  return s;
}

function statusFromValidation(errs) {
  if (!errs || errs.length === 0) return 'no-error';
  return errs[0].code;
}

// --- generation ------------------------------------------------------------

if (!fs.existsSync(INPUT_PATH)) {
  console.error(`Missing input ${INPUT_PATH}. Run extract_latex_exprs.mjs first.`);
  process.exit(2);
}

const records = JSON.parse(fs.readFileSync(INPUT_PATH, 'utf8'));
console.log(`Loaded ${records.length} expressions from ${path.relative(PROJECT_ROOT, INPUT_PATH)}`);

// Stable ordering: source then line.
records.sort((a, b) => a.source.localeCompare(b.source) || a.line - b.line);

// Per-category running index so keys look like
//   "math_simple.md 0/ \frac{a}{b} renders correctly 1"
//   "math_simple.md 1/ \int f(x) dx renders correctly 1"
const indexByCategory = new Map();

const entries = [];
const stats = {
  totalRecords: records.length,
  rendered: 0,
  noError: 0,
  withErrors: 0,
  byErrorCode: new Map(),
  byCategory: new Map(),
  renderFailures: 0,
};

for (const rec of records) {
  const category = categoryFor(rec.source);
  const idx = indexByCategory.get(category) ?? 0;
  indexByCategory.set(category, idx + 1);

  const normalized = normalizeFormula(rec.latex);

  // Validate (always — even if convertLatexToMarkup later throws).
  let errs = [];
  try {
    errs = validateLatex(normalized);
  } catch (e) {
    errs = [{ code: 'validator-threw', arg: String(e.message ?? e) }];
  }
  const status = statusFromValidation(errs);

  // Render. We feed the *unwrapped* formula to MathLive; the \[...\] in the
  // key is purely for the Lambda runner's display detection.
  let html = '';
  let renderFailed = false;
  try {
    html = convertLatexToMarkup(normalized);
  } catch (e) {
    renderFailed = true;
    html = `<!-- render threw: ${String(e.message ?? e).replace(/[<>]/g, '')} -->`;
  }
  const renamed = applyClassRename(html);

  // Display wrapping for the key (runner's normalizeFormula detects \[...\]).
  const keyFormula = rec.display ? `\\[${normalized}\\]` : normalized;

  // Compose key. The runner extracts via:
  //   /^[^ ]+ \d+\/ ([\s\S]*) renders correctly 1$/
  const key = `${category} ${idx}/ ${keyFormula} renders correctly 1`;

  entries.push({
    key,
    html: renamed,
    status,
    source: rec.source,
    line: rec.line,
    display: rec.display,
    env: rec.env,
  });

  stats.rendered++;
  if (status === 'no-error') stats.noError++;
  else stats.withErrors++;
  if (renderFailed) stats.renderFailures++;
  stats.byErrorCode.set(status, (stats.byErrorCode.get(status) ?? 0) + 1);
  stats.byCategory.set(category, (stats.byCategory.get(category) ?? 0) + 1);
}

// --- serialize -------------------------------------------------------------

function snapBlock(entry) {
  const k = escapeForKey(entry.key);
  const h = escapeForValue(entry.html);
  const s = escapeForValue(entry.status);
  return (
    `exports[\`${k}\`] = \`\n` +
    `[\n` +
    `  "${h}",\n` +
    `  "${s}",\n` +
    `]\n` +
    `\`;\n\n`
  );
}

const HEADER = '// Jest Snapshot v1, https://goo.gl/fbAQLP\n\n';
const body = entries.map(snapBlock).join('');

fs.mkdirSync(path.dirname(OUTPUT_PATH), { recursive: true });
fs.writeFileSync(OUTPUT_PATH, HEADER + body, 'utf8');

// --- summary ---------------------------------------------------------------

const relOut = path.relative(PROJECT_ROOT, OUTPUT_PATH);
console.log(`\nWrote ${stats.rendered} entries to ${relOut}`);
console.log(`  no-error: ${stats.noError}  with-errors: ${stats.withErrors}  render-failures: ${stats.renderFailures}`);
console.log(`\nError code breakdown:`);
for (const [code, n] of [...stats.byErrorCode.entries()].sort((a, b) => b[1] - a[1])) {
  console.log(`  ${String(n).padStart(4)}  ${code}`);
}
console.log(`\nPer-category counts (${stats.byCategory.size} categories):`);
const widest = [...stats.byCategory.keys()].reduce((w, k) => Math.max(w, k.length), 0);
for (const [cat, n] of [...stats.byCategory.entries()].sort()) {
  console.log(`  ${cat.padEnd(widest, ' ')}  ${String(n).padStart(4)}`);
}
