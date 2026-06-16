#!/usr/bin/env node
// Extract LaTeX math expressions from ./test/input/ documents.
// Output: ./temp/lambda_mathlive_extracted.json (a flat list of records).
//
// Each record:
//   { source: <path>, line: <1-based>, display: bool, env: <env or null>, latex: <expr> }
//
// Scope:
//   - Skips ascii_math_*.txt (AsciiMath, not LaTeX).
//   - .txt files that look like math (math_*.txt, matrix_*.txt): each non-empty
//     line is one expression.
//   - .md and .tex files: scan for $...$, $$...$$, \(...\), \[...\],
//     and \begin{<math_env>}...\end{<math_env>}.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const INPUT_DIR = path.join(PROJECT_ROOT, 'test/input');
const TEMP_DIR = path.join(PROJECT_ROOT, 'temp');
const OUT_PATH = path.join(TEMP_DIR, 'lambda_mathlive_extracted.json');

const MATH_ENVS = new Set([
  'equation', 'equation*',
  'align', 'align*',
  'gather', 'gather*',
  'multline', 'multline*',
  'eqnarray', 'eqnarray*',
  'aligned', 'alignat', 'alignat*',
  'cases',
  'matrix', 'pmatrix', 'bmatrix', 'vmatrix', 'Vmatrix', 'Bmatrix',
  'smallmatrix', 'array',
  'displaymath',
]);

// Files whose entire content is one or more bare LaTeX expressions, one per line.
function isBareLatexTxt(name) {
  return (name.startsWith('math_') || name.startsWith('matrix_'))
    && name.endsWith('.txt')
    && !name.startsWith('ascii_math_');
}

// Index-pair helpers -------------------------------------------------------

function lineOf(src, idx) {
  let line = 1;
  for (let i = 0; i < idx && i < src.length; i++) {
    if (src.charCodeAt(i) === 10) line++;
  }
  return line;
}

