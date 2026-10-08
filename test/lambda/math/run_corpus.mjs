#!/usr/bin/env node
// Exercise the public SVG renderer over the reference formula corpus.
// Geometry/serialization smoke coverage; font-specific assertions live in .ls tests.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { readCorpus } from './corpus.mjs';
import { parse_lambda_json } from './lambda_math_renderer.mjs';

const PROJECT_ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const TEMP_DIR = path.join(PROJECT_ROOT, 'temp');

function parseArgs(argv) {
  const opts = {
    fixtureSource: 'all', category: null, limit: null, list: false,
    jobs: Math.max(1, os.cpus().length - 1),
    lambda: path.join(PROJECT_ROOT, 'lambda.exe'),
    script: path.join(TEMP_DIR, 'math_corpus_batch.ls'),
    report: path.join(TEMP_DIR, 'math_corpus_report.json'),
  };
  const values = { '--fixture-source': 'fixtureSource', '--category': 'category',
    '--limit': 'limit', '--jobs': 'jobs', '--lambda': 'lambda', '--script': 'script', '--report': 'report' };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--list') opts.list = true;
    else if (arg === '--help' || arg === '-h') {
      console.log('Usage: node test/lambda/math/run_corpus.mjs [--fixture-source all|mathlive|lambda-input] [--category NAME] [--limit N] [--jobs N] [--lambda PATH] [--script PATH] [--report PATH] [--list]');
      process.exit(0);
    } else if (values[arg]) {
      const value = argv[++i];
      if (value == null) throw new Error(`missing value for ${arg}`);
      opts[values[arg]] = value;
    } else throw new Error(`unknown argument: ${arg}`);
  }
  opts.jobs = Number(opts.jobs);
  if (!Number.isInteger(opts.jobs) || opts.jobs < 1) throw new Error('--jobs must be positive');
  if (opts.limit != null) {
    opts.limit = Number(opts.limit);
    if (!Number.isInteger(opts.limit) || opts.limit < 1) throw new Error('--limit must be positive');
  }
  for (const key of ['lambda', 'script', 'report']) opts[key] = path.resolve(opts[key]);
  return opts;
}

function buildLambdaScript(cases) {
  const literals = cases.map((c) => JSON.stringify(c.formula)).join(',\n');
  return `import math: lambda.doc.math.math

fn check(formula) {
    let parsed = parse(formula, {type: "math", flavor: "latex"}) ^ { ^ }
    // Reference snapshots were generated in display style, including bare formulas.
    let box = if (parsed is error) parsed else math.render_box(parsed, {display: true})
    if (box is error) {error: string(box)}
    else {
        let html = format(box.element, 'html');
        {width: box.width, height: box.height, depth: box.depth,
         svg: name(box.element) == 'svg' and box.element.class == "lambda-math",
         view_box: box.element.viewBox,
         finite: box.width - box.width == 0 and box.height - box.height == 0 and box.depth - box.depth == 0,
         html: html}
    }
}
format([for (formula in [${literals}]) check(formula)], 'json')
`;
}

function checkGeometry(actual) {
  if (!actual) return 'missing result';
  if (actual.error) return actual.error;
  if (!actual.svg) return 'missing public math SVG';
  if (!actual.finite || ![actual.width, actual.height, actual.depth].every(Number.isFinite))
    return 'nonfinite measured dimensions';
  const viewBox = actual.view_box?.trim().split(/\s+/).map(Number);
  if (viewBox?.length !== 4 || !viewBox.every(Number.isFinite) || viewBox[2] <= 0 || viewBox[3] <= 0)
    return 'invalid SVG viewBox';
  // Inspect attributes only: source titles may legitimately contain words such as "inf".
  const attributes = [...actual.html.matchAll(/(?:d|transform|viewBox|width|height|x|y|style)="([^"]*)"/g)];
  if (attributes.some(([, value]) => /(?:^|[^A-Za-z])(?:NaN|[+-]?(?:Infinity|inf))(?:$|[^A-Za-z])/i.test(value)))
    return 'nonfinite SVG geometry';
  if (/<(?:text|link|style)\b/.test(actual.html) || /class="lm_/.test(actual.html))
    return 'external-font or legacy markup dependency';
  return null;
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
        resolve(parse_lambda_json(stdout));
      } catch (err) {
        reject(err);
      }
    });
  });
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

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const corpus = readCorpus(opts.fixtureSource);
  let cases = corpus.cases;
  if (opts.category) cases = cases.filter((c) => c.category.toLowerCase().includes(opts.category.toLowerCase()));
  if (opts.limit != null) cases = cases.slice(0, opts.limit);
  if (cases.length === 0) throw new Error('no formulas selected');
  if (opts.list) {
    for (const c of cases) console.log(`${c.category}\t${c.formula}\t${c.key}`);
    console.log(`${cases.length} formulas`);
    return;
  }
  const actuals = await runLambda(cases, opts);
  if (actuals.length !== cases.length) throw new Error(`expected ${cases.length} results, received ${actuals.length}`);
  const results = cases.map((c, i) => {
    const error = checkGeometry(actuals[i]);
    return { ...c, pass: error === null, error };
  });
  const summary = summarize(results);
  fs.mkdirSync(path.dirname(opts.report), { recursive: true });
  fs.writeFileSync(opts.report, JSON.stringify({
    fixtureSource: opts.fixtureSource, sourceSnapshots: corpus.snapshots,
    summary, results,
  }, null, 2) + '\n');
  console.log(`Lambda math SVG corpus: ${summary.passed}/${summary.total} passed, ${summary.failed} failed`);
  console.log(`Report: ${path.relative(PROJECT_ROOT, opts.report)}`);
  for (const result of results.filter((r) => !r.pass).slice(0, 10))
    console.error(`${result.key}: ${result.error}`);
  if (summary.failed > 0) process.exitCode = 1;
}

try { await main(); }
catch (error) { console.error(error.stack || error.message); process.exitCode = 1; }
