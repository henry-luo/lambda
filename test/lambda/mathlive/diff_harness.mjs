#!/usr/bin/env node
// Side-by-side diff harness for Lambda math vs MathLive.
//
// Renders a LaTeX expression through BOTH MathLive (via SSR bundle) and
// Lambda (via lambda.exe), tokenizes the HTML output of each, and emits
// a structural diff:
//
//   1. Aligned token-by-token comparison (open-tag, text, close-tag).
//   2. Classified mismatches: class diff, style diff, em-value drift, text diff,
//      structure mismatch.
//   3. Em-value drift sorted by magnitude (largest first) — the metric calibration
//      bottleneck the project is trying to close.
//   4. Optional --tree mode prints both renders as an indented S-expression so
//      structural drift is visible at a glance.
//
// Usage:
//   node test/lambda/mathlive/diff_harness.mjs "\\frac{1}{2}"
//   node test/lambda/mathlive/diff_harness.mjs --display "\\sum_{i=1}^n i^2"
//   node test/lambda/mathlive/diff_harness.mjs --batch failing_cases.txt
//   node test/lambda/mathlive/diff_harness.mjs --from-report temp/lambda_mathlive_markup_report.json --top 20
//   node test/lambda/mathlive/diff_harness.mjs --json "\\frac{1}{2}" > diff.json
//   node test/lambda/mathlive/diff_harness.mjs --tree "\\frac{1}{2}"

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const TEMP_DIR = path.join(PROJECT_ROOT, 'temp');
const LAMBDA = path.join(PROJECT_ROOT, 'lambda.exe');
const MATHLIVE_SSR_PATH = path.join(
  PROJECT_ROOT,
  'ref/mathlive/dist/mathlive-ssr.min.mjs'
);

// ANSI colors — emit only when stdout is a TTY.
const COLORS = process.stdout.isTTY
  ? {
      reset: '\x1b[0m',
      dim: '\x1b[2m',
      red: '\x1b[31m',
      green: '\x1b[32m',
      yellow: '\x1b[33m',
      cyan: '\x1b[36m',
      magenta: '\x1b[35m',
      bold: '\x1b[1m',
    }
  : Object.fromEntries(
      ['reset', 'dim', 'red', 'green', 'yellow', 'cyan', 'magenta', 'bold'].map((k) => [
        k,
        '',
      ])
    );

// ---------------------------------------------------------------------------
// CLI parsing
// ---------------------------------------------------------------------------

function parseArgs(argv) {
  const opts = {
    formula: null,
    display: false,
    batch: null,
    fromReport: null,
    top: 10,
    json: false,
    tree: false,
    lambda: LAMBDA,
    onlyStructural: false,
    onlyMetric: false,
    quiet: false,
    showHtml: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    if (a === '--display' || a === '-d') opts.display = true;
    else if (a === '--batch') opts.batch = argv[++i];
    else if (a === '--from-report') opts.fromReport = argv[++i];
    else if (a === '--top') opts.top = Number(argv[++i] ?? '10');
    else if (a === '--json') opts.json = true;
    else if (a === '--tree') opts.tree = true;
    else if (a === '--lambda') opts.lambda = path.resolve(argv[++i] ?? '');
    else if (a === '--only-structural') opts.onlyStructural = true;
    else if (a === '--only-metric') opts.onlyMetric = true;
    else if (a === '--quiet' || a === '-q') opts.quiet = true;
    else if (a === '--show-html') opts.showHtml = true;
    else if (a === '--help' || a === '-h') {
      printHelp();
      process.exit(0);
    } else if (a.startsWith('--')) {
      throw new Error(`unknown flag: ${a}`);
    } else {
      if (opts.formula != null) {
        throw new Error(`extra positional argument: ${a}`);
      }
      opts.formula = a;
    }
  }
  if (
    opts.formula == null &&
    opts.batch == null &&
    opts.fromReport == null
  ) {
    printHelp();
    process.exit(2);
  }
  return opts;
}

