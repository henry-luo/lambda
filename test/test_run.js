#!/usr/bin/env node
'use strict';

// Lambda Test Suite Runner - Node.js rewrite
// Progress-based idle timeout: kills tests only when they stop producing output.
// Auto-scales parallelism to CPU count.

const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');

// ─── CLI argument parsing ──────────────────────────────────────────────────────

const args = process.argv.slice(2);
let targetSuite = '';
let excludeSuite = '';
let targetCategory = '';
let rawOutput = false;
let parallelExecution = true;

for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg.startsWith('--target='))        targetSuite = arg.split('=')[1];
    else if (arg === '--target')            targetSuite = args[++i];
    else if (arg.startsWith('--exclude-target=')) excludeSuite = arg.split('=')[1];
    else if (arg === '--exclude-target')    excludeSuite = args[++i];
    else if (arg.startsWith('--category=')) targetCategory = arg.split('=')[1];
    else if (arg === '--category')          targetCategory = args[++i];
    else if (arg === '--raw')              rawOutput = true;
    else if (arg === '--sequential')       parallelExecution = false;
    else if (arg === '--parallel')         parallelExecution = true;
    else if (arg === '-h' || arg === '--help') {
        console.log(`Usage: node test/test_run.js [OPTIONS]
  --target=SUITE       Run only tests from specified suite (library, input, mir, lambda, validator, radiant, jube)
  --exclude-target=S   Exclude tests from specified suite (e.g. jube)
  --category=CAT       Run only tests from specified category (baseline, extended)
  --raw                Show raw test output without formatting
  --sequential         Run tests sequentially (default: parallel)
  --parallel           Run tests in parallel (default)
  --help               Show this help message

Environment variables:
  LAMBDA_TEST_IDLE_TIMEOUT   Override idle timeout in seconds (default: auto-scaled by CPU count)
  LAMBDA_USE_C2MIR           Set to 1 for legacy C2MIR JIT path`);
        process.exit(0);
    } else {
        console.error(`Unknown option: ${arg}\nUse --help for usage information`);
        process.exit(1);
    }
}

// ─── Configuration ─────────────────────────────────────────────────────────────

const ROOT_DIR   = path.resolve(__dirname, '..');
const CONFIG_FILE = path.join(ROOT_DIR, 'build_lambda_config.json');
const TEST_OUTPUT_DIR = path.join(ROOT_DIR, 'test_output');
const IS_WINDOWS = process.platform === 'win32';

// Idle timeout: if a test produces no output for this long, it's stuck.
// Scale by CPU count — slower machines get more headroom.
function getIdleTimeoutMs() {
    const envOverride = process.env.LAMBDA_TEST_IDLE_TIMEOUT;
    if (envOverride) return parseInt(envOverride, 10) * 1000;

    const cpus = os.cpus().length;
    if (cpus >= 8)  return 120 * 1000;  // 2 min
    if (cpus >= 4)  return 180 * 1000;  // 3 min
    return 240 * 1000;                  // 4 min
}

const IDLE_TIMEOUT_MS = getIdleTimeoutMs();

// Max concurrent tests: scale to CPU count, leave 1 core free (min 1).
function getMaxConcurrent() {
    if (!parallelExecution) return 1;
    return Math.max(1, os.cpus().length - 1);
}

const MAX_CONCURRENT = getMaxConcurrent();

// ─── Helpers ────────────────────────────────────────────────────────────────────

function loadConfig() {
    try {
        return JSON.parse(fs.readFileSync(CONFIG_FILE, 'utf8'));
    } catch (e) {
        console.error(`Error reading ${CONFIG_FILE}: ${e.message}`);
        process.exit(1);
    }
}

function ensureDir(dir) {
    fs.mkdirSync(dir, { recursive: true });
}

