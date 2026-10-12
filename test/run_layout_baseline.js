#!/usr/bin/env node
'use strict';

// keep the layout harness's category isolation and classification in one shared pool.
const fs = require('fs');
const path = require('path');

async function runCategories(tester, categories) {
    const start = Date.now();
    const results = await tester.testCategories(categories);
    let offset = 0;
    const records = [];
    for (const category of categories) {
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
module.exports = { runCategories };