function printHelp() {
  console.log(`Side-by-side diff harness — Lambda math vs MathLive.

Usage:
  node test/lambda/mathlive/diff_harness.mjs [options] "<LaTeX>"

Modes:
  <LaTeX>                  Diff a single LaTeX expression.
  --batch FILE             One LaTeX expression per line; diff each.
  --from-report PATH       Read a runner report (lambda_mathlive_markup_report.json)
                           and diff the first --top failing cases.

Options:
  --display, -d            Render in display mode (default: inline / text style).
  --top N                  Limit --from-report to N failures (default 10).
  --tree                   Show indented S-expression for both renders.
  --json                   Emit machine-readable JSON instead of pretty diff.
  --only-structural        Show only structure/class/text mismatches; hide em drift.
  --only-metric            Show only em-value drift, skip identical sites.
  --show-html              Print the raw HTML for both Lambda and MathLive.
  --quiet, -q              Suppress per-token output; only summary.
  --lambda PATH            Lambda executable (default: ./lambda.exe).
`);
}

// ---------------------------------------------------------------------------
// MathLive driver — lazy import once.
// ---------------------------------------------------------------------------

let _mathliveExports = null;
async function getMathLive() {
  if (_mathliveExports != null) return _mathliveExports;
  const mod = await import(MATHLIVE_SSR_PATH);
  if (
    typeof mod.convertLatexToMarkup !== 'function' ||
    typeof mod.validateLatex !== 'function'
  ) {
    throw new Error('MathLive SSR bundle missing expected exports.');
  }
  _mathliveExports = mod;
  return mod;
}

async function renderMathLive(formula, displayMode) {
  const { convertLatexToMarkup } = await getMathLive();
  // MathLive's mathstyle option: 'displaystyle' | 'textstyle' | 'scriptstyle' | 'scriptscriptstyle'.
  const html = convertLatexToMarkup(formula, {
    mathstyle: displayMode ? 'displaystyle' : 'textstyle',
    defaultMode: 'math',
  });
  return applyClassRename(html);
}

function applyClassRename(html) {
  // Match Lambda's class convention: ML__ → lm_, and the unprefixed `ML-X`
  // group classes that MathLive uses (`ML-mord`, etc.) → `lm_X` family.
  // Lambda emits `lm_mord` / `lm_mathit` etc. directly.
  return html.replace(/ML__/g, 'lm_');
}

// ---------------------------------------------------------------------------
// Lambda driver
// ---------------------------------------------------------------------------

function renderLambda(formula, displayMode, lambdaPath) {
  fs.mkdirSync(TEMP_DIR, { recursive: true });
  const scriptPath = path.join(TEMP_DIR, 'diff_harness_one.ls');
  const script = buildLambdaScript(formula, displayMode);
  fs.writeFileSync(scriptPath, script);

  const result = spawnSync(lambdaPath, ['--no-log', scriptPath], {
    cwd: PROJECT_ROOT,
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
  });
  if (result.status !== 0) {
    const detail = (result.stderr || '').trim().slice(0, 1000);
    throw new Error(
      `lambda.exe exited ${result.status ?? result.signal}: ${detail}`
    );
  }
  const out = result.stdout.trim();
  try {
    const parsed = out.startsWith('"') && out.endsWith('"')
      ? JSON.parse(out.slice(1, -1))
      : JSON.parse(out);
    if (parsed && typeof parsed.html === 'string') return parsed;
    return { html: String(parsed), error: 'no-error' };
  } catch (err) {
    throw new Error(
      `could not parse Lambda JSON output: ${err.message}\n${out.slice(0, 1000)}`
    );
  }
}

function buildLambdaScript(formula, displayMode) {
  const lit = JSON.stringify(formula);
  const fn = displayMode ? 'render_display' : 'render_inline';
  return `import math_pkg: lambda.package.math.math
import html_ser: lambda.package.latex.to_html

let formula = ${lit}
let ast^err = parse(formula, {type: "math", flavor: "latex"})
let result = if (^err) {
    {formula: formula, error: string(^err), html: ""}
} else {
    let rendered = math_pkg.${fn}(ast)
    let html = html_ser.to_html(rendered)
    {formula: formula, error: "no-error", html: html}
}
format(result, "json")
`;
}

// ---------------------------------------------------------------------------
// HTML tokenizer — handles <span ...>, text, </span>. Lambda's renderer
// emits only span / br elements, so we don't need a full HTML parser.
// ---------------------------------------------------------------------------

const TAG_RE = /<(\/?)([a-zA-Z][\w:-]*)((?:\s+[^>]*)?)>/g;
const ATTR_RE = /([a-zA-Z][\w:-]*)\s*=\s*("([^"]*)"|'([^']*)'|([^\s>]+))/g;

