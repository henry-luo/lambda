#!/usr/bin/env node
// Adapt MathLive markup snapshots into a Lambda math-package comparison run.
//
// This runner uses the copied MathLive snapshot corpus as expected output, but
// renders formulas through lambda/package/math via a generated Lambda script.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const SNAPSHOT_FIXTURES = [
  {
    source: 'mathlive',
    path: path.join(__dirname, '__snapshots__', 'markup.test.ts.snap'),
  },
  {
    source: 'lambda-input',
    path: path.join(__dirname, '__snapshots__', 'lambda_input_markup.snap'),
  },
];
const TEMP_DIR = path.join(PROJECT_ROOT, 'temp');
const DEFAULT_SCRIPT_PATH = path.join(
  TEMP_DIR,
  'lambda_mathlive_markup_batch.ls'
);
const DEFAULT_REPORT_PATH = path.join(
  TEMP_DIR,
  'lambda_mathlive_markup_report.json'
);
const DEFAULT_BASELINE_PATH = path.join(__dirname, 'baseline.txt');

function parseArgs(argv) {
  const opts = {
    category: null,
    limit: null,
    strict: false,
    list: false,
    lambda: path.join(PROJECT_ROOT, 'lambda.exe'),
    script: DEFAULT_SCRIPT_PATH,
    report: DEFAULT_REPORT_PATH,
    baseline: DEFAULT_BASELINE_PATH,
    noBaseline: false,
    fixtureSource: 'all',
    jobs: defaultJobCount(),
  };

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--strict') opts.strict = true;
    else if (arg === '--list') opts.list = true;
    else if (arg === '--no-baseline') opts.noBaseline = true;
    else if (arg === '--category') opts.category = argv[++i] ?? '';
    else if (arg === '--limit') opts.limit = Number(argv[++i] ?? '0');
    else if (arg === '--lambda') opts.lambda = path.resolve(argv[++i] ?? '');
    else if (arg === '--script') opts.script = path.resolve(argv[++i] ?? '');
    else if (arg === '--report') opts.report = path.resolve(argv[++i] ?? '');
    else if (arg === '--baseline') opts.baseline = path.resolve(argv[++i] ?? '');
    else if (arg === '--fixture-source') opts.fixtureSource = argv[++i] ?? '';
    else if (arg === '--jobs') opts.jobs = Number(argv[++i] ?? '0');
    else if (arg === '--help' || arg === '-h') {
      printHelp();
      process.exit(0);
    } else {
      throw new Error(`unknown argument: ${arg}`);
    }
  }

  if (!Number.isInteger(opts.jobs) || opts.jobs < 1) {
    throw new Error(`--jobs must be a positive integer, got ${opts.jobs}`);
  }

  return opts;
}

function defaultJobCount() {
  return Math.max(1, os.cpus().length - 1);
}

function selectedSnapshotFixtures(source) {
  if (source === 'all') return SNAPSHOT_FIXTURES;
  const fixtures = SNAPSHOT_FIXTURES.filter((fixture) => fixture.source === source);
  if (fixtures.length === 0) {
    throw new Error(`unknown --fixture-source: ${source}`);
  }
  return fixtures;
}

function printHelp() {
  console.log(`Usage: node test/lambda/mathlive/run_lambda_mathlive_markup.mjs [options]

Options:
  --category NAME   Run only snapshot keys whose category contains NAME
  --limit N         Run at most N extracted cases
  --list            List extracted cases without running Lambda
  --strict          Exit non-zero if any extracted case mismatches
  --baseline PATH   Passed-case baseline path, default test/lambda/mathlive/baseline.txt
  --no-baseline     Do not check passed-case baseline regressions
  --fixture-source SOURCE
                    Snapshot source: mathlive, lambda-input, or all (default)
  --lambda PATH     Lambda executable path, default ./lambda.exe
  --script PATH     Generated Lambda batch script path, default ./temp/...
  --report PATH     JSON report path, default ./temp/...
  --jobs N          Parallel Lambda worker count, default CPU count - 1
`);
}