/** Get test suite category for a given executable base name */
function getTestSuiteCategory(config, baseName) {
    for (const suite of config.test.test_suites) {
        if (suite.disabled) continue;
        for (const t of (suite.tests || [])) {
            if (t.disabled) continue;
            const src = t.source || '';
            // Match base name from source filename (strip path prefix and extension)
            const srcBase = path.basename(src).replace(/\.(c|cpp)$/, '');
            if (srcBase === baseName) return suite.suite;
        }
    }
    if (baseName === 'lambda_test_runner') return 'lambda-std';
    return 'unknown';
}

/** Get test display name */
function getTestDisplayName(config, baseName) {
    for (const suite of config.test.test_suites) {
        if (suite.disabled) continue;
        for (const t of (suite.tests || [])) {
            if (t.disabled) continue;
            const srcBase = path.basename(t.source || '').replace(/\.(c|cpp)$/, '');
            if (srcBase === baseName) return t.name || baseName;
        }
    }
    return baseName;
}

/** Get test icon */
function getTestIcon(config, baseName) {
    for (const suite of config.test.test_suites) {
        if (suite.disabled) continue;
        for (const t of (suite.tests || [])) {
            if (t.disabled) continue;
            const srcBase = path.basename(t.source || '').replace(/\.(c|cpp)$/, '');
            if (srcBase === baseName) return t.icon || '🧪';
        }
    }
    return '🧪';
}

/** Get suite display name */
function getSuiteDisplayName(config, suiteKey) {
    for (const suite of config.test.test_suites) {
        if (suite.disabled) continue;
        if (suite.suite === suiteKey) return suite.name;
    }
    const fallback = {
        'library': '📚 Library Tests',
        'input': '📄 Input Processing Tests',
        'mir': '⚡ MIR JIT Tests',
        'lambda': '🐑 Lambda Runtime Tests',
        'lambda-std': '🧪 Lambda Standard Tests',
        'validator': '🔍 Validator Tests',
        'radiant': '🎨 Radiant Layout Engine Tests',
        'unknown': '🧪 Other Tests',
    };
    return fallback[suiteKey] || `🧪 ${suiteKey} Tests`;
}

/** Get test category from config */
function getTestCategory(config, baseName) {
    for (const suite of config.test.test_suites) {
        if (suite.disabled) continue;
        for (const t of (suite.tests || [])) {
            if (t.disabled) continue;
            const binaryBase = (t.binary || '').replace(/\.exe$/, '');
            if (binaryBase === baseName) {
                return t.category || suite.category || 'baseline';
            }
        }
    }
    return 'baseline';
}

/** Check if a test executable is GTest-based (from config libraries) */
function isGtestTest(baseName, config) {
    if (baseName.endsWith('_gtest')) return true;
    for (const suite of config.test.test_suites) {
        if (suite.disabled) continue;
        for (const t of (suite.tests || [])) {
            if (t.disabled) continue;
            const srcBase = path.basename(t.source || '').replace(/\.(c|cpp)$/, '');
            if (srcBase === baseName) {
                const libs = t.libraries || [];
                return libs.includes('gtest') || libs.includes('gtest_main');
            }
        }
    }
    return false;
}

// ─── Test discovery ─────────────────────────────────────────────────────────────