function tokenize(html) {
  const tokens = [];
  let lastIndex = 0;
  let m;
  TAG_RE.lastIndex = 0;
  while ((m = TAG_RE.exec(html)) !== null) {
    const before = html.slice(lastIndex, m.index);
    if (before.length > 0) {
      const text = decodeEntities(before);
      if (text.length > 0) tokens.push({ kind: 'text', text });
    }
    const closing = m[1] === '/';
    const tag = m[2].toLowerCase();
    const attrText = m[3] || '';
    if (closing) {
      tokens.push({ kind: 'close', tag });
    } else {
      const attrs = parseAttrs(attrText);
      tokens.push({ kind: 'open', tag, attrs });
    }
    lastIndex = TAG_RE.lastIndex;
  }
  const tail = html.slice(lastIndex);
  if (tail.length > 0) {
    const text = decodeEntities(tail);
    if (text.length > 0) tokens.push({ kind: 'text', text });
  }
  return tokens;
}

function parseAttrs(s) {
  const attrs = {};
  let m;
  ATTR_RE.lastIndex = 0;
  while ((m = ATTR_RE.exec(s)) !== null) {
    const name = m[1].toLowerCase();
    const value = m[3] ?? m[4] ?? m[5] ?? '';
    attrs[name] = value;
  }
  return attrs;
}

function decodeEntities(s) {
  return s
    .replace(/&amp;/g, '&')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&#x([0-9a-fA-F]+);/g, (_, hex) =>
      String.fromCodePoint(parseInt(hex, 16))
    )
    .replace(/&#(\d+);/g, (_, dec) => String.fromCodePoint(parseInt(dec, 10)));
}

// ---------------------------------------------------------------------------
// Style helpers: parse "key: value; key2: value2" → object, with em values
// pulled out for drift comparison.
// ---------------------------------------------------------------------------

function parseStyle(style) {
  const props = {};
  if (!style) return props;
  for (const decl of style.split(';')) {
    const idx = decl.indexOf(':');
    if (idx === -1) continue;
    const key = decl.slice(0, idx).trim().toLowerCase();
    const value = decl.slice(idx + 1).trim();
    if (key.length > 0) props[key] = value;
  }
  return props;
}

function classSet(cls) {
  if (!cls) return new Set();
  return new Set(cls.trim().split(/\s+/).filter(Boolean));
}

function parseEm(value) {
  const m = /^(-?\d+(?:\.\d+)?)em$/.exec(value);
  if (!m) return null;
  return parseFloat(m[1]);
}

// ---------------------------------------------------------------------------
// Tree builder — turn token list into a tree of {tag, attrs, children}.
// ---------------------------------------------------------------------------

function buildTree(tokens) {
  // Root sentinel collects top-level siblings.
  const root = { tag: '#root', attrs: {}, children: [] };
  const stack = [root];
  for (const tok of tokens) {
    const top = stack[stack.length - 1];
    if (tok.kind === 'open') {
      const node = { tag: tok.tag, attrs: tok.attrs, children: [] };
      top.children.push(node);
      stack.push(node);
    } else if (tok.kind === 'close') {
      // Pop until we find a matching tag — recover from minor nesting issues.
      while (stack.length > 1 && stack[stack.length - 1].tag !== tok.tag) {
        stack.pop();
      }
      if (stack.length > 1) stack.pop();
    } else if (tok.kind === 'text') {
      top.children.push({ tag: '#text', text: tok.text });
    }
  }
  return root;
}

// ---------------------------------------------------------------------------
// Aligned tree diff — recursive walk, pairing children index-by-index.
// Records site descriptors for each mismatch with an em-aware classification.
// ---------------------------------------------------------------------------

function diffTrees(lambdaTree, mlTree) {
  const findings = [];
  walk(lambdaTree, mlTree, [], findings);
  return findings;
}