function readBaseline(filePath) {
  const text = fs.readFileSync(filePath, 'utf8');
  return new Set(
    text
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter((line) => line.length > 0 && !line.startsWith('#'))
  );
}

function parseSnapshots(snapshotText) {
  const entries = [];
  const re = /exports\[`([^`]+)`\] = `\n([\s\S]*?)\n`;/g;
  let match;
  while ((match = re.exec(snapshotText)) !== null) {
    const key = match[1];
    const body = match[2];
    const parsed = parseSnapshotBody(body);
    if (!parsed) continue;
    entries.push({ key, ...parsed });
  }
  return entries;
}

function parseSnapshotBody(body) {
  const lines = body.split('\n');
  const htmlStart = lines.findIndex((line) => line.startsWith('  "'));
  const errorIndex = lines.findLastIndex((line) => /^  "[^"]*",$/.test(line));
  if (htmlStart < 0 || errorIndex < 0 || htmlStart >= errorIndex) return null;

  return {
    expectedHtml: lines.slice(htmlStart, errorIndex).join('\n').slice(3, -2),
    expectedError: lines[errorIndex].slice(3, -2),
  };
}

function buildCases(snapshotEntries) {
  const cases = [];
  for (const entry of snapshotEntries) {
    const rawFormula = formulaFromSnapshotKey(entry.key);
    const normalized = rawFormula == null ? null : normalizeFormula(rawFormula);
    if (normalized == null) continue;
    cases.push({
      category: categoryFromKey(entry.key),
      key: entry.key,
      formula: normalized.formula,
      display: normalized.display,
      expectedHtml: entry.expectedHtml,
      expectedError: entry.expectedError,
    });
  }
  return cases;
}

function normalizeFormula(formula) {
  const trimmed = formula.trim();
  if (trimmed.startsWith('\\[') && trimmed.endsWith('\\]')) {
    return { formula: trimmed.slice(2, -2).trim(), display: true };
  }
  return { formula, display: false };
}

function categoryFromKey(key) {
  const categories = [
    'DELIMITER SIZING COMMANDS',
    'SUPERSCRIPT/SUBSCRIPT',
    'RULE AND DIMENSIONS',
    'SPACING AND KERN',
    'BINARY OPERATORS',
    'OVER/UNDERLINE',
    'SIZING COMMANDS',
    'MODE SHIFT',
    'LEFT/RIGHT',
    'ENVIRONMENTS',
    'EXTENSIONS',
    'FRACTIONS',
    'COMMANDS',
    'ACCENTS',
    'COLORS',
    'FONTS',
    'SURDS',
    'BOX',
    'NOT',
  ];
  return categories.find((category) => key.startsWith(category)) ?? key.split(' ')[0];
}

function formulaFromSnapshotKey(key) {
  const accent = key.match(/^ACCENTS \d+\/ (\\+[A-Za-z]+) renders correctly 1$/);
  if (accent) {
    const cmd = unescapeSnapshotText(accent[1]);
    return `${cmd}{x}${cmd}{x + 1}${cmd}`;
  }

  const colorFormat = key.match(/^COLORS \d+\/ color format "([\s\S]*)" renders correctly 1$/);
  if (colorFormat) {
    return `a\\textcolor{${unescapeSnapshotText(colorFormat[1])}}{x}b`;
  }

  if (key === 'COMMANDS Commands  1') {
    return "\\!\\#\\%\\&\\$\\_\\{\\}\\text{\\'{a}\\\"{a}\\.{a}\\`{a}\\={a}\\~{a}\\^{a}}";
  }

  const delimiterSize = key.match(
    /^DELIMITER SIZING COMMANDS \d+\/ sizing command ([\s\S]*) 1$/
  );
  if (delimiterSize) {
    const parts = delimiterSize[1].split(', ').map(unescapeSnapshotText);
    if (parts.length === 4) {
      const [left, right, middle, plain] = parts;
      return `${left}(x${middle}|y${right}) = x+${plain}x${plain}+`;
    }
  }

  const leftRight = key.match(/^LEFT\/RIGHT Delimiters ([\s\S]*) ([0-9]+)$/);
  if (leftRight) {
    const body = leftRight[1];
    const index = Number(leftRight[2]);
    const [open, close] = splitDelimiterPair(body).map(unescapeSnapshotText);
    if (!open || !close) return null;
    if (index % 2 === 1) return `\\left${open} x + 1\\right${close}`;
    return `\\left${open} x \\frac{\\frac34}{\\frac57}\\right${close}`;
  }

  const middle = key.match(/^LEFT\/RIGHT middle delimiters ([123])$/);
  if (middle) {
    return [
      '\\left(a\\middle|b\\right)',
      '\\left(a\\middle xb\\right)',
      '\\left(a\\color{red}\\middle|b\\right)',
    ][Number(middle[1]) - 1];
  }

  const script = key.match(/^SUPERSCRIPT\/SUBSCRIPT ([\s\S]*) 1$/);
  if (script) return unescapeSnapshotText(script[1]);

  const direct = key.match(/^[^ ]+ \d+\/ ([\s\S]*) renders (?:correctly|corectly) 1$/);
  if (direct) return unescapeSnapshotText(direct[1]);

  const multiWordDirect = key.match(
    /^(MODE SHIFT|BINARY OPERATORS|RULE AND DIMENSIONS|SPACING AND KERN|SIZING COMMANDS|OVER\/UNDERLINE|ENVIRONMENTS|EXTENSIONS|SURDS|COLORS|BOX|NOT) \d+\/ ([\s\S]*) renders (?:correctly|corectly) 1$/
  );
  if (multiWordDirect) return unescapeSnapshotText(multiWordDirect[2]);

  return null;
}

