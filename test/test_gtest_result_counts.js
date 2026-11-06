'use strict';
const assert = require('assert');
const { gtestResultCounts } = require('./gtest_result_counts.js');

// GTest 1.17 omits the root skip count but records the skipped case.
assert.deepStrictEqual(gtestResultCounts({ tests: 4, disabled: 1, failures: 1, testsuites: [
    { testsuite: [
        { status: 'RUN', result: 'COMPLETED' },
        { status: 'RUN', result: 'SKIPPED' },
        { status: 'RUN', result: 'COMPLETED', failures: [{ failure: 'bad' }] },
        { status: 'NOTRUN', result: 'SUPPRESSED' },
    ] },
] }), { total: 3, skipped: 1, failed: 1, passed: 1 });
// The merged parallel report provides an authoritative root count.
assert.deepStrictEqual(gtestResultCounts({ tests: 3, disabled: 0, failures: 0, skipped: 1,
    testsuites: [{ testsuite: [{ status: 'RUN', result: 'SKIPPED' }] }] }),
    { total: 3, skipped: 1, failed: 0, passed: 2 });
assert.deepStrictEqual(gtestResultCounts({ tests: 1, failures: 1, testsuites: [
    { testsuite: [{ status: 'RUN', result: 'SKIPPED', failures: [{ failure: 'bad' }] }] },
] }), { total: 1, skipped: 0, failed: 1, passed: 0 });
console.log('GTest result counts: passed');
