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
let excludeTests = new Set();
let rawOutput = false;
let parallelExecution = true;
let inputResultsFile = '';

function addExcludedTests(value) {
    if (!value) return;
    for (const name of value.split(',')) {
        const trimmed = name.trim();
        if (trimmed) {
            excludeTests.add(trimmed.replace(/\.exe$/, ''));
        }
    }
}

for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg.startsWith('--target='))        targetSuite = arg.split('=')[1];
    else if (arg === '--target')            targetSuite = args[++i];
    else if (arg.startsWith('--exclude-target=')) excludeSuite = arg.split('=')[1];
    else if (arg === '--exclude-target')    excludeSuite = args[++i];
    else if (arg.startsWith('--exclude-test=')) addExcludedTests(arg.substring('--exclude-test='.length));
    else if (arg === '--exclude-test')       addExcludedTests(args[++i]);
    else if (arg.startsWith('--category=')) targetCategory = arg.split('=')[1];
    else if (arg === '--category')          targetCategory = args[++i];
    else if (arg.startsWith('--input-results=')) inputResultsFile = arg.split('=').slice(1).join('=');
    else if (arg === '--input-results')     inputResultsFile = args[++i];
    else if (arg === '--raw')              rawOutput = true;
    else if (arg === '--sequential')       parallelExecution = false;
    else if (arg === '--parallel')         parallelExecution = true;
    else if (arg === '-h' || arg === '--help') {
        console.log(`Usage: node test/test_run.js [OPTIONS]
  --target=SUITE       Run only tests from specified suite (library, input, mir, lambda, validator, radiant, jube, extended)
  --exclude-target=S   Exclude tests from specified suite (e.g. jube)
  --exclude-test=NAME  Exclude test executable base name (repeatable or comma-separated)
  --category=CAT       Run only tests from specified category (baseline, extended)
  --raw                Show raw test output without formatting
  --sequential         Run tests sequentially (default: parallel)
  --parallel           Run tests in parallel (default)
  --help               Show this help message

Environment variables:
  LAMBDA_TEST_IDLE_TIMEOUT   Override idle timeout in seconds (default: auto-scaled by CPU count/load)
  LAMBDA_TEST_HEAVY_LOAD     Set to 1 to bias idle timeout upward for full-suite parallel runs
  LAMBDA_UI_TEST_JOBS        Override UI automation parallelism in suite runs
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

const SCRIPT_TESTS = [
    {
        baseName: 'run_lambda_mathlive_markup',
        script: 'test/lambda/mathlive/run_lambda_mathlive_markup.mjs',
        suite: 'extended',
        category: 'extended',
        displayName: 'Lambda MathLive Markup Baseline',
        icon: '🔢',
        runner: 'node',
        args: ['--strict'],
        exclusive: true,
    },
];

// Idle timeout: if a test produces no output for this long, it's stuck.
// Scale by CPU count first, then add extra headroom for suite-wide parallel
// runs and already-busy machines so quiet but still-progressing tests do not
// get killed under contention.
function getIdleTimeoutConfig() {
    const envOverride = process.env.LAMBDA_TEST_IDLE_TIMEOUT;
    if (envOverride) {
        return {
            timeoutMs: parseInt(envOverride, 10) * 1000,
            source: `env override (${envOverride}s)`,
        };
    }

    const cpus = os.cpus().length;
    let baseMs = 240 * 1000;            // 4 min
    if (cpus >= 8) baseMs = 120 * 1000; // 2 min
    else if (cpus >= 4) baseMs = 180 * 1000; // 3 min

    const load1 = os.loadavg ? os.loadavg()[0] : 0;
    const loadRatio = cpus > 0 ? load1 / cpus : 0;
    const heavyLoadRequested = process.env.LAMBDA_TEST_HEAVY_LOAD === '1';

    let multiplier = 1.0;
    let reason = `cpu-scaled base (${cpus} CPUs)`;

    if (heavyLoadRequested) {
        multiplier = Math.max(multiplier, 1.5);
        reason = 'heavy-load suite mode';
    }
    if (loadRatio >= 1.5) {
        multiplier = Math.max(multiplier, 3.0);
        reason = `system load ${load1.toFixed(2)} on ${cpus} CPUs`;
    } else if (loadRatio >= 1.0) {
        multiplier = Math.max(multiplier, 2.0);
        reason = `system load ${load1.toFixed(2)} on ${cpus} CPUs`;
    } else if (loadRatio >= 0.75) {
        multiplier = Math.max(multiplier, 1.5);
        reason = `system load ${load1.toFixed(2)} on ${cpus} CPUs`;
    }

    const timeoutMs = Math.round(baseMs * multiplier);
    return {
        timeoutMs,
        source: multiplier > 1.0 ? `${reason}, x${multiplier.toFixed(1)}` : reason,
    };
}

const IDLE_TIMEOUT = getIdleTimeoutConfig();
const IDLE_TIMEOUT_MS = IDLE_TIMEOUT.timeoutMs;

// Max concurrent tests: scale to CPU count, leave 1 core free (min 1).
function getMaxConcurrent() {
    if (!parallelExecution) return 1;
    return Math.max(1, os.cpus().length - 1);
}

const MAX_CONCURRENT = getMaxConcurrent();
const heartbeatSeconds = parseInt(process.env.LAMBDA_TEST_HEARTBEAT || '30', 10);
const HEARTBEAT_MS = (Number.isFinite(heartbeatSeconds) ? Math.max(10, heartbeatSeconds) : 30) * 1000;

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
        'radiant': '🎨 Radiant Tests',
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
                    // Some GTest binaries launch their own worker processes; respect
                    // config-level serialization so nested async subprocesses are not
                    // starved by the outer suite scheduler.
                    exclusive: t.parallel === false || suite.parallel === false || t.exclusive === true,
                });
            }
        }
    }

    for (const scriptTest of SCRIPT_TESTS) {
        const scriptPath = path.join(ROOT_DIR, scriptTest.script);
        if (fs.existsSync(scriptPath)) {
            tests.push({
                ...scriptTest,
                scriptPath,
                exePath: scriptPath,
                isGtest: false,
            });
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
    if (excludeTests.size) result = result.filter(t => !excludeTests.has(t.baseName));
    if (targetCategory) result = result.filter(t => t.category === targetCategory);
    return result;
}

function parseDurationMs(value) {
    if (typeof value === 'number') return Math.max(0, value * 1000);
    if (typeof value !== 'string') return 0;

    const trimmed = value.trim();
    if (trimmed.endsWith('ms')) return Math.max(0, parseFloat(trimmed));
    if (trimmed.endsWith('s')) return Math.max(0, parseFloat(trimmed) * 1000);
    const numeric = parseFloat(trimmed);
    return Number.isFinite(numeric) ? Math.max(0, numeric * 1000) : 0;
}

function getPreviousDurationMs(baseName) {
    const jsonFile = path.join(TEST_OUTPUT_DIR, `${baseName}_results.json`);
    try {
        if (!fs.existsSync(jsonFile)) return 0;
        const data = JSON.parse(fs.readFileSync(jsonFile, 'utf8'));
        return parseDurationMs(data.time);
    } catch (_) {
        return 0;
    }
}

function formatDuration(ms) {
    const totalSeconds = Math.max(0, Math.round(ms / 1000));
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return minutes > 0 ? `${minutes}m${seconds.toString().padStart(2, '0')}s` : `${seconds}s`;
}

function getTestProperty(tc, key) {
    if (tc && Object.prototype.hasOwnProperty.call(tc, key)) return tc[key];
    if (!Array.isArray(tc.properties)) return null;
    const prop = tc.properties.find(p => p && p.key === key);
    return prop ? prop.value : null;
}

function getCapturedCaseDurationMs(tc) {
    const elapsedUs = Number(getTestProperty(tc, 'lambda_script_elapsed_us'));
    if (Number.isFinite(elapsedUs) && elapsedUs > 0) return elapsedUs / 1000;
    const elapsedMs = Number(getTestProperty(tc, 'lambda_script_elapsed_ms'));
    if (Number.isFinite(elapsedMs) && elapsedMs > 0) return elapsedMs;
    return 0;
}

function formatSecondsForGtest(ms) {
    return `${(ms / 1000).toFixed(3)}s`;
}

function normalizeGtestCaseTimes(jsonFile) {
    try {
        if (!fs.existsSync(jsonFile)) return;
        const data = JSON.parse(fs.readFileSync(jsonFile, 'utf8'));
        let changed = false;

        for (const suite of (data.testsuites || [])) {
            let suiteCaseMs = 0;
            let suiteHasCaptured = false;
            for (const tc of (suite.testsuite || [])) {
                const capturedMs = getCapturedCaseDurationMs(tc);
                if (capturedMs > 0) {
                    tc.time = formatSecondsForGtest(capturedMs);
                    suiteCaseMs += capturedMs;
                    suiteHasCaptured = true;
                    changed = true;
                } else {
                    suiteCaseMs += parseDurationMs(tc.time);
                }
            }
            if (suiteHasCaptured) {
                suite.time = formatSecondsForGtest(suiteCaseMs);
                suite.lambda_captured_case_time_ms = Math.round(suiteCaseMs);
            }
        }

        if (changed) {
            fs.writeFileSync(jsonFile, JSON.stringify(data, null, 2));
        }
    } catch (_) {
        // Keep the original gtest JSON if normalization cannot parse it.
    }
}

function testArtifactLabel(testInfo) {
    if (testInfo.runner === 'node') return testInfo.script;
    return `${testInfo.baseName}.exe`;
}

function appendArgValue(args, name, value) {
    if (args.some((arg, index) => arg === name || arg.startsWith(`${name}=`) || args[index - 1] === name)) {
        return args;
    }
    return [...args, name, value];
}

function scheduleTests(tests) {
    if (!parallelExecution) return tests;

    return tests
        .map((test, index) => ({
            test,
            index,
            previousDurationMs: getPreviousDurationMs(test.baseName),
        }))
        .sort((a, b) => {
            if (b.previousDurationMs !== a.previousDurationMs) {
                return b.previousDurationMs - a.previousDurationMs;
            }
            return a.index - b.index;
        })
        .map(entry => entry.test);
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
        const { baseName, exePath, scriptPath } = testInfo;
        const testPath = testInfo.runner === 'node' ? scriptPath : exePath;

        if (!testPath || !fs.existsSync(testPath)) {
            resolve({
                passed: 0, failed: 1, total: 1,
                status: '❌ ERROR', failedTests: [`[${baseName}] ${testArtifactLabel(testInfo)} not found`],
                timedOut: false,
            });
            return;
        }

        const jsonFile = path.join(TEST_OUTPUT_DIR, `${baseName}_results.json`);
        try { fs.unlinkSync(jsonFile); } catch (_) {}

        // Build argv based on test type
        let testArgs = [];
        let env = { ...process.env };

        // Suppress ASan container-overflow false positives
        const existingAsan = env.ASAN_OPTIONS || '';
        env.ASAN_OPTIONS = existingAsan ? `${existingAsan}:detect_container_overflow=0` : 'detect_container_overflow=0';

        let command = `./${path.relative(ROOT_DIR, exePath)}`;
        let spawnArgs = testArgs;

        if (testInfo.runner === 'node') {
            command = 'node';
            let scriptArgs = testInfo.args || [];
            if (baseName === 'run_lambda_mathlive_markup' && targetCategory === 'baseline') {
                scriptArgs = appendArgValue(scriptArgs, '--fixture-source', 'mathlive');
            }
            testArgs = appendArgValue(scriptArgs, '--report', jsonFile);
            spawnArgs = [scriptPath, ...testArgs];
        } else if (baseName === 'lambda_test_runner') {
            const tapFile = path.join(TEST_OUTPUT_DIR, `${baseName}_results.tap`);
            try { fs.unlinkSync(tapFile); } catch (_) {}
            testArgs = ['--test-dir', 'test/std', '--format', 'both',
                        '--json-output', jsonFile, '--tap-output', tapFile];
            if (rawOutput) testArgs.push('--verbose');
            spawnArgs = testArgs;
        } else if (testInfo.isGtest) {
            const jsonPath = IS_WINDOWS ? jsonFile.replace(/\//g, '\\') : jsonFile;
            testArgs = [`--gtest_output=json:${jsonPath}`];
            if (baseName === 'test_js_gtest' && targetCategory === 'baseline') {
                testArgs.push('--baseline');
            }
            // Special filter for input roundtrip
            if (baseName === 'test_input_roundtrip_gtest') {
                testArgs.unshift('--gtest_filter=JsonTests.*');
            }
            if (baseName === 'test_ui_automation_gtest') {
                const uiJobs = process.env.LAMBDA_UI_TEST_JOBS || '';
                if (uiJobs) {
                    testArgs.push('--jobs', uiJobs);
                }
            }
            spawnArgs = testArgs;
        } else if (baseName === 'test_flex_standalone') {
            // No special args — output parsed manually
            spawnArgs = testArgs;
        } else {
            // Criterion-based: --json=FILE
            testArgs = [`--json=${jsonFile}`];
            spawnArgs = testArgs;
        }

        const child = spawn(command, spawnArgs, {
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
            if (testInfo.isGtest) {
                normalizeGtestCaseTimes(jsonFile);
            }
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
    let caseTimings = [];
    let capturedCaseDurationMs = 0;

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
    } else if (
        data.summary &&
        typeof data.summary.passed === 'number' &&
        typeof data.summary.failed === 'number'
    ) {
        // Scripted test report format, e.g. Lambda MathLive markup adapter
        passed = data.summary.passed;
        failed = data.summary.failed;
        total = typeof data.summary.total === 'number' ? data.summary.total : passed + failed;
        if (Array.isArray(data.results)) {
            failedTests = data.results
                .filter(t => t.pass === false)
                .map(t => `[${baseName}] ${t.key || t.name || t.formula || 'unnamed case'}`);
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
                    const capturedMs = getCapturedCaseDurationMs(tc);
                    const durationMs = capturedMs > 0 ? capturedMs : parseDurationMs(tc.time);
                    if (durationMs > 0) {
                        caseTimings.push({
                            name: `${suite.name}.${tc.name}`,
                            duration_ms: durationMs,
                        });
                        if (capturedMs > 0) capturedCaseDurationMs += capturedMs;
                    }
                    const failureCount = Array.isArray(tc.failures) ?
                        tc.failures.length : (tc.failures || 0);
                    if (failureCount > 0) {
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
        caseTimings: caseTimings.sort((a, b) => b.duration_ms - a.duration_ms),
        capturedCaseDurationMs,
        timedOut: false,
    };
}

// ─── Parallel execution with concurrency limit ─────────────────────────────────

async function runAllTests(tests, labelOffset = 0, labelTotal = tests.length) {
    const results = new Map(); // baseName -> result + testInfo
    let nextIndex = 0;
    const running = new Map();
    let heartbeat = null;

    return new Promise((resolveAll) => {
        if (!rawOutput && parallelExecution) {
            heartbeat = setInterval(() => {
                if (running.size === 0) return;

                const now = Date.now();
                const active = [...running.values()]
                    .sort((a, b) => (now - b.startMs) - (now - a.startMs))
                    .slice(0, 5)
                    .map(entry => `${entry.testInfo.baseName} ${formatDuration(now - entry.startMs)}`);
                console.log(`   ... still running (${running.size}): ${active.join(', ')}`);
            }, HEARTBEAT_MS);
        }

        function finishIfDone() {
            if (running.size === 0 && nextIndex >= tests.length) {
                if (heartbeat) clearInterval(heartbeat);
                resolveAll(results);
            }
        }

        function startNext() {
            while (running.size < MAX_CONCURRENT && nextIndex < tests.length) {
                const idx = nextIndex++;
                const testInfo = tests[idx];
                const label = `[${labelOffset + idx + 1}/${labelTotal}]`;

                if (!rawOutput) {
                    console.log(`   ${label} Starting ${testInfo.baseName}...`);
                }

                const promise = runTest(testInfo).then((result) => {
                    const entry = running.get(promise);
                    const durationMs = entry ? Date.now() - entry.startMs : 0;
                    running.delete(promise);
                    results.set(testInfo.baseName, { ...result, durationMs, testInfo });

                    if (!rawOutput) {
                        const icon = result.status.includes('PASS') ? '✓' : '✗';
                        console.log(`   ${icon} Completed ${testInfo.baseName} (${result.passed}/${result.total} passed, ${formatDuration(durationMs)})`);
                    }

                    startNext();
                    finishIfDone();
                });

                running.set(promise, { testInfo, startMs: Date.now() });
            }

            // Edge case: no tests to run
            if (tests.length === 0) {
                if (heartbeat) clearInterval(heartbeat);
                resolveAll(results);
            }
        }

        startNext();
    });
}

async function runScheduledTests(tests) {
    const regularTests = tests.filter(t => !t.exclusive);
    const exclusiveTests = tests.filter(t => t.exclusive);
    const results = await runAllTests(regularTests, 0, tests.length);

    for (let i = 0; i < exclusiveTests.length; i++) {
        const testInfo = exclusiveTests[i];
        const label = `[${regularTests.length + i + 1}/${tests.length}]`;

        if (!rawOutput) {
            console.log(`   ${label} Starting ${testInfo.baseName}...`);
        }

        const startMs = Date.now();
        const result = await runTest(testInfo);
        const durationMs = Date.now() - startMs;
        results.set(testInfo.baseName, { ...result, durationMs, testInfo });

        if (!rawOutput) {
            const icon = result.status.includes('PASS') ? '✓' : '✗';
            console.log(`   ${icon} Completed ${testInfo.baseName} (${result.passed}/${result.total} passed, ${formatDuration(durationMs)})`);
        }
    }

    return results;
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
    const allCaseTimings = [];

    // Load input baseline results if available
    let inputResults = null;
    if (inputResultsFile) {
        try {
            inputResults = JSON.parse(fs.readFileSync(inputResultsFile, 'utf8'));
        } catch (e) {
            // ignore if file not found
        }
    }

    // Collect suite results first (need totals for JSON and report)
    const suiteResults = [];
    for (const suiteKey of allSuites) {
        const suite = suiteMap.get(suiteKey);
        let suitePassed = 0, suiteFailed = 0, suiteTotal = 0, suiteDurationMs = 0;

        for (const t of suite.tests) {
            suitePassed += t.result.passed;
            suiteFailed += t.result.failed;
            suiteTotal  += t.result.total;
            suiteDurationMs += t.result.durationMs || 0;
            allFailedTests.push(...t.result.failedTests);
            for (const caseTiming of (t.result.caseTimings || [])) {
                allCaseTimings.push({
                    test_binary: t.baseName,
                    test: caseTiming.name,
                    duration_ms: caseTiming.duration_ms,
                });
            }
        }

        totalTests  += suiteTotal;
        totalPassed += suitePassed;
        totalFailed += suiteFailed;

        suiteResults.push({ suite, suitePassed, suiteFailed, suiteTotal, suiteDurationMs });
    }

    // Save summary JSON
    const summary = {
        total_tests: totalTests,
        total_passed: totalPassed,
        total_failed: totalFailed,
        failed_test_names: allFailedTests,
        top_slow_tests: allCaseTimings
            .slice()
            .sort((a, b) => b.duration_ms - a.duration_ms)
            .slice(0, 20),
        level1_test_suites: allSuites.map(key => {
            const s = suiteMap.get(key);
            let sp = 0, sf = 0, st = 0, sd = 0;
            for (const t of s.tests) { sp += t.result.passed; sf += t.result.failed; st += t.result.total; sd += t.result.durationMs || 0; }
            return { name: s.displayName, total: st, passed: sp, failed: sf, duration_ms: sd, status: sf === 0 ? '✅ PASS' : '❌ FAIL' };
        }),
        level2_c_tests: [...results.entries()].map(([bn, e]) => ({
            name: e.testInfo.displayName, suite: e.testInfo.suite,
            total: e.total, passed: e.passed, failed: e.failed,
            duration_ms: e.durationMs || 0,
            captured_case_duration_ms: e.capturedCaseDurationMs || 0,
            status: e.status,
            top_slow_tests: (e.caseTimings || []).slice(0, 20),
        })),
    };

    const summaryFile = path.join(TEST_OUTPUT_DIR, 'test_summary.json');
    fs.writeFileSync(summaryFile, JSON.stringify(summary, null, 4));

    // Print report
    console.log('');
    console.log(`📁 Results saved to: ${TEST_OUTPUT_DIR}`);
    console.log('   - Individual JSON results: *_results.json');
    console.log('   - Two-level summary: test_summary.json');
    console.log('==============================================================');
    console.log('🏁 TEST RESULTS BREAKDOWN');
    console.log('==============================================================');
    console.log('📊 Test Results:');

    // Print input baseline results first
    if (inputResults) {
        const ip = inputResults.total_passed;
        const if_ = inputResults.total_failed;
        const it = ip + if_;
        const inputStatus = if_ === 0 ? '✅ PASS' : '❌ FAIL';
        console.log('');
        console.log(`📥 Input Parser Tests ${inputStatus} (${ip}/${it} tests)`);
        for (let i = 0; i < inputResults.suites.length; i++) {
            const s = inputResults.suites[i];
            const st = s.passed + s.failed;
            const ss = s.failed === 0 ? '✅ PASS' : '❌ FAIL';
            const prefix = i < inputResults.suites.length - 1 ? '├─' : '└─';
            console.log(` ${prefix}${ss} (${s.passed}/${st} tests) ${s.name}`);
        }
    }

    // Print lambda/runtime suite results
    for (const { suite, suitePassed, suiteFailed, suiteTotal, suiteDurationMs } of suiteResults) {
        const suiteStatus = suiteFailed === 0 ? '✅ PASS' : '❌ FAIL';
        console.log('');
        console.log(`${suite.displayName} ${suiteStatus} (${suitePassed}/${suiteTotal} tests, ${formatDuration(suiteDurationMs)})`);

        for (const t of suite.tests) {
            console.log(` └─${t.result.status} (${t.result.passed}/${t.result.total} tests, ${formatDuration(t.result.durationMs || 0)}) ${t.displayName} (${testArtifactLabel(t.result.testInfo)})`);
        }
    }

    console.log('==============================================================');

    // Combined results including input parsers
    if (inputResults) {
        const ip = inputResults.total_passed;
        const if_ = inputResults.total_failed;
        const it = ip + if_;
        const combinedTotal = totalTests + it;
        const combinedPassed = totalPassed + ip;
        const combinedFailed = totalFailed + if_;
        console.log('📊 Combined Baseline Results (Lambda + Input):');
        console.log(`   Input Parsers:  ${ip}/${it}`);
        console.log(`   Lambda Runtime: ${totalPassed}/${totalTests}`);
        console.log('   ────────────────────────');
        console.log(`   Total Tests: ${combinedTotal}`);
        console.log(`   ✅ Passed:   ${combinedPassed}`);
        if (combinedFailed > 0) {
            console.log(`   ❌ Failed:   ${combinedFailed}`);
        }
    } else {
        console.log('📊 Overall Results:');
        console.log(`   Total Tests: ${totalTests}`);
        console.log(`   ✅ Passed:   ${totalPassed}`);
        if (totalFailed > 0) {
            console.log(`   ❌ Failed:   ${totalFailed}`);
        }
    }

    if (allFailedTests.length > 0) {
        console.log('');
        console.log('🔍 Failed Tests:');
        for (const name of allFailedTests) {
            console.log(`   ❌ ${name}`);
        }
    }

    if (allCaseTimings.length > 0) {
        console.log('');
        console.log('⏱️  Slowest Individual Tests:');
        const slow = allCaseTimings.slice().sort((a, b) => b.duration_ms - a.duration_ms).slice(0, 10);
        for (let i = 0; i < slow.length; i++) {
            const t = slow[i];
            console.log(`   ${String(i + 1).padStart(2, ' ')}. ${formatDuration(t.duration_ms)} ${t.test_binary}: ${t.test}`);
        }
    }

    console.log('==============================================================');

    return { totalFailed };
}

// ─── Main ───────────────────────────────────────────────────────────────────────

async function main() {
    console.log('==============================================================');
    console.log('🧪 Lambda Test Suite Runner (Node.js)');
    console.log('==============================================================');
    console.log(`Platform: ${os.platform()} | CPUs: ${os.cpus().length} | Idle timeout: ${IDLE_TIMEOUT_MS / 1000}s (${IDLE_TIMEOUT.source})`);
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
    tests = scheduleTests(tests);

    if (tests.length === 0) {
        console.error('❌ No test executables found matching filters');
        process.exit(1);
    }

    console.log(`📋 Found ${tests.length} test(s)`);
    console.log('');

    // Run tests
    const results = await runScheduledTests(tests);

    // Display and save results
    if (!rawOutput) {
        const { totalFailed } = displayResults(config, results);
        process.exitCode = totalFailed > 0 ? 1 : 0;
    } else {
        console.log('');
        console.log(`📁 Raw output mode - results saved to: ${TEST_OUTPUT_DIR}`);
        let totalFailed = 0;
        for (const entry of results.values()) {
            totalFailed += entry.failed || 0;
        }
        process.exitCode = totalFailed > 0 ? 1 : 0;
    }
}

main().catch((err) => {
    console.error('Fatal error:', err);
    process.exit(1);
});