function splitDelimiterPair(body) {
  const parts = body.split(' ');
  if (parts.length === 2) return parts;
  if (parts.length > 2) {
    return [parts.slice(0, -1).join(' '), parts[parts.length - 1]];
  }
  return [body, ''];
}

function unescapeSnapshotText(text) {
  return text.replace(/\\\\n(?=\s|$)/g, ' ').replace(/\\\\/g, '\\');
}

function lambdaString(value) {
  return JSON.stringify(value);
}

function buildLambdaScript(cases) {
  const formulaList = cases.map((testCase) =>
    `    {formula: ${lambdaString(testCase.formula)}, display: ${testCase.display ? 'true' : 'false'}}`
  ).join(',\n');
  return `import math_pkg: lambda.package.math.math
import html_ser: lambda.package.latex.to_html

let cases = [
${formulaList}
]

fn render_formula(test_case) {
    let formula = test_case.formula
    let ast^err = parse(formula, {type: "math", flavor: "latex"})
    if (^err) {
        let result = {formula: formula, error: string(^err), html: ""}
        result
    }
    else {
        // MathLive's convertLatexToMarkup (which generated the golden
        // snapshots) defaults to mathstyle: 'displaystyle' regardless of the
        // \[..\] delimiters (mathlive-ssr.ts:106-108). The whole corpus is
        // therefore display-rooted — verified: inline-golden superscripts use
        // sup1 (0.41), never sup2 (0.36). Render display-style to match the
        // ground truth.
        let rendered = math_pkg.render_display(ast)
        let html = html_ser.to_html(rendered)
        let result = {formula: formula, error: "no-error", html: html}
        result
    }
}

let outputs = [for (test_case in cases) render_formula(test_case)]
format(outputs, "json")
`;
}

async function runLambda(cases, opts) {
  const jobCount = Math.min(opts.jobs, cases.length);
  if (jobCount <= 1) {
    return runLambdaShard(cases, opts.script, opts);
  }

  const shards = shardCases(cases, jobCount);
  const shardResults = await Promise.all(
    shards.map((shard) =>
      runLambdaShard(shard.cases, shardScriptPath(opts.script, shard.index), opts)
        .then((actuals) => ({ start: shard.start, actuals }))
    )
  );

  const actuals = new Array(cases.length);
  for (const shard of shardResults) {
    for (let i = 0; i < shard.actuals.length; i += 1) {
      actuals[shard.start + i] = shard.actuals[i];
    }
  }
  return actuals;
}