function walk(la, ml, pathSegs, findings) {
  if (la == null && ml == null) return;
  if (la == null) {
    findings.push({
      kind: 'extra-mathlive',
      path: pathSegs.join(' > '),
      mathlive: nodeSummary(ml),
      lambda: null,
    });
    return;
  }
  if (ml == null) {
    findings.push({
      kind: 'extra-lambda',
      path: pathSegs.join(' > '),
      lambda: nodeSummary(la),
      mathlive: null,
    });
    return;
  }
  if (la.tag !== ml.tag) {
    findings.push({
      kind: 'tag-mismatch',
      path: pathSegs.join(' > '),
      lambda: nodeSummary(la),
      mathlive: nodeSummary(ml),
    });
    return;
  }
  if (la.tag === '#text') {
    if (la.text !== ml.text) {
      findings.push({
        kind: 'text-mismatch',
        path: pathSegs.join(' > '),
        lambda: la.text,
        mathlive: ml.text,
      });
    }
    return;
  }

  // class diff
  const laClasses = classSet(la.attrs?.class);
  const mlClasses = classSet(ml.attrs?.class);
  const classLambdaOnly = [...laClasses].filter((c) => !mlClasses.has(c));
  const classMlOnly = [...mlClasses].filter((c) => !laClasses.has(c));
  if (classLambdaOnly.length > 0 || classMlOnly.length > 0) {
    findings.push({
      kind: 'class-diff',
      path: pathSegs.join(' > '),
      lambdaOnly: classLambdaOnly,
      mathliveOnly: classMlOnly,
      lambdaClasses: [...laClasses],
      mathliveClasses: [...mlClasses],
    });
  }

  // style diff — split into em-drift vs structural style
  const laStyle = parseStyle(la.attrs?.style);
  const mlStyle = parseStyle(ml.attrs?.style);
  const allKeys = new Set([...Object.keys(laStyle), ...Object.keys(mlStyle)]);
  for (const key of allKeys) {
    const lv = laStyle[key];
    const mv = mlStyle[key];
    if (lv === mv) continue;
    const lEm = lv != null ? parseEm(lv) : null;
    const mEm = mv != null ? parseEm(mv) : null;
    if (lEm != null && mEm != null) {
      findings.push({
        kind: 'em-drift',
        path: pathSegs.join(' > '),
        property: key,
        lambdaEm: lEm,
        mathliveEm: mEm,
        delta: lEm - mEm,
      });
    } else {
      findings.push({
        kind: 'style-diff',
        path: pathSegs.join(' > '),
        property: key,
        lambda: lv ?? null,
        mathlive: mv ?? null,
      });
    }
  }

  // recurse children pairwise
  const n = Math.max(la.children.length, ml.children.length);
  for (let i = 0; i < n; i += 1) {
    const lc = la.children[i] ?? null;
    const mc = ml.children[i] ?? null;
    const seg = childPathSegment(lc ?? mc, i);
    walk(lc, mc, [...pathSegs, seg], findings);
  }
}

function childPathSegment(node, index) {
  if (node == null) return `[${index}]`;
  if (node.tag === '#text') {
    const t = (node.text || '').slice(0, 12);
    return `[${index}]#text(${JSON.stringify(t)})`;
  }
  const cls = node.attrs?.class;
  if (cls) {
    const first = cls.trim().split(/\s+/)[0];
    return `[${index}]${node.tag}.${first}`;
  }
  return `[${index}]${node.tag}`;
}

function nodeSummary(node) {
  if (node == null) return null;
  if (node.tag === '#text') return { tag: '#text', text: node.text };
  return {
    tag: node.tag,
    class: node.attrs?.class ?? '',
    style: node.attrs?.style ?? '',
  };
}

// ---------------------------------------------------------------------------
// Tree S-expression formatter (for --tree mode)
// ---------------------------------------------------------------------------

function treeToSexp(node, depth = 0) {
  const indent = '  '.repeat(depth);
  if (node.tag === '#text') {
    return `${indent}"${escSexp(node.text)}"`;
  }
  if (node.tag === '#root') {
    return node.children.map((c) => treeToSexp(c, depth)).join('\n');
  }
  const attrParts = [];
  if (node.attrs?.class) attrParts.push(`class=${JSON.stringify(node.attrs.class)}`);
  if (node.attrs?.style) attrParts.push(`style=${JSON.stringify(node.attrs.style)}`);
  for (const [k, v] of Object.entries(node.attrs ?? {})) {
    if (k === 'class' || k === 'style') continue;
    attrParts.push(`${k}=${JSON.stringify(v)}`);
  }
  const head =
    attrParts.length === 0
      ? `${indent}(${node.tag}`
      : `${indent}(${node.tag} ${attrParts.join(' ')}`;
  if (node.children.length === 0) return `${head})`;
  const childs = node.children.map((c) => treeToSexp(c, depth + 1)).join('\n');
  return `${head}\n${childs}\n${indent})`;
}

function escSexp(s) {
  return s.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
}

// ---------------------------------------------------------------------------
// Pretty-print findings.
// ---------------------------------------------------------------------------

