'use strict';

// Some GTest versions report skips only on individual test cases.
function gtestResultCounts(data) {
    const total = data.tests - (data.disabled || 0);
    const skipped = typeof data.skipped === 'number' ? data.skipped :
        (data.testsuites || []).reduce((count, suite) => count +
            (suite.testsuite || []).filter(test => test.status === 'RUN' && test.result === 'SKIPPED' &&
                !(Array.isArray(test.failures) ? test.failures.length : test.failures)).length, 0);
    const failed = data.failures || 0;
    return { total, skipped, failed, passed: total - failed - skipped };
}

module.exports = { gtestResultCounts };