function shardCases(cases, jobCount) {
  const shards = [];
  const chunkSize = Math.ceil(cases.length / jobCount);
  for (let start = 0; start < cases.length; start += chunkSize) {
    shards.push({
      index: shards.length,
      start,
      cases: cases.slice(start, start + chunkSize),
    });
  }
  return shards;
}

function shardScriptPath(scriptPath, index) {
  const parsed = path.parse(scriptPath);
  return path.join(parsed.dir, `${parsed.name}.job${index}${parsed.ext || '.ls'}`);
}

function runLambdaShard(cases, scriptPath, opts) {
  fs.mkdirSync(path.dirname(scriptPath), { recursive: true });
  fs.writeFileSync(scriptPath, buildLambdaScript(cases));

  return new Promise((resolve, reject) => {
    const child = spawn(opts.lambda, ['--no-log', scriptPath], {
      cwd: PROJECT_ROOT,
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    const chunks = { stdout: [], stderr: [] };
    let stdoutBytes = 0;
    let stderrBytes = 0;
    const maxBytes = 128 * 1024 * 1024;

    child.stdout.on('data', (chunk) => {
      stdoutBytes += chunk.length;
      if (stdoutBytes > maxBytes) {
        child.kill();
        reject(new Error(`Lambda stdout exceeded ${maxBytes} bytes for ${scriptPath}`));
      } else {
        chunks.stdout.push(chunk);
      }
    });
    child.stderr.on('data', (chunk) => {
      stderrBytes += chunk.length;
      if (stderrBytes > maxBytes) {
        child.kill();
        reject(new Error(`Lambda stderr exceeded ${maxBytes} bytes for ${scriptPath}`));
      } else {
        chunks.stderr.push(chunk);
      }
    });
    child.on('error', reject);
    child.on('close', (status, signal) => {
      const stdout = Buffer.concat(chunks.stdout).toString('utf8');
      const stderr = Buffer.concat(chunks.stderr).toString('utf8');
      if (status !== 0) {
        reject(
          new Error(
            `Lambda exited with ${status ?? signal} for ${scriptPath}\n\nSTDOUT:\n${stdout}\n\nSTDERR:\n${stderr}`
          )
        );
        return;
      }
      try {
        resolve(parseLambdaJsonString(stdout));
      } catch (err) {
        reject(err);
      }
    });
  });
}

function parseLambdaJsonString(stdout) {
  const trimmed = stdout.trim();
  try {
    if (trimmed.startsWith('"') && trimmed.endsWith('"')) {
      return JSON.parse(trimmed.slice(1, -1));
    }
    return JSON.parse(trimmed);
  } catch (err) {
    throw new Error(`could not parse Lambda JSON output: ${err.message}\n${trimmed.slice(0, 2000)}`);
  }
}

function normalizeHtml(html) {
  return html.replace(/\s+/g, ' ').trim();
}

// A case whose HTML differs from the golden ONLY in `<num>em` dimensions, each
// within this many em, is allowed to pass (sub-pixel CEIL@2 rounding tips at
// font-metric precision). Such passes are flagged with a warning line.
const EM_TOLERANCE = 0.015;

// Returns { tolerant, diffs }. `tolerant` is true only when `expected` and
// `actual` are byte-identical apart from `<num>em` values, and every differing
// em value is within `tol`. Any structural (non-em) difference => not tolerant.
function compareEmTolerant(expected, actual, tol) {
  const re = /(-?\d+(?:\.\d+)?)em/g;
  const eNums = [];
  const aNums = [];
  const eTemplate = expected.replace(re, (_, n) => (eNums.push(parseFloat(n)), 'em'));
  const aTemplate = actual.replace(re, (_, n) => (aNums.push(parseFloat(n)), 'em'));
  if (eTemplate !== aTemplate || eNums.length !== aNums.length) {
    return { tolerant: false, diffs: [] };
  }
  const diffs = [];
  for (let i = 0; i < eNums.length; i += 1) {
    const delta = Math.abs(eNums[i] - aNums[i]);
    if (delta > 1e-9) {
      if (delta > tol + 1e-9) return { tolerant: false, diffs: [] };
      diffs.push({ expected: eNums[i], actual: aNums[i], delta });
    }
  }
  return { tolerant: diffs.length > 0, diffs };
}

function compareCases(cases, actuals) {
  return cases.map((testCase, index) => {
    const actual = actuals[index] ?? { error: 'missing-result', html: '' };
    const expectedHtml = normalizeHtml(testCase.expectedHtml);
    const actualHtml = normalizeHtml(actual.html ?? '');
    const expectedError = testCase.expectedError;
    const actualError = normalizeActualError(
      actual.error ?? 'no-error',
      actualHtml,
      expectedError,
      expectedHtml
    );
    const errorMatch = expectedError === actualError;
    const htmlMatch = expectedHtml === actualHtml;
    const tol = htmlMatch
      ? { tolerant: false, diffs: [] }
      : compareEmTolerant(expectedHtml, actualHtml, EM_TOLERANCE);
    const htmlPass = htmlMatch || tol.tolerant;

    return {
      ...testCase,
      actualError,
      actualHtml: actual.html ?? '',
      pass: errorMatch && htmlPass,
      errorMatch,
      htmlMatch,
      // tolerant: passed only because every em-diff was within EM_TOLERANCE
      emTolerant: tol.tolerant,
      emDiffs: tol.diffs,
    };
  });
}

function normalizeActualError(actualError, actualHtml, expectedError, expectedHtml) {
  if (
    actualError === 'no-error' &&
    expectedError === 'unknown-command' &&
    actualHtml.includes('lm_error')
  ) {
    return 'unknown-command';
  }
  if (
    actualError === 'no-error' &&
    expectedError === 'unexpected-delimiter' &&
    (actualHtml === expectedHtml || actualHtml.includes('lm_error'))
  ) {
    return 'unexpected-delimiter';
  }
  return actualError;
}

function summarize(results) {
  const summary = {
    total: results.length,
    passed: results.filter((x) => x.pass).length,
    failed: results.filter((x) => !x.pass).length,
    byCategory: {},
  };

  for (const result of results) {
    const bucket = summary.byCategory[result.category] ?? {
      total: 0,
      passed: 0,
      failed: 0,
    };
    bucket.total += 1;
    if (result.pass) bucket.passed += 1;
    else bucket.failed += 1;
    summary.byCategory[result.category] = bucket;
  }

  return summary;
}

function buildBaselineReport(results, baselineSet, baselinePath) {
  if (!baselineSet) return null;

  const covered = results.filter((result) => baselineSet.has(result.key));
  const regressions = covered.filter((result) => !result.pass);
  return {
    path: path.relative(PROJECT_ROOT, baselinePath),
    expected: baselineSet.size,
    covered: covered.length,
    regressions: regressions.map((result) => ({
      key: result.key,
      category: result.category,
      formula: result.formula,
      errorMatch: result.errorMatch,
      htmlMatch: result.htmlMatch,
      expectedError: result.expectedError,
      actualError: result.actualError,
    })),
  };
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const snapshotFixtures = selectedSnapshotFixtures(opts.fixtureSource)
    .filter((fixture) => fs.existsSync(fixture.path));
  const snapshotText = snapshotFixtures
    .map((fixture) => fs.readFileSync(fixture.path, 'utf8'))
    .join('\n');
  let cases = buildCases(parseSnapshots(snapshotText));

  if (opts.category) {
    const needle = opts.category.toLowerCase();
    cases = cases.filter((testCase) => testCase.category.toLowerCase().includes(needle));
  }
  if (opts.limit != null) cases = cases.slice(0, opts.limit);

  if (opts.list) {
    for (const testCase of cases) {
      console.log(`${testCase.category}\t${testCase.formula}\t${testCase.key}`);
    }
    console.log(`\n${cases.length} extracted Lambda-adapted MathLive markup cases`);
    return;
  }

  if (cases.length === 0) throw new Error('no runnable MathLive markup cases extracted');

  const baselineSet = opts.noBaseline ? null : readBaseline(opts.baseline);
  const actuals = await runLambda(cases, opts);
  const results = compareCases(cases, actuals);

  // Warn about every case that passed ONLY via the em-tolerance, so a sub-pixel
  // pass never goes unnoticed.
  const tolerantPasses = results.filter((x) => x.emTolerant && x.pass);
  for (const tp of tolerantPasses) {
    const detail = tp.emDiffs
      .map((d) => `${d.expected}em→${d.actual}em (Δ${d.delta.toFixed(4)})`)
      .join(', ');
    console.warn(
      `⚠ em-tolerance pass (≤${EM_TOLERANCE}em) [${tp.category}] ${tp.key}: ${detail}`
    );
  }

  const summary = summarize(results);
  const baseline = buildBaselineReport(results, baselineSet, opts.baseline);
  const report = {
    generatedScript: path.relative(PROJECT_ROOT, opts.script),
    fixtureSource: opts.fixtureSource,
    sourceSnapshots: snapshotFixtures
      .map((fixture) => path.relative(PROJECT_ROOT, fixture.path)),
    baseline,
    jobs: Math.min(opts.jobs, cases.length),
    summary,
    results,
  };

  fs.mkdirSync(path.dirname(opts.report), { recursive: true });
  fs.writeFileSync(opts.report, `${JSON.stringify(report, null, 2)}\n`);

  console.log(`Lambda MathLive markup adapter`);
  console.log(`  cases:  ${summary.total}`);
  console.log(`  passed: ${summary.passed}`);
  if (tolerantPasses.length > 0) {
    console.log(`    (incl. ${tolerantPasses.length} em-tolerance pass(es) ≤${EM_TOLERANCE}em — see warnings)`);
  }
  console.log(`  failed: ${summary.failed}`);
  console.log(`  jobs:   ${Math.min(opts.jobs, cases.length)}`);
  if (baseline) {
    console.log(
      `  baseline: ${baseline.path} (${baseline.expected} tests, ${baseline.covered} covered)`
    );
    console.log(`  regressions: ${baseline.regressions.length}`);
  }
  console.log(`  report: ${path.relative(PROJECT_ROOT, opts.report)}`);
  console.log(``);
  for (const [category, bucket] of Object.entries(summary.byCategory)) {
    console.log(
      `  ${category}: ${bucket.passed}/${bucket.total} passed (${bucket.failed} failed)`
    );
  }

  const firstFailures = results.filter((x) => !x.pass).slice(0, 5);
  if (firstFailures.length > 0) {
    console.log(`\nFirst failures:`);
    for (const failure of firstFailures) {
      console.log(`  - ${failure.key}`);
      if (!failure.errorMatch) {
        console.log(`    error expected ${failure.expectedError}, got ${failure.actualError}`);
      }
      if (!failure.htmlMatch) {
        console.log(`    html differs`);
      }
    }
  }

  if (baseline && baseline.regressions.length > 0) {
    console.log(`\nBaseline regressions:`);
    for (const regression of baseline.regressions.slice(0, 10)) {
      console.log(`  - ${regression.key}`);
      if (!regression.errorMatch) {
        console.log(
          `    error expected ${regression.expectedError}, got ${regression.actualError}`
        );
      }
      if (!regression.htmlMatch) {
        console.log(`    html differs`);
      }
    }
  }

  if (opts.strict && summary.failed > 0) process.exit(1);
}

try {
  await main();
} catch (err) {
  console.error(err.stack || err.message);
  process.exit(1);
}