function classifyFindings(findings) {
  const out = {
    structural: [],
    em: [],
    style: [],
    text: [],
    class: [],
  };
  for (const f of findings) {
    switch (f.kind) {
      case 'tag-mismatch':
      case 'extra-lambda':
      case 'extra-mathlive':
        out.structural.push(f);
        break;
      case 'em-drift':
        out.em.push(f);
        break;
      case 'style-diff':
        out.style.push(f);
        break;
      case 'text-mismatch':
        out.text.push(f);
        break;
      case 'class-diff':
        out.class.push(f);
        break;
    }
  }
  out.em.sort((a, b) => Math.abs(b.delta) - Math.abs(a.delta));
  return out;
}

function printDiff(formula, ml, lambda, opts) {
  const c = COLORS;
  const lambdaTokens = tokenize(lambda.html);
  const mathliveTokens = tokenize(ml);
  const lambdaTree = buildTree(lambdaTokens);
  const mathliveTree = buildTree(mathliveTokens);
  const findings = diffTrees(lambdaTree, mathliveTree);
  const classified = classifyFindings(findings);

  if (opts.tree) {
    console.log(`${c.bold}=== Lambda tree ===${c.reset}`);
    console.log(treeToSexp(lambdaTree));
    console.log(`\n${c.bold}=== MathLive tree ===${c.reset}`);
    console.log(treeToSexp(mathliveTree));
    console.log('');
  }

  if (opts.showHtml) {
    console.log(`${c.bold}=== Lambda HTML ===${c.reset}\n${lambda.html}\n`);
    console.log(`${c.bold}=== MathLive HTML ===${c.reset}\n${ml}\n`);
  }

  const passed = findings.length === 0 && lambda.html === ml;

  console.log(
    `${c.bold}Formula:${c.reset} ${formula}    ${c.dim}[display=${opts.display}]${c.reset}`
  );
  console.log(
    `${c.bold}Status:${c.reset} ${
      passed ? `${c.green}PASS${c.reset}` : `${c.red}DIFF${c.reset}`
    }    ${c.dim}lambda=${lambda.html.length}ch mathlive=${ml.length}ch  findings=${findings.length}${c.reset}`
  );

  if (!opts.onlyStructural && classified.em.length > 0) {
    console.log(`\n${c.bold}${c.yellow}Em-value drift (sorted by magnitude):${c.reset}`);
    const top = classified.em.slice(0, 20);
    for (const f of top) {
      const arrow = f.delta > 0 ? '↑' : '↓';
      const color = Math.abs(f.delta) >= 0.05 ? c.red : Math.abs(f.delta) >= 0.01 ? c.yellow : c.dim;
      console.log(
        `  ${color}${arrow} Δ${f.delta.toFixed(4)}em${c.reset}  ${f.property}: ${f.lambdaEm}em (lambda) vs ${f.mathliveEm}em (mathlive)   ${c.dim}${truncate(f.path, 70)}${c.reset}`
      );
    }
    if (classified.em.length > top.length) {
      console.log(`  ${c.dim}... and ${classified.em.length - top.length} more${c.reset}`);
    }
  }

  if (!opts.onlyMetric && classified.structural.length > 0) {
    console.log(`\n${c.bold}${c.red}Structural mismatches:${c.reset}`);
    for (const f of classified.structural.slice(0, 20)) {
      if (f.kind === 'tag-mismatch') {
        console.log(`  ${c.red}≠ tag${c.reset}  ${c.dim}${truncate(f.path, 80)}${c.reset}`);
        console.log(`      lambda:   ${nodeText(f.lambda)}`);
        console.log(`      mathlive: ${nodeText(f.mathlive)}`);
      } else if (f.kind === 'extra-lambda') {
        console.log(`  ${c.magenta}+ lambda only${c.reset}  ${c.dim}${truncate(f.path, 80)}${c.reset}  ${nodeText(f.lambda)}`);
      } else if (f.kind === 'extra-mathlive') {
        console.log(`  ${c.cyan}+ mathlive only${c.reset}  ${c.dim}${truncate(f.path, 80)}${c.reset}  ${nodeText(f.mathlive)}`);
      }
    }
  }

  if (!opts.onlyMetric && classified.class.length > 0) {
    console.log(`\n${c.bold}${c.cyan}Class mismatches:${c.reset}`);
    for (const f of classified.class.slice(0, 20)) {
      const both = `lambda+[${f.lambdaOnly.join(',')}] mathlive+[${f.mathliveOnly.join(',')}]`;
      console.log(`  ${both}  ${c.dim}${truncate(f.path, 70)}${c.reset}`);
    }
  }

  if (!opts.onlyMetric && classified.text.length > 0) {
    console.log(`\n${c.bold}${c.magenta}Text mismatches:${c.reset}`);
    for (const f of classified.text.slice(0, 20)) {
      console.log(
        `  ${c.dim}${truncate(f.path, 60)}${c.reset}  ${JSON.stringify(f.lambda)} vs ${JSON.stringify(f.mathlive)}`
      );
    }
  }

  if (!opts.onlyStructural && !opts.onlyMetric && classified.style.length > 0) {
    console.log(`\n${c.bold}Style diffs (non-em):${c.reset}`);
    for (const f of classified.style.slice(0, 20)) {
      console.log(
        `  ${f.property}: ${c.green}${f.lambda ?? '(none)'}${c.reset} vs ${c.cyan}${f.mathlive ?? '(none)'}${c.reset}   ${c.dim}${truncate(f.path, 60)}${c.reset}`
      );
    }
  }

  if (!passed) {
    const cats = [];
    if (classified.structural.length) cats.push(`structural=${classified.structural.length}`);
    if (classified.class.length) cats.push(`class=${classified.class.length}`);
    if (classified.em.length) cats.push(`em=${classified.em.length}`);
    if (classified.style.length) cats.push(`style=${classified.style.length}`);
    if (classified.text.length) cats.push(`text=${classified.text.length}`);
    console.log(`\n${c.bold}Summary:${c.reset} ${cats.join('  ')}`);
  }
  console.log('');

  return { passed, findings, classified };
}