function discoverTests(config) {
    const tests = [];

    for (const suite of config.test.test_suites) {
        if (suite.disabled) continue;
        for (const t of (suite.tests || [])) {
            if (t.disabled) continue;
            const src = t.source || '';
            let baseName = path.basename(src).replace(/\.(c|cpp)$/, '');

            // Prefer GTest version for known Criterion tests
            const preferGtest = [
                'test_lambda', 'test_lambda_repl', 'test_lambda_proc',
                'test_mime_detect', 'test_css_files_safe', 'test_math',
                'test_math_ascii', 'test_markup_roundtrip', 'test_dir',
                'test_input_roundtrip',
            ];
            if (preferGtest.includes(baseName)) {
                const gtestExe = path.join(ROOT_DIR, 'test', `${baseName}_gtest.exe`);
                if (fs.existsSync(gtestExe)) {
                    baseName = baseName + '_gtest';
                }
            }

            // Skip Catch2 tests
            if (baseName.includes('catch2')) continue;

            // On Windows, only run gtest tests
            if (IS_WINDOWS && !baseName.includes('gtest')) continue;

            const exePath = path.join(ROOT_DIR, 'test', `${baseName}.exe`);
            const srcPath = path.join(ROOT_DIR, src.startsWith('test/') ? src : `test/${src}`);

            if (fs.existsSync(exePath) || fs.existsSync(srcPath)) {
                const libs = t.libraries || [];
                tests.push({
                    baseName,
                    exePath,
                    suite: suite.suite,
                    category: t.category || suite.category || 'baseline',
                    displayName: t.name || baseName,
                    icon: t.icon || '🧪',
                    isGtest: baseName.endsWith('_gtest') || libs.includes('gtest') || libs.includes('gtest_main'),
                });
            }
        }
    }

    // Deduplicate by baseName
    const seen = new Set();
    return tests.filter(t => {
        if (seen.has(t.baseName)) return false;
        seen.add(t.baseName);
        return true;
    });
}

function filterTests(tests) {
    let result = tests;
    if (targetSuite)    result = result.filter(t => t.suite === targetSuite);
    if (excludeSuite)   result = result.filter(t => t.suite !== excludeSuite);
    if (targetCategory) result = result.filter(t => t.category === targetCategory);
    return result;
}

// ─── Run a single test with idle-timeout ────────────────────────────────────────

/**
 * Spawns a test executable and monitors its stdout/stderr.
 * Resets an idle timer on every line of output.
 * Kills the process if idle for IDLE_TIMEOUT_MS.
 *
 * Returns: { passed, failed, total, status, failedTests[], timedOut }
 */