// Mask out fenced code blocks (```...```) and indented code blocks-of-4 are
// rare in these files; we only handle fenced. Replace fenced regions with
// spaces of equal length so character indexes still map to original line nums.
function maskFences(src) {
  let out = src;
  // ```lang\n...\n``` — non-greedy
  out = out.replace(/```[^\n]*\n[\s\S]*?\n```/g, (m) => m.replace(/[^\n]/g, ' '));
  // Inline `code` spans — also mask, since a $ inside an inline code span is
  // not math. Keep length to preserve line numbers.
  out = out.replace(/`[^`\n]*`/g, (m) => m.replace(/[^\n]/g, ' '));
  return out;
}

// For .tex files: mask lines whose first non-space character is %.
function maskTexComments(src) {
  return src.replace(/(^|\n)([ \t]*)%[^\n]*/g, (m, pfx, sp) =>
    pfx + sp + ' '.repeat(m.length - pfx.length - sp.length));
}

// Push record helper, normalizing trim.
function pushExpr(records, src, latex, idx, display, env) {
  const trimmed = latex.replace(/^\s+|\s+$/g, '');
  if (!trimmed) return;
  records.push({
    source: path.relative(PROJECT_ROOT, src),
    line: lineOf(fs.readFileSync(src, 'utf8'), idx),
    display,
    env,
    latex: trimmed,
  });
}

// Scan a masked text body for math expressions ----------------------------
// `allowTexDelims`: if true, recognize \(...\) and \[...\] (TeX-only — they
// are bracket-escape sequences in plain markdown, not display math).

function extractFromMaskedTex(records, srcPath, raw, masked, allowTexDelims) {
  const blocked = new Array(masked.length).fill(false);
  const overlapsBlocked = (s, e) => {
    for (let i = s; i < e; i++) if (blocked[i]) return true;
    return false;
  };
  const block = (s, e) => { for (let i = s; i < e; i++) blocked[i] = true; };

  let m;

  // 1) Display math: $$ ... $$  (body has no $).
  //    Match first so envs inside $$...$$ are absorbed into the $$ record
  //    rather than split out (the $$ was the author's chosen unit).
  const dollarDisp = /\$\$([^$]+?)\$\$/g;
  while ((m = dollarDisp.exec(masked)) !== null) {
    pushExpr(records, srcPath, m[1], m.index, true, null);
    block(m.index, m.index + m[0].length);
  }

  // 2) Display math: \[ ... \]  (TeX only; lookbehind skips \\[8pt] spacing).
  if (allowTexDelims) {
    const sqDisp = /(?<!\\)\\\[([\s\S]*?)(?<!\\)\\\]/g;
    while ((m = sqDisp.exec(masked)) !== null) {
      if (overlapsBlocked(m.index, m.index + m[0].length)) continue;
      pushExpr(records, srcPath, m[1], m.index, true, null);
      block(m.index, m.index + m[0].length);
    }

    // 3) Inline math: \( ... \)  (TeX only).
    const parInline = /(?<!\\)\\\(([\s\S]*?)(?<!\\)\\\)/g;
    while ((m = parInline.exec(masked)) !== null) {
      if (overlapsBlocked(m.index, m.index + m[0].length)) continue;
      pushExpr(records, srcPath, m[1], m.index, false, null);
      block(m.index, m.index + m[0].length);
    }
  }

  // 4) \begin{<math_env>} ... \end{<same env>} — scan begin tokens one at a
  //    time so non-math wrappers like \begin{document} don't gobble inner
  //    math envs (greedy regex would advance past them on `continue`).
  const beginRe = /\\begin\{([A-Za-z*]+)\}/g;
  while ((m = beginRe.exec(masked)) !== null) {
    if (blocked[m.index]) continue;
    const env = m[1];
    if (!MATH_ENVS.has(env)) continue;
    const endTag = `\\end{${env}}`;
    const endIdx = masked.indexOf(endTag, m.index + m[0].length);
    if (endIdx < 0) continue;
    const absEnd = endIdx + endTag.length;
    if (overlapsBlocked(m.index, absEnd)) continue;
    pushExpr(records, srcPath, masked.slice(m.index, absEnd), m.index, true, env);
    block(m.index, absEnd);
    beginRe.lastIndex = absEnd;
  }

  // 5) Inline math: $ ... $  (body has no $, no newline).
  //    Skip ${...} (JS template) and lone numeric tokens like $0$, $1$.
  const dollarInline = /(?<!\\)\$([^$\n]+?)(?<!\\)\$/g;
  while ((m = dollarInline.exec(masked)) !== null) {
    if (overlapsBlocked(m.index, m.index + m[0].length)) continue;
    const body = m[1];
    if (body.startsWith('{')) continue; // ${name} style
    pushExpr(records, srcPath, body, m.index, false, null);
    block(m.index, m.index + m[0].length);
  }
}

// Per-format scanners ------------------------------------------------------

function scanBareTxt(records, srcPath) {
  const raw = fs.readFileSync(srcPath, 'utf8');
  const lines = raw.split('\n');
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].replace(/^\s+|\s+$/g, '');
    if (!line) continue;
    records.push({
      source: path.relative(PROJECT_ROOT, srcPath),
      line: i + 1,
      display: false,
      env: null,
      latex: line,
    });
  }
}

function scanMd(records, srcPath) {
  const raw = fs.readFileSync(srcPath, 'utf8');
  const masked = maskFences(raw);
  extractFromMaskedTex(records, srcPath, raw, masked, /*allowTexDelims*/ false);
}

function scanTex(records, srcPath) {
  const raw = fs.readFileSync(srcPath, 'utf8');
  const masked = maskTexComments(raw);
  extractFromMaskedTex(records, srcPath, raw, masked, /*allowTexDelims*/ true);
}

// Main ---------------------------------------------------------------------

function main() {
  if (!fs.existsSync(TEMP_DIR)) fs.mkdirSync(TEMP_DIR, { recursive: true });

  const entries = fs.readdirSync(INPUT_DIR).sort();
  const records = [];

  for (const name of entries) {
    const srcPath = path.join(INPUT_DIR, name);
    const stat = fs.statSync(srcPath);
    if (!stat.isFile()) continue;
    if (name.startsWith('ascii_math_')) continue;

    if (name.endsWith('.txt')) {
      if (isBareLatexTxt(name)) scanBareTxt(records, srcPath);
      // else: txt files that aren't bare LaTeX → no math.
    } else if (name.endsWith('.md')) {
      scanMd(records, srcPath);
    } else if (name.endsWith('.tex')) {
      scanTex(records, srcPath);
    }
  }

  // Sort by (source, line) for stable output.
  records.sort((a, b) => a.source.localeCompare(b.source) || a.line - b.line);

  // Re-line: lineOf in pushExpr re-reads file each call; that's wasteful but
  // fine for this corpus size. Records already have line numbers; we just
  // ensure each entry has them assigned.

  fs.writeFileSync(OUT_PATH, JSON.stringify(records, null, 2) + '\n', 'utf8');

  // Summary -----------------------------------------------------------------
  const bySrc = new Map();
  for (const r of records) bySrc.set(r.source, (bySrc.get(r.source) || 0) + 1);
  console.log(`Wrote ${records.length} expressions to ${path.relative(PROJECT_ROOT, OUT_PATH)}`);
  console.log(`Across ${bySrc.size} source files:`);
  const widest = [...bySrc.keys()].reduce((w, k) => Math.max(w, k.length), 0);
  for (const [src, n] of [...bySrc.entries()].sort()) {
    console.log(`  ${src.padEnd(widest, ' ')}  ${String(n).padStart(4)}`);
  }
}

main();