function nodeText(n) {
  if (n == null) return '<none>';
  if (n.tag === '#text') return `"${n.text}"`;
  let s = `<${n.tag}`;
  if (n.class) s += ` class="${n.class}"`;
  if (n.style) s += ` style="${n.style}"`;
  return `${s}>`;
}

function truncate(s, n) {
  if (s.length <= n) return s;
  return s.slice(0, n - 3) + '...';
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

async function runOne(formula, opts) {
  const ml = await renderMathLive(formula, opts.display);
  const lambda = renderLambda(formula, opts.display, opts.lambda);
  if (opts.json) {
    const lambdaTree = buildTree(tokenize(lambda.html));
    const mlTree = buildTree(tokenize(ml));
    const findings = diffTrees(lambdaTree, mlTree);
    const classified = classifyFindings(findings);
    process.stdout.write(
      JSON.stringify(
        {
          formula,
          display: opts.display,
          lambdaHtml: lambda.html,
          mathliveHtml: ml,
          findings,
          classified: {
            structural: classified.structural.length,
            em: classified.em.length,
            style: classified.style.length,
            text: classified.text.length,
            class: classified.class.length,
          },
        },
        null,
        2
      ) + '\n'
    );
    return findings.length === 0 && lambda.html === ml;
  }
  const { passed } = printDiff(formula, ml, lambda, opts);
  return passed;
}

async function runBatch(filePath, opts) {
  const lines = fs
    .readFileSync(filePath, 'utf8')
    .split('\n')
    .map((l) => l.trim())
    .filter((l) => l.length > 0 && !l.startsWith('#'));
  let pass = 0;
  for (const line of lines) {
    try {
      const ok = await runOne(line, opts);
      if (ok) pass++;
    } catch (err) {
      console.error(`! error on "${line}": ${err.message}`);
    }
  }
  console.log(`\nBatch result: ${pass}/${lines.length} pass`);
}

async function runFromReport(reportPath, opts) {
  const report = JSON.parse(fs.readFileSync(reportPath, 'utf8'));
  const failing = (report.results || []).filter((r) => !r.pass).slice(0, opts.top);
  console.log(`Found ${failing.length} failing cases in ${reportPath} (top ${opts.top}).\n`);
  for (const result of failing) {
    console.log(`${COLORS.bold}=== ${result.key} ===${COLORS.reset}`);
    const oneOpts = { ...opts, display: result.display };
    try {
      await runOne(result.formula, oneOpts);
    } catch (err) {
      console.error(`! error: ${err.message}`);
    }
  }
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.batch != null) {
    await runBatch(opts.batch, opts);
  } else if (opts.fromReport != null) {
    await runFromReport(opts.fromReport, opts);
  } else {
    const ok = await runOne(opts.formula, opts);
    process.exit(ok ? 0 : 1);
  }
}

try {
  await main();
} catch (err) {
  console.error(err.stack || err.message);
  process.exit(2);
}