function runTest(testInfo) {
    return new Promise((resolve) => {
        const { baseName, exePath } = testInfo;

        if (!fs.existsSync(exePath)) {
            resolve({
                passed: 0, failed: 1, total: 1,
                status: '❌ ERROR', failedTests: [`[${baseName}] executable not found`],
                timedOut: false,
            });
            return;
        }

        const jsonFile = path.join(TEST_OUTPUT_DIR, `${baseName}_results.json`);

        // Build argv based on test type
        let testArgs = [];
        let env = { ...process.env };

        // Suppress ASan container-overflow false positives
        const existingAsan = env.ASAN_OPTIONS || '';
        env.ASAN_OPTIONS = existingAsan ? `${existingAsan}:detect_container_overflow=0` : 'detect_container_overflow=0';

        if (baseName === 'lambda_test_runner') {
            const tapFile = path.join(TEST_OUTPUT_DIR, `${baseName}_results.tap`);
            testArgs = ['--test-dir', 'test/std', '--format', 'both',
                        '--json-output', jsonFile, '--tap-output', tapFile];
            if (rawOutput) testArgs.push('--verbose');
        } else if (testInfo.isGtest) {
            const jsonPath = IS_WINDOWS ? jsonFile.replace(/\//g, '\\') : jsonFile;
            testArgs = [`--gtest_output=json:${jsonPath}`];
            // Special filter for input roundtrip
            if (baseName === 'test_input_roundtrip_gtest') {
                testArgs.unshift('--gtest_filter=JsonTests.*');
            }
        } else if (baseName === 'test_flex_standalone') {
            // No special args — output parsed manually
        } else {
            // Criterion-based: --json=FILE
            testArgs = [`--json=${jsonFile}`];
        }

        const child = spawn(`./${path.relative(ROOT_DIR, exePath)}`, testArgs, {
            cwd: ROOT_DIR,
            env,
            stdio: ['ignore', 'pipe', 'pipe'],
        });

        let outputLines = [];
        let idleTimer = null;
        let timedOut = false;
        let finished = false;

        function resetIdleTimer() {
            if (idleTimer) clearTimeout(idleTimer);
            if (finished) return;
            idleTimer = setTimeout(() => {
                if (finished) return;
                timedOut = true;
                console.error(`   ⏰ ${baseName} idle for ${IDLE_TIMEOUT_MS / 1000}s — killing (stuck?)`)
                child.kill('SIGKILL');
            }, IDLE_TIMEOUT_MS);
        }

        // Start the idle timer immediately
        resetIdleTimer();

        child.stdout.on('data', (data) => {
            resetIdleTimer();
            const lines = data.toString().split('\n');
            outputLines.push(...lines);
            if (rawOutput) process.stdout.write(data);
        });

        child.stderr.on('data', (data) => {
            resetIdleTimer();
            if (rawOutput) process.stderr.write(data);
        });

        child.on('error', (err) => {
            finished = true;
            if (idleTimer) clearTimeout(idleTimer);
            resolve({
                passed: 0, failed: 1, total: 1,
                status: '❌ ERROR', failedTests: [`[${baseName}] spawn error: ${err.message}`],
                timedOut: false,
            });
        });

        child.on('close', (code) => {
            finished = true;
            if (idleTimer) clearTimeout(idleTimer);

            if (timedOut) {
                // Write a timeout marker JSON so results aggregation works
                const timeoutJson = {
                    tests: 1, failures: 1, passed: 0,
                    testsuites: [{ name: 'timeout', tests: 1, failures: 1 }],
                };
                try { fs.writeFileSync(jsonFile, JSON.stringify(timeoutJson)); } catch (_) {}
            }

            // For standalone flex test, parse output manually
            if (baseName === 'test_flex_standalone') {
                const allOutput = outputLines.join('\n');
                const passCount = (allOutput.match(/PASS:/g) || []).length;
                const failCount = (allOutput.match(/FAIL:/g) || []).length;
                const total = passCount + failCount;
                const simpleJson = { tests: total, failures: failCount, passed: passCount };
                try { fs.writeFileSync(jsonFile, JSON.stringify(simpleJson)); } catch (_) {}
            }

            // Parse result JSON
            const result = parseTestResults(baseName, jsonFile, timedOut);
            resolve(result);
        });
    });
}

// ─── Result parsing ─────────────────────────────────────────────────────────────

function parseTestResults(baseName, jsonFile, timedOut) {
    if (timedOut) {
        return {
            passed: 0, failed: 1, total: 1,
            status: '⏰ TIMEOUT',
            failedTests: [`[${baseName}] timed out (no output for ${IDLE_TIMEOUT_MS / 1000}s)`],
            timedOut: true,
        };
    }

    let data;
    // Wait briefly for JSON file to appear (GTest writes after process exit)
    for (let attempt = 0; attempt < 50; attempt++) {
        try {
            if (fs.existsSync(jsonFile)) {
                data = JSON.parse(fs.readFileSync(jsonFile, 'utf8'));
                break;
            }
        } catch (_) {}
        // Synchronous small wait — only during result collection, not during test execution
        if (attempt < 49) {
            const start = Date.now();
            while (Date.now() - start < 100) { /* spin */ }
        }
    }

    if (!data) {
        return {
            passed: 0, failed: 1, total: 1,
            status: '❌ ERROR',
            failedTests: [`[${baseName}] no valid JSON output`],
            timedOut: false,
        };
    }

    let passed = 0, failed = 0, total = 0;
    let failedTests = [];

    if (baseName === 'lambda_test_runner') {
        // Custom Lambda runner format
        passed = (data.summary && data.summary.passed) || 0;
        failed = (data.summary && data.summary.failed) || 0;
        total = passed + failed;
        if (Array.isArray(data.tests)) {
            failedTests = data.tests
                .filter(t => t.passed === false)
                .map(t => `[${baseName}] ${t.name}`);
        }
    } else if (data['test-run']) {
        // Catch2 format
        const totals = data['test-run'].totals?.['test-cases'] || {};
        passed = totals.passed || 0;
        failed = totals.failed || 0;
        total = passed + failed;
    } else if (typeof data.tests === 'number') {
        // GTest format: { tests: N, failures: M, disabled: D, testsuites: [...] }
        const disabled = data.disabled || 0;
        total = data.tests - disabled;
        failed = data.failures || 0;
        passed = total - failed;
        // Extract failed test names from testsuites
        if (Array.isArray(data.testsuites)) {
            for (const suite of data.testsuites) {
                for (const tc of (suite.testsuite || [])) {
                    if (tc.failures && tc.failures > 0) {
                        failedTests.push(`[${baseName}] ${suite.name}.${tc.name}`);
                    }
                }
            }
        }
    } else if (typeof data.passed === 'number') {
        // Criterion or simple format: { passed: N, failed: M }
        passed = data.passed || 0;
        failed = data.failed || 0;
        total = passed + failed;
        // Extract failed test names if available
        if (Array.isArray(data.test_suites)) {
            for (const suite of data.test_suites) {
                for (const tc of (suite.tests || [])) {
                    if (tc.status === 'FAILED') {
                        failedTests.push(`[${baseName}] ${tc.name}`);
                    }
                }
            }
        }
    } else {
        return {
            passed: 0, failed: 1, total: 1,
            status: '⚠️ NO OUTPUT',
            failedTests: [`[${baseName}] unrecognized JSON format`],
            timedOut: false,
        };
    }

    return {
        passed, failed, total,
        status: failed === 0 ? '✅ PASS' : '❌ FAIL',
        failedTests,
        timedOut: false,
    };
}

// ─── Parallel execution with concurrency limit ─────────────────────────────────

async function runAllTests(tests) {
    const results = new Map(); // baseName -> result + testInfo
    let nextIndex = 0;
    const running = new Set();

    return new Promise((resolveAll) => {
        function startNext() {
            while (running.size < MAX_CONCURRENT && nextIndex < tests.length) {
                const idx = nextIndex++;
                const testInfo = tests[idx];
                const label = `[${idx + 1}/${tests.length}]`;

                if (!rawOutput) {
                    console.log(`   ${label} Starting ${testInfo.baseName}...`);
                }

                const promise = runTest(testInfo).then((result) => {
                    running.delete(promise);
                    results.set(testInfo.baseName, { ...result, testInfo });

                    if (!rawOutput) {
                        const icon = result.status.includes('PASS') ? '✓' : '✗';
                        console.log(`   ${icon} Completed ${testInfo.baseName} (${result.passed}/${result.total} passed)`);
                    }

                    startNext();

                    if (running.size === 0 && nextIndex >= tests.length) {
                        resolveAll(results);
                    }
                });

                running.add(promise);
            }

            // Edge case: no tests to run
            if (tests.length === 0) resolveAll(results);
        }

        startNext();
    });
}

// ─── Display results ────────────────────────────────────────────────────────────

function displayResults(config, results) {
    // Aggregate by suite
    const suiteMap = new Map(); // suite -> { name, tests: [{ baseName, displayName, icon, result }] }

    for (const [baseName, entry] of results) {
        const info = entry.testInfo;
        const suite = info.suite;
        if (!suiteMap.has(suite)) {
            suiteMap.set(suite, {
                displayName: getSuiteDisplayName(config, suite),
                tests: [],
            });
        }
        suiteMap.get(suite).tests.push({
            baseName,
            displayName: info.displayName,
            icon: info.icon,
            result: entry,
        });
    }

    // Ordered suite keys
    const orderedSuites = ['library', 'input', 'mir', 'lambda', 'lambda-std', 'validator', 'radiant'];
    const allSuites = [...new Set([...orderedSuites, ...suiteMap.keys()])].filter(s => suiteMap.has(s));

    let totalTests = 0, totalPassed = 0, totalFailed = 0;
    const allFailedTests = [];

    console.log('');
    console.log('==============================================================');
    console.log('🏁 TEST RESULTS BREAKDOWN');
    console.log('==============================================================');
    console.log('📊 Test Results:');

    for (const suiteKey of allSuites) {
        const suite = suiteMap.get(suiteKey);
        let suitePassed = 0, suiteFailed = 0, suiteTotal = 0;

        for (const t of suite.tests) {
            suitePassed += t.result.passed;
            suiteFailed += t.result.failed;
            suiteTotal  += t.result.total;
            allFailedTests.push(...t.result.failedTests);
        }

        totalTests  += suiteTotal;
        totalPassed += suitePassed;
        totalFailed += suiteFailed;

        const suiteStatus = suiteFailed === 0 ? '✅ PASS' : '❌ FAIL';
        console.log(`   ${suite.displayName} ${suiteStatus} (${suitePassed}/${suiteTotal} tests)`);

        for (const t of suite.tests) {
            console.log(`     └─ ${t.icon} ${t.result.status} (${t.result.passed}/${t.result.total} tests) ${t.displayName} (${t.baseName}.exe)`);
        }
    }

    console.log('');
    console.log('📊 Overall Results:');
    console.log(`   Total Tests: ${totalTests}`);
    console.log(`   ✅ Passed:   ${totalPassed}`);
    if (totalFailed > 0) {
        console.log(`   ❌ Failed:   ${totalFailed}`);
    }

    if (allFailedTests.length > 0) {
        console.log('');
        console.log('🔍 Failed Tests:');
        for (const name of allFailedTests) {
            console.log(`   ❌ ${name}`);
        }
    }

    console.log('==============================================================');

    // Save summary JSON
    const summary = {
        total_tests: totalTests,
        total_passed: totalPassed,
        total_failed: totalFailed,
        failed_test_names: allFailedTests,
        level1_test_suites: allSuites.map(key => {
            const s = suiteMap.get(key);
            let sp = 0, sf = 0, st = 0;
            for (const t of s.tests) { sp += t.result.passed; sf += t.result.failed; st += t.result.total; }
            return { name: s.displayName, total: st, passed: sp, failed: sf, status: sf === 0 ? '✅ PASS' : '❌ FAIL' };
        }),
        level2_c_tests: [...results.entries()].map(([bn, e]) => ({
            name: e.testInfo.displayName, suite: e.testInfo.suite,
            total: e.total, passed: e.passed, failed: e.failed, status: e.status,
        })),
    };

    const summaryFile = path.join(TEST_OUTPUT_DIR, 'test_summary.json');
    fs.writeFileSync(summaryFile, JSON.stringify(summary, null, 4));

    console.log('');
    console.log(`📁 Results saved to: ${TEST_OUTPUT_DIR}`);
    console.log('   - Individual JSON results: *_results.json');
    console.log('   - Two-level summary: test_summary.json');

    return { totalFailed };
}

// ─── Main ───────────────────────────────────────────────────────────────────────

async function main() {
    console.log('==============================================================');
    console.log('🧪 Lambda Test Suite Runner (Node.js)');
    console.log('==============================================================');
    console.log(`Platform: ${os.platform()} | CPUs: ${os.cpus().length} | Idle timeout: ${IDLE_TIMEOUT_MS / 1000}s`);
    if (parallelExecution) {
        console.log(`⚡ Parallel Execution: max ${MAX_CONCURRENT} concurrent`);
    } else {
        console.log('🔄 Sequential Execution: running test suites one by one');
    }
    console.log('==============================================================');

    const config = loadConfig();
    ensureDir(TEST_OUTPUT_DIR);

    // Discover and filter tests
    let tests = discoverTests(config);
    tests = filterTests(tests);

    if (tests.length === 0) {
        console.error('❌ No test executables found matching filters');
        process.exit(1);
    }

    console.log(`📋 Found ${tests.length} test executable(s)`);
    console.log('');

    // Run tests
    const results = await runAllTests(tests);

    // Display and save results
    if (!rawOutput) {
        const { totalFailed } = displayResults(config, results);
    } else {
        console.log('');
        console.log(`📁 Raw output mode - results saved to: ${TEST_OUTPUT_DIR}`);
    }
}

main().catch((err) => {
    console.error('Fatal error:', err);
    process.exit(1);
});
