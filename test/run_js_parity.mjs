#!/usr/bin/env node
// JS execution-tier parity runner.
//
// Runs test_js_gtest and the Test262 baseline under each pinned execution tier
// (JS_EXECUTION_BACKEND=mir via --full-mir / --mir-only, and the full AST
// interpreter via --full-ast / --ast-only), then reports per-leg results and
// the cross-tier divergence: cases that fail under one tier but not the other.
//
// When invoked by test/test_run.js, it accepts the runner's --report argument
// and emits the summary/results shape consumed by the shared Node adapter lane.

import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const PROJECT_ROOT = path.resolve(path.dirname(__filename), '..');
const OUT_DIR = path.join(PROJECT_ROOT, 'temp', 'js_parity');
const HEARTBEAT_MS = 30 * 1000;

const MODES = ['mir', 'ast'];
const SUITES = {
  js: {
    label: 'test_js_gtest',
    exe: 'test/test_js_gtest.exe',
    args: (mode, jsonPath) => [
      mode === 'mir' ? '--full-mir' : '--full-ast',
      '--baseline',
      '--gtest_brief=1',
      '--gtest_color=no',
      `--gtest_output=json:${jsonPath}`,
    ],
  },
  test262: {
    label: 'test262',
    exe: 'test/test_js_test262_gtest.exe',
    requires: 'ref/test262',
    args: (mode) => [
      mode === 'mir' ? '--mir-only' : '--ast-only',
      '--baseline-only',
      '--batch-only',
      '--run-async',
      '--async-list=test/js262/test262_baseline.txt',
    ],
  },
};

function parseArgs(argv) {
  const opts = {
    suites: Object.keys(SUITES),
    modes: MODES.slice(),
    report: path.join(OUT_DIR, 'report.json'),
    verbose: false,
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    const value = (name) => (arg.startsWith(`${name}=`) ? arg.slice(name.length + 1) : argv[++i]);
    if (arg === '--report' || arg.startsWith('--report=')) opts.report = path.resolve(value('--report'));
    else if (arg === '--suite' || arg.startsWith('--suite=')) {
      const suite = value('--suite');
      opts.suites = suite === 'all' ? Object.keys(SUITES) : [suite];
    } else if (arg === '--mode' || arg.startsWith('--mode=')) {
      const mode = value('--mode');
      opts.modes = mode === 'all' ? MODES.slice() : [mode];
    } else if (arg === '--verbose') opts.verbose = true;
    else if (arg === '--help' || arg === '-h') {
      console.log(`Usage: node test/run_js_parity.mjs [options]

Runs test_js_gtest (--baseline) and the Test262 baseline under pinned MIR and
full AST interpreter modes, then reports results and cross-tier divergence.

Options:
  --suite=js|test262|all   Suites to run (default: all)
  --mode=mir|ast|all       Execution tiers to run (default: all)
  --report PATH            JSON report path (default: temp/js_parity/report.json)
  --verbose                Stream child output (always saved to temp/js_parity/*.log)
`);
      process.exit(0);
    } else {
      console.error(`Unknown option: ${arg}`);
      process.exit(2);
    }
  }
  for (const s of opts.suites) if (!SUITES[s]) { console.error(`Unknown suite: ${s}`); process.exit(2); }
  for (const m of opts.modes) if (!MODES.includes(m)) { console.error(`Unknown mode: ${m}`); process.exit(2); }
  return opts;
}

