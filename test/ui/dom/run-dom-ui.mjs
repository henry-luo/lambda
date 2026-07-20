#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { availableParallelism, cpus } from 'node:os';
import { basename, join } from 'node:path';
import { readdir, readFile } from 'node:fs/promises';

const fixtureDir = 'test/ui/dom';
const defaultJobs = Math.max(1,
  (typeof availableParallelism === 'function' ? availableParallelism() : cpus().length) - 1);

function parsePositiveInteger(value, option) {
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < 1) {
    throw new Error(`${option} must be a positive integer, got '${value}'`);
  }
  return parsed;
}

function parseArgs(argv) {
  const options = { jobs: defaultJobs, test: null };
  for (let index = 0; index < argv.length; index += 1) {
    if (argv[index] === '--jobs' && argv[index + 1]) {
      options.jobs = parsePositiveInteger(argv[++index], '--jobs');
    } else if (argv[index] === '--test' && argv[index + 1]) {
      options.test = argv[++index];
    } else {
      throw new Error(`unknown or incomplete option '${argv[index]}'`);
    }
  }
  return options;
}

async function loadFixtures(selector) {
  let paths;
  if (selector) {
    const selectedPath = selector.endsWith('.json') ? selector : join(fixtureDir, `${selector}.json`);
    if (!existsSync(selectedPath)) throw new Error(`DOM UI fixture not found: ${selectedPath}`);
    paths = [selectedPath];
  } else {
    paths = (await readdir(fixtureDir))
      .filter((name) => name.endsWith('.json'))
      .sort()
      .map((name) => join(fixtureDir, name));
  }

  return Promise.all(paths.map(async (jsonPath) => {
    const fixture = JSON.parse(await readFile(jsonPath, 'utf8'));
    const name = basename(jsonPath, '.json');
    return {
      name,
      jsonPath,
      page: fixture.html || join(fixtureDir, `${name}.html`),
    };
  }));
}

function runFixture(fixture, batch) {
  return new Promise((resolve) => {
    const args = ['view', fixture.page, '--event-file', fixture.jsonPath, '--headless'];
    // Parallel fixtures share the working directory, so batch logging would
    // race on log.txt. A selected single fixture retains logs for diagnosis.
    if (batch) args.push('--no-log');

    const child = spawn('./lambda.exe', args, { stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    child.stdout.on('data', (chunk) => { output += chunk; });
    child.stderr.on('data', (chunk) => { output += chunk; });
    child.once('error', (error) => resolve({ fixture, passed: false, error, output }));
    child.once('exit', (code, signal) => resolve({
      fixture,
      passed: code === 0,
      code,
      signal,
      output,
    }));
  });
}

async function runBounded(fixtures, jobs) {
  const results = new Array(fixtures.length);
  let nextIndex = 0;

  async function worker() {
    while (nextIndex < fixtures.length) {
      const index = nextIndex++;
      results[index] = await runFixture(fixtures[index], fixtures.length > 1);
    }
  }

  const workerCount = Math.min(jobs, fixtures.length);
  await Promise.all(Array.from({ length: workerCount }, () => worker()));
  return results;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const fixtures = await loadFixtures(options.test);
  const results = await runBounded(fixtures, options.jobs);
  let passed = 0;

  for (const result of results) {
    if (result.passed) {
      passed += 1;
      console.log(`  \x1b[32m✓\x1b[0m ${result.fixture.name}`);
    } else {
      const detail = result.error?.message || (result.signal ? `signal ${result.signal}` : `exit ${result.code}`);
      console.log(`  \x1b[31m✗\x1b[0m ${result.fixture.name} (${detail})`);
      if (result.output.trim()) {
        console.log(result.output.trim().split('\n').map((line) => `      ${line}`).join('\n'));
      }
    }
  }

  console.log('==============================================================');
  console.log(`dom-ui: ${passed}/${results.length} passed`);
  if (passed !== results.length) process.exitCode = 1;
}

main().catch((error) => {
  console.error(`dom-ui runner: ${error.message}`);
  process.exitCode = 1;
});
