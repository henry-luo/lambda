#!/usr/bin/env node
// Rule 15 fraction geometry fixture.
// Each entry is a representative LaTeX expression for a fraction layout shape
// that used to have a dedicated frac_bar_spec() branch. The implementation is
// now metric-driven, so this fixture compares final Lambda HTML against
// MathLive rather than asserting deleted dispatch branches.
//
// Usage:
//   node test/lambda/mathlive/fraction_branch_fixture.mjs
//   node test/lambda/mathlive/fraction_branch_fixture.mjs --update-baseline
//   node test/lambda/mathlive/fraction_branch_fixture.mjs --branch B22

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const TEMP_DIR = path.join(PROJECT_ROOT, 'temp');
const LAMBDA = path.join(PROJECT_ROOT, 'lambda.exe');
const MATHLIVE_SSR = path.join(
  PROJECT_ROOT,
  'ref/mathlive/dist/mathlive-ssr.min.mjs'
);

// Historical labels are kept for --branch compatibility, but they now identify
// geometry coverage cases instead of code branches.
const FIXTURE = [
  {
    branch: 'B1',
    desc: 'script & script_container — denom is a fraction',
    formula: 'a_{\\frac{1}{2}}',
    display: false,
  },
  {
    branch: 'B2',
    desc: 'colorbox + nested numer-fraction',
    formula: 'a+\\colorbox{red}{$\\frac{\\frac{1}{2}}{c}$}',
    display: false,
  },
  {
    branch: 'B3',
    desc: 'scriptscript style',
    formula: 'a_{b_{\\frac{1}{2}}}',
    display: false,
  },
  {
    branch: 'B4',
    desc: 'script + tall numer + short denom',
    formula: 'a_{\\frac{A}{x}}',
    display: false,
  },
  {
    branch: 'B5',
    desc: 'script with descender numer',
    formula: 'a_{\\frac{p}{x}}',
    display: false,
  },
  {
    branch: 'B6',
    desc: 'script + short denom',
    formula: 'a_{\\frac{x}{y}}',
    display: false,
  },
  {
    branch: 'B7',
    desc: 'script + larger child',
    formula: 'a_{\\frac{ab}{cd}}',
    display: false,
  },
  {
    branch: 'B8',
    desc: 'script catch-all',
    formula: 'a_{\\frac12}',
    display: false,
  },
  {
    branch: 'B9',
    desc: 'colorbox big numer + big denom',
    formula:
      'a+\\colorbox{red}{$\\frac{\\frac{\\frac{1}{2}}{c}}{\\frac{a+1}{b+c}}$}',
    display: false,
  },
  {
    branch: 'B10',
    desc: 'colorbox big numer + small denom',
    formula: 'a+\\colorbox{red}{$\\frac{\\frac{1}{2}}{c}$}',
    display: false,
  },
  {
    branch: 'B12',
    desc: 'numer big + denom big',
    formula: '\\frac{\\frac{1}{2}}{\\frac{3}{4}+1}',
    display: false,
  },
  {
    branch: 'B13',
    desc: 'numer big + denom small',
    formula: '\\frac{\\frac{1}{2}}{x}',
    display: false,
  },
  {
    branch: 'B14',
    desc: 'numer is script-radical',
    formula: '\\frac{\\sqrt{x}^2}{y}',
    display: false,
  },
  {
    branch: 'B15',
    desc: 'numer moderate + denom short',
    formula: '\\frac{ab}{x}',
    display: false,
  },
  {
    branch: 'B16',
    desc: 'denom big + numer short',
    formula: '\\frac{x}{\\frac{1}{2}+1}',
    display: false,
  },
  {
    branch: 'B17',
    desc: 'denom moderate + numer short',
    formula: '\\frac{x}{\\frac{1}{2}}',
    display: false,
  },
  {
    branch: 'B18',
    desc: 'both moderate-large',
    formula: '\\frac{ab+c}{xy+z}',
    display: false,
  },
  {
    branch: 'B19',
    desc: 'denom moderate + numer short — compound denom descender',
    formula: '\\frac{1}{p+q}',
    display: false,
  },
  {
    branch: 'B19b',
    desc: 'denom moderate + numer short — no descender',
    formula: '\\frac{1}{ab}',
    display: false,
  },
  {
    branch: 'B20',
    desc: 'child moderate (both)',
    formula: '\\frac{abc}{def}',
    display: false,
  },
  {
    branch: 'B21',
    desc: 'short denom + moderate numer (tall single)',
    formula: '\\frac{d}{dx}',
    display: false,
  },
  {
    branch: 'B22a',
    desc: 'short-body numer + no-descender denom (a/b)',
    formula: '\\frac{a}{b}',
    display: false,
  },
  {
    branch: 'B22b',
    desc: 'short-body numer + descender denom (x/y)',
    formula: '\\frac{x}{y}',
    display: false,
  },
  {
    branch: 'B23',
    desc: 'tall numer + tall body (no significant descender)',
    formula: '\\frac{A}{B}',
    display: false,
  },
  {
    branch: 'B24a',
    desc: 'default — digit/digit (5/7)',
    formula: '\\frac{5}{7}',
    display: false,
  },
  {
    branch: 'B24b',
    desc: 'default — digit/digit-with-descender',
    formula: '\\frac{1}{y}',
    display: false,
  },
  {
    branch: 'B24c',
    desc: 'default — compound names',
    formula: '\\frac{numerator}{denominator}',
    display: false,
  },
];

