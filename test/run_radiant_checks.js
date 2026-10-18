#!/usr/bin/env node
'use strict';

// Adapt the existing layout/snapshot/render verdicts to the shared runner protocol.
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');
const { createWorkspace } = require('./test_workspace.js');

const ROOT = path.resolve(__dirname, '..');

function run(command, args, options = {}) {
    return new Promise((resolve, reject) => {
        const { input, ...spawnOptions } = options;
        const child = spawn(command, args, {
            cwd: ROOT,
            env: { LAMBDA_AUTO_CLOSE: '1', LAMBDA_POST_LOAD_SETTLE_MS: '200',
                ...process.env, LAMBDA_ROOT: ROOT,
                LAMBDA_HOME: path.resolve(ROOT, process.env.LAMBDA_HOME || 'lmd') },
            stdio: ['pipe', 'pipe', 'pipe'],
            ...spawnOptions,
        });
        let output = '';
        child.stdout.on('data', data => { output += data; process.stdout.write(data); });
        child.stderr.on('data', data => process.stderr.write(data));
        child.on('error', reject);
        child.on('close', (code, signal) => resolve({ code, signal, output }));
        child.stdin.on('error', error => { if (error.code !== 'EPIPE') reject(error); });
        child.stdin.end(input || '');
    });
}

function layoutWorkspace(check) {
    // the external layout harness clears cwd/temp/layout; give each check its own cwd.
    return createWorkspace(ROOT, `radiant-${check}-`);
}

function count(output, pattern) {
    const match = output.match(pattern);
    return match ? Number(match[1]) : 0;
}

function checkJobs(requested) {
    const limit = Number(process.env.LAMBDA_TEST_MAX_CONCURRENT);
    const ceiling = Number.isInteger(limit) && limit > 0 ? limit : Math.max(1, os.cpus().length - 1);
    const jobs = Number(requested);
    return Math.min(ceiling, Number.isInteger(jobs) && jobs > 0 ? jobs : ceiling);
}

async function layout(report) {
    // The layout-only Make target owns baseline selection and classification.
    const workspace = layoutWorkspace('layout');
    const resultFile = path.join(path.dirname(report), '_layout_baseline_results.txt');
    fs.rmSync(resultFile, { force: true });
    const child = await run('make', ['--no-print-directory', '-f', path.join(ROOT, 'Makefile'), 'run-layout-baseline-suites',
        `LAYOUT_BASELINE_RESULTS=${resultFile}`,
        `LAYOUT_BASELINE_CONCURRENCY=${checkJobs(process.env.LAYOUT_BASELINE_CONCURRENCY ||
            process.env.LAMBDA_TEST_MAX_CONCURRENT || 4)}`],
        { cwd: workspace });
    const suites = fs.readFileSync(resultFile, 'utf8').trim().split('\n').filter(Boolean).map(line => {
        const [name, status, passed, partial, failed, skipped, elapsed] = line.split('|');
        const numbers = [passed, partial, failed, skipped, elapsed].map(Number);
        if (numbers.some(n => !Number.isInteger(n) || n < 0)) {
            throw new Error(`Invalid layout record: ${line}`);
        }
        return { name, status, passed: numbers[0], partial: numbers[1], failed: numbers[2],
            skipped: numbers[3], duration_ms: numbers[4] * 1000 };
    });
    if (!suites.length) throw new Error('Layout runner returned no suite records');
    const summary = { passed: 0, partial: 0, failed: 0, skipped: 0 };
    for (const suite of suites) {
        if (suite.status.includes('FAIL') && suite.failed === 0) suite.failed = 1;
        for (const key of Object.keys(summary)) summary[key] += suite[key];
    }
    if ((child.code !== 0 || child.signal) && summary.failed === 0) summary.failed = 1;
    summary.total = summary.passed + summary.partial + summary.failed + summary.skipped;
    return { summary, suites, workspace,
        results: suites.map(s => ({ name: s.name, pass: !s.status.includes('FAIL') })) };
}

async function snapshot() {
    const workspace = layoutWorkspace('snapshot');
    const measured = await run(process.execPath, [path.join(ROOT, 'test/layout/test_radiant_layout.js'),
        '--engine', 'lambda-css', '-c', 'page', '--json', '-j', String(checkJobs(5))], { cwd: workspace });
    const checked = await run(process.execPath, ['test/layout/layout_suite_snapshot.js',
        '--check', 'page'], { input: measured.output });
    return { ...snapshot_result(measured, checked), workspace };
}

function snapshot_result(measured, checked) {
    const passed = count(checked.output, /^Current:\s*(\d+) tests/m);
    // Layout exit 1 includes per-file regressions that the average-based snapshot
    // deliberately permits. The checker validates inventory and runner integrity.
    const failed = ![0, 1].includes(measured.code) || checked.code !== 0 ||
        measured.signal || checked.signal || passed === 0 ? 1 : 0;
    return { summary: { passed, failed, total: passed + failed },
        results: [{ name: 'page snapshot inventory, integrity and averages', pass: failed === 0 }] };
}

async function render() {
    const jobs = String(checkJobs(process.env.RADIANT_RENDER_JOBS));
    const child = await run(process.execPath, ['test_radiant_render.js', '-j', jobs, '--baseline'],
        { cwd: path.join(ROOT, 'test/render') });
    const line = child.output.split('\n').reverse().find(line => line.startsWith('Results:')) || '';
    const passed = count(line, /^Results: (\d+)/);
    const total = count(line, /^Results: \d+\/(\d+)/);
    const skipped = count(line, /(\d+) skipped/);
    let failed = count(child.output, /Baseline Regressions \((\d+)\)/);
    // Baseline verdicts permit recorded pixel differences and expected failures;
    // retain those as partial results, while startup errors still fail the gate.
    if ((child.code !== 0 || child.signal || total === 0) && failed === 0) failed = 1;
    const partial = Math.max(0, total - passed - skipped - failed);
    return { summary: { passed, partial, failed, skipped, total: passed + partial + failed + skipped },
        details: line || 'Render runner returned no result summary',
        results: [{ name: 'render visual baseline regressions', pass: failed === 0 }] };
}

async function main() {
    const args = process.argv.slice(2);
    const reportIndex = args.indexOf('--report');
    const reportArg = args[reportIndex + 1];
    if (reportIndex < 0 || !reportArg) throw new Error('Expected --report <path>');
    const report = path.resolve(reportArg);
    fs.mkdirSync(path.dirname(report), { recursive: true });
    const checks = { layout, snapshot, render };
    let result;
    try {
        if (!checks[args[0]]) throw new Error(`Unknown Radiant check: ${args[0]}`);
        result = await checks[args[0]](report);
    } catch (error) {
        console.error(`Radiant check failed: ${error.message}`);
        result = { summary: { passed: 0, failed: 1, total: 1 },
            results: [{ name: error.message, pass: false }] };
    }
    fs.writeFileSync(report, JSON.stringify(result, null, 2) + '\n');
    process.exitCode = result.summary.failed ? 1 : 0;
}

if (require.main === module) {
    main().catch(error => { console.error(error); process.exitCode = 1; });
}
module.exports = { snapshot_result };
