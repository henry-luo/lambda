#!/usr/bin/env node
'use strict';

// keep the layout harness's category isolation and classification in one shared pool.
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const INPUT_EVENTS_CATEGORY = 'wpt-input-events';

function classifyInputEvents(data, baseline, output) {
    let passed = 0, skipped = 0, failed = 0;
    const cases = (data.testsuites || []).flatMap(suite => suite.testsuite || []);
    const names = new Map();
    for (const row of cases) {
        const name = row.name?.replace(/^Run\//, '');
        if (!name || names.has(name)) throw new Error('invalid or duplicate input-events case');
        names.set(name, row);
        if (row.result === 'SKIPPED') skipped++;
        else if (row.result === 'COMPLETED' && !row.failures?.length) passed++;
        else failed++;
    }
    if (!cases.length || !baseline.length || data.tests !== cases.length || baseline.some(name => {
        const row = names.get(name);
        return !row || row.result !== 'COMPLETED' || row.failures?.length;
    })) throw new Error('input-events inventory is incomplete or a required case failed');
    if (output.code !== 0 || output.signal) failed = Math.max(1, failed);
    return { passed, partial: 0, failed, skipped };
}

async function runInputEventsCategory(jobs) {
    const start = Date.now();
    const reportDir = fs.mkdtempSync(path.join(process.cwd(), 'temp', 'wpt-input-layout-'));
    const report = path.join(reportDir, 'results.json');
    const executable = path.resolve('test/test_wpt_input_events_gtest.exe');
    const output = await new Promise((resolve, reject) => {
        const child = spawn(executable, ['--jobs', String(jobs), `--gtest_output=json:${report}`], {
            cwd: process.cwd(), stdio: ['ignore', 'pipe', 'pipe'],
        });
        child.stdout.on('data', chunk => process.stdout.write(chunk));
        child.stderr.on('data', chunk => process.stderr.write(chunk));
        child.on('error', reject);
        child.on('close', (code, signal) => resolve({ code, signal }));
    });
    let passed = 0, skipped = 0, failed = 0;
    try {
        const data = JSON.parse(fs.readFileSync(report, 'utf8'));
        const baseline = fs.readFileSync('test/wpt/wpt_input_events_baseline.txt', 'utf8')
            .split(/\r?\n/).map(line => line.trim()).filter(line => line && !line.startsWith('#'));
        ({ passed, skipped, failed } = classifyInputEvents(data, baseline, output));
    } catch (error) {
        console.error(`Input-events layout result error: ${error.message}`);
        failed = Math.max(1, failed);
    } finally {
        fs.rmSync(reportDir, { recursive: true, force: true });
    }
    return { name: INPUT_EVENTS_CATEGORY, passed, partial: 0, failed, skipped,
        duration_ms: Date.now() - start };
}

async function runCategories(tester, categories, runInputEvents = runInputEventsCategory) {
    const start = Date.now();
    const layoutCategories = categories.filter(category => category !== INPUT_EVENTS_CATEGORY);
    const results = layoutCategories.length ? await tester.testCategories(layoutCategories) : [];
    const inputEvents = categories.includes(INPUT_EVENTS_CATEGORY)
        ? await runInputEvents(tester.maxConcurrency) : null;
    let offset = 0;
    const records = [];
    for (const category of categories) {
        if (category === INPUT_EVENTS_CATEGORY) {
            records.push(inputEvents);
            continue;
        }
        const summary = tester._categorySummaries?.[category];
        if (!summary) {
            records.push({ name: category, passed: 0, partial: 0, failed: 1, skipped: 0,
                duration_ms: Date.now() - start });
            continue;
        }
        if (!Number.isInteger(summary.total) || summary.total < 0 || offset + summary.total > results.length)
            throw new Error(`Invalid layout result count for ${category}`);
        // testCategories concatenates results in category order, even when names repeat.
        const rows = results.slice(offset, offset + summary.total);
        offset += summary.total;
        const baseline = await tester.loadBaseline(category);
        const classified = baseline?.size > 0 ? tester.classifyBaselineResults(baseline, rows) : null;
        const last = rows.reduce((end, row) => Math.max(end, Date.parse(row.timestamp) || start), start);
        records.push({ name: category, passed: classified ? classified.successful : summary.successful,
            partial: classified ? classified.partial : 0,
            failed: classified ? classified.failures.length : summary.failed,
            skipped: summary.skipped,
            // category wall time includes planning and waiting for the shared process pool.
            duration_ms: last - start });
    }
    if (offset !== results.length) throw new Error('Layout category inventory does not match results');
    return records;
}

async function main() {
    const args = process.argv.slice(2);
    const value = flag => args[args.indexOf(flag) + 1];
    const categories = value('--suites')?.split(/[\s,]+/).filter(Boolean);
    const jobs = Number(value('--jobs'));
    const resultFile = value('--results');
    if (!args.includes('--suites') || !categories?.length || new Set(categories).size !== categories.length ||
        !args.includes('--jobs') || !Number.isInteger(jobs) || jobs < 1 || !args.includes('--results') || !resultFile)
        throw new Error('Expected --suites <categories> --jobs <count> --results <path>');
    fs.mkdirSync(path.dirname(resultFile), { recursive: true });
    fs.writeFileSync(resultFile, '');
    const { RadiantLayoutTester } = require('./layout/test_radiant_layout.js');
    const tester = new RadiantLayoutTester({ engine: 'radiant', tolerance: 5.5,
        baselineOnly: true, batchSize: 100, batchSizeExplicit: true,
        maxConcurrency: jobs, maxConcurrencyExplicit: true, globalConcurrency: jobs,
        json: true, emitJson: false });
    try {
        const records = await runCategories(tester, categories);
        fs.writeFileSync(resultFile, records.map(r => [r.name, r.failed ? '❌ FAIL' : '✅ PASS',
            r.passed, r.partial, r.failed, r.skipped, Math.ceil(r.duration_ms / 1000)].join('|')).join('\n') + '\n');
        for (const r of records)
            console.log(`Layout ${r.name}: ${r.passed} passed, ${r.partial} partial, ${r.failed} failed, ${r.skipped} skipped`);
        if (records.some(r => r.failed > 0)) process.exitCode = 1;
    } finally {
        tester.terminateLayoutProcesses();
    }
}

if (require.main === module) main().catch(error => { console.error(error); process.exitCode = 1; });
module.exports = { runCategories, classifyInputEvents };