function runChild(exe, args, logPath, verbose) {
  return new Promise((resolve) => {
    const log = fs.createWriteStream(logPath);
    const child = spawn(`./${exe}`, args, { cwd: PROJECT_ROOT, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '';
    // quiet legs still need periodic output, or test_run.js's idle timer kills them
    const heartbeat = verbose ? null : setInterval(() => {
      console.log(`   ... ${path.basename(logPath, '.log')} still running`);
    }, HEARTBEAT_MS);
    child.stdout.on('data', (data) => {
      stdout += data;
      log.write(data);
      if (verbose) process.stdout.write(data);
    });
    child.stderr.on('data', (data) => {
      log.write(data);
      if (verbose) process.stderr.write(data);
    });
    const finish = (code, error) => {
      if (heartbeat) clearInterval(heartbeat);
      log.end();
      resolve({ code, stdout, error });
    };
    child.on('error', (err) => finish(-1, err.message));
    child.on('close', (code, signal) => finish(code ?? -1, signal ? `killed by ${signal}` : null));
  });
}

// GTest JSON → per-case failures; every listed non-skipped case counts.
function parseGtestJson(jsonPath) {
  const data = JSON.parse(fs.readFileSync(jsonPath, 'utf8'));
  let total = 0;
  const failures = [];
  for (const suite of data.testsuites || []) {
    for (const tc of suite.testsuite || []) {
      if (tc.result === 'SKIPPED' || tc.status === 'NOTRUN') continue;
      total++;
      if (Array.isArray(tc.failures) && tc.failures.length > 0) failures.push(`${suite.name}.${tc.name}`);
    }
  }
  return { total, failures };
}

// Test262 --batch-only prints a "Regression Check vs Baseline" box followed by
// a "  - <name>  [...]" entry per regression; a message may span lines.
function parseTest262Output(stdout) {
  const baseline = stdout.match(/Baseline passing:\s*(\d+)/);
  const regressions = stdout.match(/Regressions:\s*(\d+)/);
  if (!baseline || !regressions) return null;
  const failures = [];
  const listStart = stdout.indexOf('REGRESSIONS (');
  if (listStart >= 0) {
    for (const line of stdout.slice(listStart).split('\n').slice(1)) {
      const m = line.match(/^ {2}- (\S+)/);
      if (m) failures.push(m[1]);
      else if (/^\S/.test(line) && !line.startsWith(']')) break;  // next report section
    }
  }
  const count = parseInt(regressions[1], 10);
  // keep the count authoritative even if the list format changes
  while (failures.length < count) failures.push(`<unlisted regression ${failures.length + 1}>`);
  return { total: parseInt(baseline[1], 10), failures };
}

async function runLeg(suiteKey, mode, verbose) {
  const suite = SUITES[suiteKey];
  const name = `${suiteKey}_${mode}`;
  const leg = { suite: suite.label, mode, total: 0, passed: 0, failed: 0, failures: [], error: null };
  if (!fs.existsSync(path.join(PROJECT_ROOT, suite.exe))) {
    leg.error = `${suite.exe} not found (run make build-test)`;
    return leg;
  }
  if (suite.requires && !fs.existsSync(path.join(PROJECT_ROOT, suite.requires))) {
    leg.skipped = `${suite.requires} not present`;
    return leg;
  }
  const jsonPath = path.join(OUT_DIR, `${name}.json`);
  const logPath = path.join(OUT_DIR, `${name}.log`);
  try { fs.unlinkSync(jsonPath); } catch (_) {}
  console.log(`▶ ${suite.label} [${mode}] ${suite.exe} ${suite.args(mode, jsonPath).join(' ')}`);
  const start = Date.now();
  const run = await runChild(suite.exe, suite.args(mode, jsonPath), logPath, verbose);
  leg.seconds = (Date.now() - start) / 1000;
  leg.exitCode = run.code;
  leg.log = path.relative(PROJECT_ROOT, logPath);

  let parsed = null;
  try {
    parsed = suiteKey === 'js' ? parseGtestJson(jsonPath) : parseTest262Output(run.stdout);
  } catch (_) {}
  if (!parsed) {
    leg.error = `no result summary (exit ${run.code}${run.error ? `, ${run.error}` : ''}); see ${leg.log}`;
    return leg;
  }
  leg.total = parsed.total;
  leg.failures = parsed.failures.sort();
  leg.failed = leg.failures.length;
  leg.passed = leg.total - leg.failed;
  if (run.code !== 0 && leg.failed === 0) {
    leg.error = `exit ${run.code}${run.error ? ` (${run.error})` : ''} with no failing case; see ${leg.log}`;
  }
  console.log(`  ${leg.failed === 0 && !leg.error ? '✅' : '❌'} ${leg.passed}/${leg.total} passed in ${leg.seconds.toFixed(1)}s`);
  return leg;
}

function divergence(legs) {
  const out = [];
  for (const suiteKey of Object.keys(SUITES)) {
    const label = SUITES[suiteKey].label;
    const mir = legs.find((l) => l.suite === label && l.mode === 'mir' && !l.error && !l.skipped);
    const ast = legs.find((l) => l.suite === label && l.mode === 'ast' && !l.error && !l.skipped);
    if (!mir || !ast) continue;
    const mirSet = new Set(mir.failures);
    const astSet = new Set(ast.failures);
    out.push({
      suite: label,
      astOnly: ast.failures.filter((f) => !mirSet.has(f)),
      mirOnly: mir.failures.filter((f) => !astSet.has(f)),
      both: ast.failures.filter((f) => mirSet.has(f)),
    });
  }
  return out;
}

function printReport(legs, diffs) {
  const pad = (s, n) => String(s).padEnd(n);
  const lpad = (s, n) => String(s).padStart(n);
  console.log('\n╔════════════════════════════════════════════════════════════════╗');
  console.log('║                  JS Execution-Tier Parity                      ║');
  console.log('╠════════════════════════════════════════════════════════════════╣');
  console.log(`║  ${pad('Suite', 15)}${pad('Tier', 6)}${lpad('Passed', 8)}${lpad('Total', 8)}${lpad('Failed', 8)}${lpad('Time', 9)}        ║`);
  for (const leg of legs) {
    const time = leg.seconds !== undefined ? `${leg.seconds.toFixed(0)}s` : '-';
    const status = leg.skipped ? 'SKIP' : leg.error ? 'ERR' : leg.failed ? 'FAIL' : 'ok';
    console.log(`║  ${pad(leg.suite, 15)}${pad(leg.mode, 6)}${lpad(leg.passed, 8)}${lpad(leg.total, 8)}${lpad(leg.failed, 8)}${lpad(time, 9)}  ${pad(status, 6)}║`);
  }
  console.log('╚════════════════════════════════════════════════════════════════╝');

  const listCap = 40;
  const printList = (title, names) => {
    if (names.length === 0) return;
    console.log(`\n  ${title} (${names.length}):`);
    for (const n of names.slice(0, listCap)) console.log(`    - ${n}`);
    if (names.length > listCap) console.log(`    ... ${names.length - listCap} more (see report JSON)`);
  };
  for (const leg of legs) {
    if (leg.skipped) console.log(`\n  ⏭  ${leg.suite} [${leg.mode}] skipped: ${leg.skipped}`);
    if (leg.error) console.log(`\n  ⚠️  ${leg.suite} [${leg.mode}] error: ${leg.error}`);
  }
  for (const d of diffs) {
    console.log(`\n  ${d.suite}: ${d.astOnly.length} AST-only, ${d.mirOnly.length} MIR-only, ${d.both.length} both-tier failures`);
    printList(`${d.suite} fails only under AST`, d.astOnly);
    printList(`${d.suite} fails only under MIR`, d.mirOnly);
    printList(`${d.suite} fails under both tiers`, d.both);
  }
  // legs without a counterpart tier still list their failures
  for (const leg of legs) {
    if (diffs.some((d) => d.suite === leg.suite)) continue;
    printList(`${leg.suite} [${leg.mode}] failures`, leg.failures);
  }
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  fs.mkdirSync(OUT_DIR, { recursive: true });
  const legs = [];
  // legs run sequentially: both runners fan out their own worker processes,
  // and test262 holds a single-run lock
  for (const suiteKey of opts.suites) {
    for (const mode of opts.modes) legs.push(await runLeg(suiteKey, mode, opts.verbose));
  }
  const diffs = divergence(legs);
  printReport(legs, diffs);

  const results = [];
  for (const leg of legs) {
    if (leg.skipped) continue;
    if (leg.error) results.push({ name: `${leg.suite}[${leg.mode}] ${leg.error}`, pass: false });
    for (const f of leg.failures) results.push({ name: `${leg.suite}[${leg.mode}] ${f}`, pass: false });
  }
  const total = legs.reduce((n, l) => n + l.total, 0) + legs.filter((l) => l.error).length;
  const failed = results.length;
  const report = {
    summary: { total, passed: total - failed, failed },
    legs,
    divergence: diffs,
    results,
  };
  fs.mkdirSync(path.dirname(opts.report), { recursive: true });
  fs.writeFileSync(opts.report, `${JSON.stringify(report, null, 2)}\n`);
  console.log(`\n  report: ${path.relative(PROJECT_ROOT, opts.report)}`);
  process.exit(failed === 0 ? 0 : 1);
}

main();