// Lazy-load MathLive
let mlExports = null;
async function loadML() {
  if (mlExports) return mlExports;
  mlExports = await import(MATHLIVE_SSR);
  if (typeof mlExports.convertLatexToMarkup !== 'function')
    throw new Error('MathLive SSR bundle missing convertLatexToMarkup');
  return mlExports;
}

function applyClassRename(html) {
  return html.replace(/ML__/g, 'lm_');
}

async function renderML(formula, displayMode) {
  const { convertLatexToMarkup } = await loadML();
  const html = convertLatexToMarkup(formula, {
    mathstyle: displayMode ? 'displaystyle' : 'textstyle',
    defaultMode: 'math',
  });
  return applyClassRename(html);
}

function renderLambda(formula, displayMode) {
  fs.mkdirSync(TEMP_DIR, { recursive: true });
  const scriptPath = path.join(TEMP_DIR, 'frac_fixture_run.ls');
  const lit = JSON.stringify(formula);
  const fn = displayMode ? 'render_display' : 'render_inline';
  const script = `import math_pkg: lambda.package.math.math
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
  fs.writeFileSync(scriptPath, script);
  const r = spawnSync(LAMBDA, ['--no-log', scriptPath], {
    cwd: PROJECT_ROOT,
    encoding: 'utf8',
    maxBuffer: 16 * 1024 * 1024,
  });
  if (r.status !== 0) throw new Error(`lambda failed: ${r.stderr.slice(0, 500)}`);
  const out = r.stdout.trim();
  const parsed = out.startsWith('"') && out.endsWith('"')
    ? JSON.parse(out.slice(1, -1))
    : JSON.parse(out);
  return parsed.html;
}

function normalize(html) {
  return html.replace(/\s+/g, ' ').trim();
}

async function main() {
  const args = process.argv.slice(2);
  const filterBranch = args.includes('--branch')
    ? args[args.indexOf('--branch') + 1]
    : null;

  let pass = 0,
    fail = 0;
  const failures = [];
  for (const t of FIXTURE) {
    if (filterBranch && t.branch !== filterBranch) continue;
    process.stdout.write(`  ${t.branch.padEnd(5)} ${t.desc.padEnd(60)} `);
    try {
      const [ml, la] = await Promise.all([
        renderML(t.formula, t.display),
        Promise.resolve(renderLambda(t.formula, t.display)),
      ]);
      const mlN = normalize(ml);
      const laN = normalize(la);
      if (mlN === laN) {
        process.stdout.write('PASS\n');
        pass++;
      } else {
        process.stdout.write('FAIL\n');
        failures.push({ branch: t.branch, formula: t.formula, ml: mlN, la: laN });
        fail++;
      }
    } catch (err) {
      process.stdout.write('ERROR\n');
      failures.push({ branch: t.branch, formula: t.formula, err: err.message });
      fail++;
    }
  }
  console.log(
    `\n${pass}/${pass + fail} fraction geometry cases pass${filterBranch ? ` (filter=${filterBranch})` : ''}.`
  );

  if (failures.length > 0 && args.includes('--show-diffs')) {
    console.log('\nFailures:');
    for (const f of failures) {
      console.log(`\n  [${f.branch}] ${f.formula}`);
      if (f.err) console.log(`    error: ${f.err}`);
      else {
        console.log(`    ml: ${f.ml.slice(0, 300)}`);
        console.log(`    la: ${f.la.slice(0, 300)}`);
      }
    }
  }
}

try {
  await main();
} catch (err) {
  console.error(err.stack || err.message);
  process.exit(1);
}
