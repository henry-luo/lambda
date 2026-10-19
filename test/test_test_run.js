#!/usr/bin/env node
'use strict';

// Exercise the runner as a subprocess so selection, exits and saved totals agree.
const assert = require('assert/strict');
const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');
const { snapshot_result } = require('./run_radiant_checks.js');

const ROOT = path.resolve(__dirname, '..');
const runner = path.join(ROOT, 'test/test_run.js');
const reportIndex = process.argv.indexOf('--report');
const report = reportIndex >= 0 ? process.argv[reportIndex + 1] : null;
const results = [];
fs.mkdirSync(path.join(ROOT, 'temp'), { recursive: true });
const fixture = fs.mkdtempSync(path.join(ROOT, 'temp/test-run-contracts-'));
const logFault = path.join(fixture, 'log-fault.cjs');
fs.writeFileSync(logFault, `
if (process.argv[1] === ${JSON.stringify(runner)}) {
    const fs = require('fs');
    const { Writable } = require('stream');
    const original = fs.createWriteStream;
    const failure = () => Object.assign(new Error('ENOSPC: simulated full disk'), { code: 'ENOSPC' });
    fs.createWriteStream = function(file, options) {
        if (!String(file).endsWith(process.env.FAKE_LOG_FAULT_PATH)) return original.call(this, file, options);
        if (process.env.FAKE_LOG_FAULT_MODE !== 'open') fs.writeFileSync(file, '');
        const stream = new Writable({ write(data, encoding, callback) {
            if (process.env.FAKE_LOG_FAULT_MODE === 'slow') {
                setTimeout(() => { fs.appendFileSync(file, data); callback(); }, 2);
            } else setTimeout(() => callback(failure()), Number(process.env.FAKE_LOG_FAULT_DELAY || 0));
        } });
        let peak = 0;
        const write = stream.write;
        stream.write = function(...args) {
            const result = write.apply(this, args);
            peak = Math.max(peak, this.writableLength);
            return result;
        };
        stream.on('finish', () => {
            if (process.env.FAKE_LOG_BUFFER_TRACE) fs.writeFileSync(process.env.FAKE_LOG_BUFFER_TRACE, String(peak));
        });
        if (process.env.FAKE_LOG_FAULT_MODE === 'open') process.nextTick(() => stream.destroy(failure()));
        return stream;
    };
    const append = fs.appendFileSync;
    fs.appendFileSync = function(file, ...args) {
        if (process.env.FAKE_APPEND_FAULT && String(file).endsWith(process.env.FAKE_APPEND_FAULT)) throw failure();
        return append.call(this, file, ...args);
    };
}
`);

function invoke(args, env = {}) {
    const child = spawnSync(process.execPath, [runner, ...args], {
        cwd: ROOT, encoding: 'utf8', timeout: 15000,
        env: { ...process.env, ...env }, maxBuffer: 4 * 1024 * 1024,
    });
    if (child.error) throw child.error;
    return child;
}

const inventory = JSON.parse(invoke(['--target=radiant', '--category=baseline', '--list']).stdout);
const fake = `#!/usr/bin/env node
const fs = require('fs');
const path = require('path');
const name = path.basename(process.argv[1], '.exe');
const requestedScenario = name === process.env.FAKE_TEST ? process.env.FAKE_SCENARIO : 'pass';
if (process.argv.includes('--gtest_list_tests')) {
    if (requestedScenario !== 'list-empty') {
        const names = process.env.FAKE_CASE_NAMES ? JSON.parse(process.env.FAKE_CASE_NAMES) :
            ['Case0', 'Case1', 'Case2', 'Case3', 'DISABLED_Off'];
        process.stdout.write('Fixture.\\n' + names.map(name => '  ' + name + '\\n').join(''));
    }
    if (requestedScenario === 'list-duplicate') process.stdout.write('  Case0\\n');
    if (requestedScenario === 'list-crash') process.kill(process.pid, 'SIGKILL');
    else process.exit(0);
}
const filter = process.argv.find(arg => arg.startsWith('--gtest_filter=Fixture.'));
const workerCase = filter ? filter.slice('--gtest_filter=Fixture.'.length) : '';
const scenario = !workerCase || workerCase === 'Case1' ? requestedScenario : 'pass';
const output = process.argv.find(arg => arg.startsWith('--gtest_output=json:')).slice('--gtest_output=json:'.length);
if (process.env.FAKE_SUITE_TRACE_DIR) {
    const trace = { name, case: workerCase, start: Date.now(), cap: Number(process.env.LAMBDA_TEST_MAX_CONCURRENT),
        args: process.argv.slice(2) };
    process.on('exit', () => fs.writeFileSync(path.join(process.env.FAKE_SUITE_TRACE_DIR,
        name + '-' + (workerCase || path.basename(output)) + '.json'), JSON.stringify({ ...trace, end: Date.now() })));
}
if (scenario === 'no-json') process.exit(1);
if (scenario === 'invalid-json') { fs.writeFileSync(output, '{'); process.exit(0); }
const data = { tests: scenario === 'empty' ? 0 : 4, failures: 0,
    skipped: scenario === 'skip' ? 1 : 0, args: process.argv.slice(2),
    testsuites: [{ name: 'Fixture', testsuite: [{ name: 'Case', time: '0.001s' }] }] };
if (workerCase) {
    if (scenario !== 'empty') data.tests = 1;
    data.testsuites[0].testsuite[0] = { name: scenario === 'wrong-case' ? 'Other' : workerCase,
        status: 'RUN', result: scenario === 'skip' ? 'SKIPPED' : 'COMPLETED', time: '0.001s' };
    if (process.env.FAKE_CASE_TRACE_DIR) {
        const trace = { start: Date.now(), cap: process.env.LAMBDA_TEST_MAX_CONCURRENT,
            cwd: process.cwd(), args: process.argv.slice(2) };
        process.on('exit', () => fs.writeFileSync(path.join(process.env.FAKE_CASE_TRACE_DIR, workerCase + '.json'),
            JSON.stringify({ ...trace, end: Date.now() })));
    }
    if (process.env.FAKE_VERIFY_WORKSPACE) {
        const scratch = path.join('temp', 'shared-name.txt');
        fs.writeFileSync(scratch, workerCase);
        process.on('exit', () => {
            if (process.cwd() === process.env.FAKE_VERIFY_WORKSPACE ||
                !fs.existsSync('lambda.exe') || !fs.existsSync('test') ||
                !fs.readFileSync('lambda.exe').equals(fs.readFileSync(process.env.RADIANT_TEST_EXECUTABLE)) ||
                fs.readFileSync(scratch, 'utf8') !== workerCase) process.exitCode = 9;
        });
    }
}
if (scenario === 'launches') {
    const phases = path.join(path.dirname(output), 'view-phases.tsv');
    fs.writeFileSync(phases, 'metric\\tmilliseconds\\nload\\t75.5\\ncleanup\\t4.5\\n');
    data.testsuites[0].testsuite[0].properties = [
        { key: 'lambda_script_elapsed_ms', value: '125.5' },
        { key: 'radiant_view_elapsed_ms_fixture', value: '125.5' },
        { key: 'radiant_view_phases_fixture', value: phases },
        { key: 'radiant_view_log_fixture', value: 'view.log' },
    ];
}
fs.writeFileSync(output, JSON.stringify(data));
if (scenario === 'log-flush') process.stdout.write('final log write\\n');
if (process.env.FAKE_VIEW_ARTIFACTS && name === 'test_radiant_view_gtest') {
    const directory = path.join(process.env.LAMBDA_TEST_OUTPUT_DIR, 'radiant_view', String(process.pid));
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(path.join(directory, 'view.log'), workerCase || 'suite');
}
if (scenario === 'log-io') {
    process.stdout.write('log fault stdout\\n');
    process.stderr.write('log fault stderr\\n');
    setInterval(() => {}, 1000);
} else if (scenario === 'flood') {
    process.stdout.end(Buffer.alloc(1024 * 1024, 'o'));
    process.stderr.end(Buffer.alloc(1024 * 1024, 'e'));
} else if (scenario === 'crash') process.kill(process.pid, 'SIGKILL');
else if (process.env.FAKE_SUITE_TRACE_DIR) setTimeout(() => process.exit(0), 200);
else if (workerCase && requestedScenario === 'parallel') setTimeout(() => process.exit(0), 200);
else if (scenario === 'timeout') setInterval(() => {}, 1000);
else process.exit(scenario === 'exit' ? 7 : 0);
`;
for (const test of inventory.filter(t => t.buildTarget)) {
    fs.writeFileSync(path.join(fixture, test.buildTarget + '.exe'), fake, { mode: 0o755 });
}

function check(name, fn) {
    try { fn(); results.push({ name, pass: true }); console.log(`PASS: ${name}`); }
    catch (error) {
        results.push({ name, pass: false, error: error.message });
        console.error(`FAIL: ${name}: ${error.message}`);
    }
}

function runSelected(names, scenario = 'pass', extra = [], env = {}, parallel = false, previousSummary = null,
                     output = path.join(fixture, 'results-' + results.length)) {
    fs.mkdirSync(output, { recursive: true });
    // A stale passing artifact must never hide a child that fails to write JSON.
    fs.writeFileSync(path.join(output, names[0] + '_results.json'), JSON.stringify({ tests: 99, failures: 0,
        time: env.FAKE_SLOW_CASE ? '9s' : '0s',
        testsuites: env.FAKE_SLOW_CASE ? [{ name: 'Fixture', testsuite: [{ name: env.FAKE_SLOW_CASE, time: '9s' }] }] : [] }));
    if (previousSummary) fs.writeFileSync(path.join(output, 'test_summary.json'),
        JSON.stringify({ level2_c_tests: previousSummary }));
    const excluded = inventory.filter(t => !names.includes(t.baseName)).map(t => t.baseName);
    const executable = path.join(fixture, names[0] + '.exe');
    if (scenario === 'missing') fs.unlinkSync(executable);
    let child;
    try {
        child = invoke(['--target=radiant', '--category=baseline', ...(parallel ? [] : ['--sequential']),
            '--exclude-test=' + excluded.join(','), ...extra], {
            LAMBDA_TEST_BIN_DIR: fixture, LAMBDA_TEST_OUTPUT_DIR: output,
            LAMBDA_TEST_IDLE_TIMEOUT: '1', FAKE_TEST: names[0], FAKE_SCENARIO: scenario,
            LAMBDA_RADIANT_VIEW_TEST_JOBS: parallel ? '4' : '1',
            ...env,
        });
    } finally {
        if (scenario === 'missing') fs.writeFileSync(executable, fake, { mode: 0o755 });
    }
    return { child, output, summary: JSON.parse(fs.readFileSync(path.join(output, 'test_summary.json'), 'utf8')) };
}

try {
    check('Selected PDF workers preserve every case and isolate fixed scratch paths', () => {
        const name = 'test_pdf_render_visual_gtest';
        fs.writeFileSync(path.join(fixture, name + '.exe'), fake, { mode: 0o755 });
        const output = path.join(fixture, 'pdf-results');
        const traces = path.join(fixture, 'pdf-traces');
        const host = path.join(fixture, 'pdf-host.exe');
        fs.writeFileSync(host, 'workspace host fixture');
        fs.mkdirSync(output); fs.mkdirSync(traces);
        const selected = JSON.parse(invoke(['--include-test=' + name, '--list']).stdout);
        assert.equal(selected.length, 1);
        assert.equal(selected[0].baseName, name);
        assert.equal(selected[0].isolatedCwd, true);
        const child = invoke(['--include-test=' + name], {
            LAMBDA_TEST_BIN_DIR: fixture, LAMBDA_TEST_OUTPUT_DIR: output,
            LAMBDA_PDF_TEST_JOBS: '2', FAKE_TEST: name, FAKE_SCENARIO: 'parallel',
            FAKE_CASE_TRACE_DIR: traces, FAKE_VERIFY_WORKSPACE: ROOT,
            RADIANT_TEST_EXECUTABLE: host,
        });
        assert.equal(child.status, 0, child.stdout + child.stderr);
        const data = JSON.parse(fs.readFileSync(path.join(output, name + '_results.json')));
        assert.equal(data.tests, 4); assert.equal(data.failures, 0);
        const cases = fs.readdirSync(traces).map(file => JSON.parse(fs.readFileSync(path.join(traces, file))));
        assert.equal(cases.length, 4);
        const directories = new Set(cases.map(item => item.cwd));
        assert.equal(directories.size, 2);
        assert.ok([...directories].every(directory => directory.startsWith(path.join(ROOT, 'temp') + path.sep)));
        for (const directory of directories) fs.rmSync(directory, { recursive: true, force: true });
    });
    check('Radiant inventory includes every baseline phase', () => {
        const selected = new Set(inventory.map(t => t.baseName));
        for (const name of ['test_scene3d_unit_gtest', 'test_map_gtest', 'test_paged_media_gtest',
            'test_ui_automation_gtest', 'test_radiant_view_ui_gtest', 'test_dom_ui_gtest',
            'test_page_load_gtest', 'test_radiant_view_gtest', 'test_layout_fuzzy_gtest',
            'test_wpt_css_syntax_gtest', 'test_wpt_input_events_gtest',
            'test_radiant_layout_baseline', 'test_radiant_page_snapshot', 'test_radiant_render_baseline']) {
            assert.ok(selected.has(name), name);
        }
        assert.ok(!selected.has('test_scene3d_gtest'));
        assert.ok(!selected.has('test_view_reuse_gtest'));
        const paged = inventory.find(t => t.baseName === 'test_paged_media_gtest');
        assert.equal(paged.buildTarget, 'test_paged_media_gtest');
        assert.deepEqual(paged.runnerArgs, []);
        const extended = JSON.parse(invoke(['--target=extended', '--list']).stdout);
        const reuse = extended.find(t => t.baseName === 'test_view_reuse_gtest');
        assert.ok(reuse);
        assert.equal(reuse.buildTarget, 'test_view_reuse_gtest');
        assert.deepEqual(reuse.runnerArgs, []);
        assert.ok(!extended.some(t => t.baseName === 'test_view_reuse_extended_gtest'));
        assert.equal(selected.size, inventory.length);
        assert.ok(inventory.every(t => t.suite === 'radiant' && t.category === 'baseline'));
        const view = inventory.find(t => t.baseName === 'test_radiant_view_gtest');
        assert.ok(view.runnerArgs.includes('--gtest_filter=*-RadiantViewTest.Doom*'));
        assert.equal(view.caseWorkers, 3);
        assert.deepEqual(view.artifactDirs, ['radiant_view']);
        assert.ok(view.caseWorkerArgs.includes('--radiant-case-worker'));
        for (const name of ['test_radiant_layout_baseline', 'test_radiant_page_snapshot', 'test_wpt_input_events_gtest']) {
            const check = inventory.find(t => t.baseName === name);
            assert.equal(check.exclusive, false);
            assert.equal(check.workerSlots, name === 'test_radiant_layout_baseline' ? 3 : 2);
        }
    });
    check('Native build targets deduplicate runner aliases', () => {
        const actual = invoke(['--target=radiant', '--category=baseline', '--list-build-targets']).stdout.trim().split(/\s+/);
        assert.deepEqual(actual, [...new Set(inventory.map(t => t.buildTarget).filter(Boolean))]);
        assert.equal(actual.filter(t => t === 'test_ui_automation_gtest').length, 1);
    });
    check('All selected suites reach the shared report', () => {
        const { child, summary } = runSelected(['test_paged_media_gtest', 'test_map_gtest']);
        assert.equal(child.status, 0, child.stderr);
        assert.equal(summary.total_passed, 8);
        assert.equal(summary.total_failed, 0);
        assert.equal(summary.level2_c_tests.length, 2);
        assert.equal(summary.level1_test_suites[0].passed, summary.total_passed);
        assert.equal(summary.level1_test_suites[0].failed, summary.total_failed);
        assert.ok(summary.level2_c_tests.every(t => t.suite === 'radiant'));
    });
    for (const scenario of ['exit', 'crash', 'no-json', 'invalid-json', 'missing', 'empty', 'timeout']) {
        check(`${scenario} fails the report and allows the next suite to run`, () => {
            const { child, summary } = runSelected(['test_paged_media_gtest', 'test_map_gtest'], scenario);
            assert.equal(child.status, 1, child.stderr);
            assert.equal(summary.total_failed, 1);
            assert.equal(summary.level2_c_tests.find(t => t.test === 'test_map_gtest').passed, 4);
            assert.ok(summary.failed_test_names.some(t => t.includes('test_paged_media_gtest')));
        });
    }
    for (const mode of ['open', 'write']) check(`A log ${mode} ENOSPC fails the suite and allows the next suite to run`, () => {
        const { child, summary } = runSelected(['test_paged_media_gtest', 'test_map_gtest'], 'log-io', [], {
            NODE_OPTIONS: `--require=${logFault}`, FAKE_LOG_FAULT_MODE: mode,
            FAKE_LOG_FAULT_PATH: 'test_paged_media_gtest.log',
        });
        assert.equal(child.status, 1, child.stderr);
        assert.ok(!child.stderr.includes("Unhandled 'error' event"));
        assert.equal(summary.total_failed, 1);
        assert.equal(summary.level2_c_tests.find(t => t.test === 'test_map_gtest').passed, 4);
        assert.ok(summary.failed_test_names.some(t => t.includes('log write failed: ENOSPC')));
    });
    check('A log flush ENOSPC overrides passing JSON after the child exits', () => {
        const { child, summary } = runSelected(['test_paged_media_gtest'], 'log-flush', [], {
            NODE_OPTIONS: `--require=${logFault}`, FAKE_LOG_FAULT_MODE: 'write', FAKE_LOG_FAULT_DELAY: '100',
            FAKE_LOG_FAULT_PATH: 'test_paged_media_gtest.log',
        });
        assert.equal(child.status, 1, child.stderr);
        assert.equal(summary.total_failed, 1);
        assert.ok(summary.failed_test_names.some(t => t.includes('log write failed: ENOSPC')));
    });
    check('A slow log sink bounds buffering and retains both output pipes', () => {
        const trace = path.join(fixture, 'log-buffer-size');
        const { child, output } = runSelected(['test_paged_media_gtest'], 'flood', [], {
            NODE_OPTIONS: `--require=${logFault}`, FAKE_LOG_FAULT_MODE: 'slow',
            FAKE_LOG_FAULT_PATH: 'test_paged_media_gtest.log', FAKE_LOG_BUFFER_TRACE: trace,
        });
        assert.equal(child.status, 0, child.stderr);
        const log = fs.readFileSync(path.join(output, 'test_paged_media_gtest.log'), 'utf8');
        assert.equal(log.split('o').length - 1, 1024 * 1024);
        assert.equal(log.split('e').length - 1, 1024 * 1024);
        assert.ok(Number(fs.readFileSync(trace, 'utf8')) <= 256 * 1024);
    });
    check('View reruns replace only their declared artifacts before starting case workers', () => {
        const output = path.join(fixture, 'artifact-retention');
        fs.mkdirSync(path.join(output, 'radiant_view', 'stale'), { recursive: true });
        fs.writeFileSync(path.join(output, 'radiant_view', 'stale', 'view.log'), 'old debug log');
        const keep = path.join(output, 'unrelated.log');
        fs.writeFileSync(keep, 'keep');
        const first = runSelected(['test_radiant_view_gtest'], 'pass', [], { FAKE_VIEW_ARTIFACTS: '1' },
            true, null, output);
        assert.equal(first.child.status, 0, first.child.stderr);
        const directory = path.join(output, 'radiant_view');
        const oldRuns = fs.readdirSync(directory);
        assert.equal(oldRuns.length, 4);
        const second = runSelected(['test_radiant_view_gtest'], 'pass', [], { FAKE_VIEW_ARTIFACTS: '1' },
            true, null, output);
        assert.equal(second.child.status, 0, second.child.stderr);
        assert.equal(fs.readdirSync(directory).length, 4);
        assert.ok(oldRuns.every(run => !fs.existsSync(path.join(directory, run))));
        assert.equal(fs.readFileSync(keep, 'utf8'), 'keep');
        const third = runSelected(['test_map_gtest'], 'pass', [], {}, true, null, output);
        assert.equal(third.child.status, 0, third.child.stderr);
        assert.equal(fs.readdirSync(directory).length, 4);
    });
    check('Skipped cases are excluded from passing counts', () => {
        const { child, summary } = runSelected(['test_paged_media_gtest'], 'skip');
        assert.equal(child.status, 0);
        assert.equal(summary.total_passed, 3);
        assert.equal(summary.total_skipped, 1);
    });
    check('Parallel view subprocess timings and phases reach the shared report', () => {
        const { child, summary } = runSelected(['test_paged_media_gtest'], 'launches');
        assert.equal(child.status, 0, child.stderr);
        const result = summary.level2_c_tests[0];
        assert.equal(result.top_slow_tests[0].duration_ms, 125.5);
        assert.equal(result.launch_timings.length, 1);
        assert.equal(result.launch_timings[0].launch, 'fixture');
        assert.equal(result.launch_timings[0].duration_ms, 125.5);
        assert.deepEqual(result.launch_timings[0].phases, { load: 75.5, cleanup: 4.5 });
        assert.equal(result.launch_timings[0].log_file, 'view.log');
    });
    check('Split case variants inherit their retired bundle timing on the first run', () => {
        const trace = path.join(fixture, 'trace-split-cases');
        fs.mkdirSync(trace);
        const { child, output, summary } = runSelected(['test_radiant_view_gtest'], 'parallel', [], {
            LAMBDA_TEST_MAX_CONCURRENT: '2', LAMBDA_RADIANT_VIEW_TEST_JOBS: '2',
            FAKE_CASE_TRACE_DIR: trace, FAKE_SLOW_CASE: 'Case',
            FAKE_CASE_NAMES: JSON.stringify(['Unknown', 'Case_auto', 'Case_jit']),
        }, true);
        assert.equal(child.status, 0, child.stderr);
        assert.equal(summary.total_passed, 3);
        const starts = Object.fromEntries(fs.readdirSync(trace).map(file =>
            [path.basename(file, '.json'), JSON.parse(fs.readFileSync(path.join(trace, file), 'utf8')).start]));
        assert.ok(starts.Case_auto < starts.Unknown && starts.Case_jit < starts.Unknown);
        const data = JSON.parse(fs.readFileSync(path.join(output, 'test_radiant_view_gtest_results.json'), 'utf8'));
        assert.deepEqual(data.testsuites[0].testsuite.map(tc => tc.name), ['Unknown', 'Case_auto', 'Case_jit']);
    });
    for (const [runner, jobsEnv, workerArg] of [
        ['test_radiant_view_gtest', 'LAMBDA_RADIANT_VIEW_TEST_JOBS', '--radiant-case-worker'],
        ['test_scene3d_unit_gtest', null, null],
    ]) for (const [limit, parallel] of [[2, true], [4, true], [4, false]]) {
        check(`${runner} case workers preserve inventory within ${limit} slots with ${parallel ? 'parallel' : 'sequential'} suites`, () => {
            const trace = path.join(fixture, `trace-${runner}-${limit}-${parallel}`);
            fs.mkdirSync(trace);
            const { child, summary } = runSelected([runner], 'parallel', [], {
                LAMBDA_TEST_MAX_CONCURRENT: String(limit), ...(jobsEnv ? { [jobsEnv]: '4' } : {}),
                FAKE_CASE_TRACE_DIR: trace,
            }, parallel);
            assert.equal(child.status, 0, child.stderr);
            assert.equal(summary.total_passed, 4);
            assert.equal(summary.total_tests, 4);
            const rows = fs.readdirSync(trace).map(file => JSON.parse(fs.readFileSync(path.join(trace, file), 'utf8')));
            assert.equal(rows.length, 4);
            assert.ok(rows.every(row => row.cap === '1' && (!workerArg || row.args.includes(workerArg))));
            const events = rows.flatMap(row => [{ time: row.start, delta: 1 }, { time: row.end, delta: -1 }])
                .sort((a, b) => a.time - b.time || a.delta - b.delta);
            let active = 0, peak = 0;
            for (const event of events) { active += event.delta; peak = Math.max(peak, active); }
            assert.ok(peak > 1 && peak <= limit, `peak workers: ${peak}`);
        });
    }
    for (const scenario of ['exit', 'crash', 'no-json', 'invalid-json', 'empty', 'wrong-case', 'timeout']) {
        check(`A ${scenario} case worker fails its own case and preserves other cases`, () => {
            const { child, summary } = runSelected(['test_radiant_view_gtest'], scenario, [], {}, true);
            assert.equal(child.status, 1, child.stderr);
            assert.equal(summary.total_tests, 4);
            assert.equal(summary.total_passed, 3);
            assert.equal(summary.total_failed, 1);
            assert.ok(summary.failed_test_names.some(name => name.includes('Fixture.Case1')));
        });
    }
    for (const mode of ['open', 'write']) check(`A case log ${mode} ENOSPC fails its own case and preserves other cases`, () => {
        const { child, output, summary } = runSelected(['test_radiant_view_gtest'], 'log-io', [], {
            NODE_OPTIONS: `--require=${logFault}`, FAKE_LOG_FAULT_MODE: mode,
            FAKE_LOG_FAULT_PATH: path.join('test_radiant_view_gtest_cases', '1.log'),
        }, true);
        assert.equal(child.status, 1, child.stderr);
        assert.equal(summary.total_passed, 3);
        assert.equal(summary.total_failed, 1);
        assert.ok(summary.failed_test_names.some(t => t.includes('Fixture.Case1')));
        const data = JSON.parse(fs.readFileSync(path.join(output, 'test_radiant_view_gtest_results.json'), 'utf8'));
        assert.ok(data.testsuites[0].testsuite[1].failures.some(f => f.failure.includes('ENOSPC')));
    });
    check('Discovery log ENOSPC is a reported failure', () => {
        const { child, summary } = runSelected(['test_radiant_view_gtest'], 'pass', [], {
            NODE_OPTIONS: `--require=${logFault}`, FAKE_LOG_FAULT_MODE: 'open',
            FAKE_LOG_FAULT_PATH: path.join('test_radiant_view_gtest_cases', 'list.log'),
        }, true);
        assert.equal(child.status, 1, child.stderr);
        assert.equal(summary.total_failed, 1);
        assert.ok(summary.failed_test_names.some(t => t.includes('ENOSPC')));
    });
    check('Merged case log ENOSPC waits for workers and allows the next suite to run', () => {
        const { child, summary } = runSelected(['test_radiant_view_gtest', 'test_map_gtest'], 'parallel', [], {
            NODE_OPTIONS: `--require=${logFault}`, FAKE_APPEND_FAULT: 'test_radiant_view_gtest.log',
        }, true);
        assert.equal(child.status, 1, child.stderr);
        assert.equal(summary.total_failed, 1);
        assert.equal(summary.level2_c_tests.find(t => t.test === 'test_map_gtest').passed, 4);
        assert.ok(summary.failed_test_names.some(t => t.includes('runner error: ENOSPC')));
    });
    check('Previous case durations start slow work early without reusing stale results', () => {
        const trace = path.join(fixture, 'trace-order');
        fs.mkdirSync(trace);
        const { child, summary } = runSelected(['test_radiant_view_gtest'], 'parallel', [], {
            LAMBDA_TEST_MAX_CONCURRENT: '2', FAKE_SLOW_CASE: 'Case3', FAKE_CASE_TRACE_DIR: trace,
        }, true);
        assert.equal(child.status, 0, child.stderr);
        assert.equal(summary.total_passed, 4);
        const started = fs.readdirSync(trace).map(file => ({ file,
            ...JSON.parse(fs.readFileSync(path.join(trace, file), 'utf8')) })).sort((a, b) => a.start - b.start);
        assert.ok(started.slice(0, 2).some(row => row.file === 'Case3.json'));
    });
    for (const scenario of ['list-empty', 'list-duplicate', 'list-crash']) {
        check(`${scenario} discovery remains a suite failure`, () => {
            const { child, summary } = runSelected(['test_radiant_view_gtest'], scenario, [], {}, true);
            assert.equal(child.status, 1, child.stderr);
            assert.equal(summary.total_failed, 1);
            assert.ok(summary.failed_test_names.some(name => name.includes('discovery')));
        });
    }
    check('Case workers preserve skipped counts and phase properties', () => {
        const skipped = runSelected(['test_radiant_view_gtest'], 'skip', [], {}, true);
        assert.equal(skipped.child.status, 0, skipped.child.stderr);
        assert.equal(skipped.summary.total_passed, 3);
        assert.equal(skipped.summary.total_skipped, 1);
        const phases = runSelected(['test_radiant_view_gtest'], 'launches', [], {}, true);
        assert.equal(phases.child.status, 0, phases.child.stderr);
        assert.equal(phases.summary.total_passed, 4);
        assert.equal(phases.summary.level2_c_tests[0].launch_timings.length, 1);
        assert.deepEqual(phases.summary.level2_c_tests[0].launch_timings[0].phases, { load: 75.5, cleanup: 4.5 });
    });
    check('Raw output still saves the final summary', () => {
        const { child, summary } = runSelected(['test_paged_media_gtest'], 'exit', ['--raw']);
        assert.equal(child.status, 1);
        assert.equal(summary.total_failed, 1);
    });
    check('CSS Syntax retains its minimum passing inventory', () => {
        const { child, summary } = runSelected(['test_wpt_css_syntax_gtest']);
        assert.equal(child.status, 1);
        assert.ok(summary.failed_test_names.some(t => t.includes('at least 25 passing tests')));
    });
    check('UI runner aliases retain separate arguments and artifacts', () => {
        const names = ['test_ui_automation_gtest', 'test_radiant_view_ui_gtest', 'test_dom_ui_gtest'];
        const { child, output, summary } = runSelected(names);
        assert.equal(child.status, 0, child.stderr);
        assert.equal(summary.level2_c_tests.length, 3);
        for (const [index, suite] of ['baseline,dtna,timeline,wired,clay,blade', 'view', 'dom'].entries()) {
            const data = JSON.parse(fs.readFileSync(path.join(output, names[index] + '_results.json'), 'utf8'));
            assert.equal(data.args[data.args.indexOf('--suite') + 1], suite);
        }
    });
    for (const budget of [5, 12]) check(`Nested pools share ${budget} slots, backfill light suites, and leave exclusive checks alone`, () => {
        const trace = path.join(fixture, `trace-suites-${budget}`);
        fs.mkdirSync(trace);
        const names = ['test_ui_automation_gtest', 'test_radiant_view_gtest', 'test_rdt_vector_gtest', 'test_map_gtest'];
        const { child, summary } = runSelected(names, 'pass', [], {
            LAMBDA_TEST_MAX_CONCURRENT: String(budget), FAKE_SUITE_TRACE_DIR: trace,
        }, true);
        assert.equal(child.status, 0, child.stderr);
        assert.equal(summary.level2_c_tests.length, names.length);
        const rows = fs.readdirSync(trace).map(file => JSON.parse(fs.readFileSync(path.join(trace, file), 'utf8')));
        const events = rows.flatMap(row => [{ time: row.start, delta: row.cap, name: row.name },
            { time: row.end, delta: -row.cap, name: row.name }])
            .sort((a, b) => a.time - b.time || a.delta - b.delta);
        let active = 0, peak = 0, parallelPeak = 0;
        for (const event of events) {
            active += event.delta;
            peak = Math.max(peak, active);
            if (event.name !== 'test_map_gtest') parallelPeak = Math.max(parallelPeak, active);
        }
        assert.equal(active, 0);
        const ceiling = Math.min(budget, Math.max(1, Math.floor(os.cpus().length * 1.25)));
        assert.equal(peak, ceiling, `peak reserved workers: ${peak}`);
        assert.ok(parallelPeak <= ceiling);
        if (ceiling >= 5) {
            // slots are reserved during discovery too; short jobs may finish before the first case starts.
            const pools = summary.level2_c_tests;
            const light = pools.find(row => row.test === 'test_rdt_vector_gtest');
            assert.ok(pools.some(row => ['test_ui_automation_gtest', 'test_radiant_view_gtest'].includes(row.test) &&
                row.start_ms < light.end_ms && light.start_ms < row.end_ms));
        }
        const graphics = rows.find(row => row.name === 'test_map_gtest');
        assert.ok(rows.filter(row => row !== graphics).every(row => row.end <= graphics.start));
        const suite = summary.level1_test_suites[0];
        assert.ok(suite.duration_ms < suite.work_duration_ms);
        assert.ok(summary.duration_ms >= suite.duration_ms);
    });
    check('Suite ordering uses subprocess wall time before assertion time', () => {
        const trace = path.join(fixture, 'trace-suite-order');
        fs.mkdirSync(trace);
        const names = ['test_radiant_view_gtest', 'test_ui_automation_gtest'];
        const { child } = runSelected(names, 'pass', [], {
            LAMBDA_TEST_MAX_CONCURRENT: '4', FAKE_SUITE_TRACE_DIR: trace, FAKE_SLOW_CASE: 'Case3',
        }, true, [{ test: names[0], duration_ms: 1000 }, { test: names[1], duration_ms: 10000 }]);
        assert.equal(child.status, 0, child.stderr);
        const rows = fs.readdirSync(trace).map(file => JSON.parse(fs.readFileSync(path.join(trace, file), 'utf8')));
        const ui = rows.find(row => row.name === names[1]);
        assert.ok(rows.filter(row => row.name === names[0]).every(row => row.start >= ui.end));
    });
    check('Native subprocess pools receive their allocated jobs', () => {
        const names = ['test_page_load_gtest', 'test_layout_fuzzy_gtest'];
        const { child, output, summary } = runSelected(names, 'pass', [], {
            LAMBDA_TEST_MAX_CONCURRENT: '3',
        }, true);
        assert.equal(child.status, 0, child.stderr);
        for (const name of names) {
            const data = JSON.parse(fs.readFileSync(path.join(output, name + '_results.json'), 'utf8'));
            assert.equal(data.args[data.args.indexOf('--jobs') + 1], '2');
            assert.equal(summary.level2_c_tests.find(row => row.test === name).worker_slots, 2);
        }
    });
    // Keep adapter tests independent of native binaries and the external corpus.
    fs.writeFileSync(path.join(fixture, 'make'), `#!/usr/bin/env node
const fs = require('fs');
if (process.env.FAKE_LAYOUT_ARGS) fs.writeFileSync(process.env.FAKE_LAYOUT_ARGS, JSON.stringify(process.argv.slice(2)));
if (process.env.FAKE_LAYOUT_WORKSPACE) fs.writeFileSync(process.env.FAKE_LAYOUT_WORKSPACE, JSON.stringify({
    cwd: process.cwd(), host: fs.realpathSync('lambda.exe'), home: process.env.LAMBDA_HOME,
    cap: Number(process.env.LAMBDA_TEST_MAX_CONCURRENT), start: Date.now() }));
const output = process.argv.find(arg => arg.startsWith('LAYOUT_BASELINE_RESULTS=')).slice('LAYOUT_BASELINE_RESULTS='.length);
if (process.env.FAKE_LAYOUT === 'missing') process.exit(0);
fs.writeFileSync(output, 'form|✅ PASS|3|2|0|1|2\\n');
process.exit(process.env.FAKE_LAYOUT === 'exit' ? 7 : 0);
`, { mode: 0o755 });
    for (const scenario of ['pass', 'exit', 'missing']) {
        check(`Layout adapter preserves counts and handles ${scenario}`, () => {
            const output = path.join(fixture, `layout-${scenario}.json`);
            const child = spawnSync(process.execPath, ['test/run_radiant_checks.js', 'layout', '--report', output], {
                cwd: ROOT, encoding: 'utf8', timeout: 15000,
                env: { ...process.env, PATH: fixture + path.delimiter + process.env.PATH, FAKE_LAYOUT: scenario },
            });
            assert.equal(child.status, scenario === 'pass' ? 0 : 1, child.stderr);
            const data = JSON.parse(fs.readFileSync(output, 'utf8'));
            assert.equal(data.summary.failed, scenario === 'pass' ? 0 : 1);
            if (scenario !== 'missing') {
                assert.equal(data.summary.passed, 3);
                assert.equal(data.summary.partial, 2);
                assert.equal(data.summary.skipped, 1);
                assert.equal(data.suites[0].duration_ms, 2000);
            }
        });
    }
    check('Layout batch workers obey the shared process ceiling', () => {
        const output = path.join(fixture, 'layout-budget.json');
        const trace = path.join(fixture, 'layout-args.json');
        const child = spawnSync(process.execPath, ['test/run_radiant_checks.js', 'layout', '--report', output], {
            cwd: ROOT, encoding: 'utf8', timeout: 15000,
            env: { ...process.env, PATH: fixture + path.delimiter + process.env.PATH, FAKE_LAYOUT: 'pass',
                FAKE_LAYOUT_ARGS: trace, LAMBDA_TEST_MAX_CONCURRENT: '1', LAYOUT_BASELINE_CONCURRENCY: '8' },
        });
        assert.equal(child.status, 0, child.stderr);
        assert.ok(JSON.parse(fs.readFileSync(trace, 'utf8')).includes('LAYOUT_BASELINE_CONCURRENCY=1'));
    });
    check('Layout checks use independent scratch directories and the selected host', () => {
        const directories = [];
        for (let i = 0; i < 2; i++) {
            const trace = path.join(fixture, `layout-workspace-${i}.json`);
            const child = spawnSync(process.execPath, ['test/run_radiant_checks.js', 'layout', '--report',
                path.join(fixture, `layout-isolated-${i}.json`)], {
                cwd: ROOT, encoding: 'utf8', timeout: 15000,
                env: { ...process.env, PATH: fixture + path.delimiter + process.env.PATH,
                    FAKE_LAYOUT: 'pass', FAKE_LAYOUT_WORKSPACE: trace,
                    RADIANT_TEST_EXECUTABLE: path.join(fixture, 'test_map_gtest.exe') },
            });
            assert.equal(child.status, 0, child.stderr);
            const data = JSON.parse(fs.readFileSync(trace, 'utf8'));
            if (process.platform !== 'win32') assert.equal(data.host, path.join(fixture, 'test_map_gtest.exe'));
            assert.deepEqual(fs.readFileSync(data.host), fs.readFileSync(path.join(fixture, 'test_map_gtest.exe')));
            assert.equal(data.home, path.resolve(ROOT, process.env.LAMBDA_HOME || 'lmd'));
            assert.equal(path.dirname(data.cwd), path.join(ROOT, 'temp'));
            assert.ok(fs.statSync(path.join(data.cwd, 'temp')).isDirectory());
            directories.push(data.cwd);
        }
        assert.notEqual(directories[0], directories[1]);
    });
    check('Parallel script pools reserve slots alongside native pools', () => {
        const trace = path.join(fixture, 'layout-native-pool-trace');
        fs.mkdirSync(trace);
        const workspaceTrace = path.join(fixture, 'layout-native-workspace.json');
        const { child, summary } = runSelected(['test_ui_automation_gtest', 'test_radiant_layout_baseline'],
            'pass', [], { PATH: fixture + path.delimiter + process.env.PATH, FAKE_LAYOUT: 'pass',
                FAKE_LAYOUT_WORKSPACE: workspaceTrace, FAKE_SUITE_TRACE_DIR: trace,
                LAMBDA_TEST_MAX_CONCURRENT: '12' }, true);
        assert.equal(child.status, 0, child.stderr);
        const layout = summary.level2_c_tests.find(t => t.test === 'test_radiant_layout_baseline');
        assert.equal(layout.worker_slots, 3);
        assert.equal(layout.total, 6);
        const row = JSON.parse(fs.readFileSync(workspaceTrace, 'utf8'));
        assert.equal(row.cap, 3);
        const native = JSON.parse(fs.readFileSync(path.join(trace, fs.readdirSync(trace)[0]), 'utf8'));
        // the adapter starts while the native pool still owns its granted slots.
        assert.ok(row.start < native.end && native.start < row.start);
    });
    function pooledLayout(data) {
        const child = spawnSync(process.execPath, ['-e', `
const { runCategories } = require('./test/run_layout_baseline.js');
const data = JSON.parse(process.argv[1]);
const tester = {
    _categorySummaries: data.summaries,
    testCategories: async () => data.rows,
    loadBaseline: async category => data.baselines[category],
    classifyBaselineResults: (baseline, rows) => {
        if (JSON.stringify(rows.map(row => row.marker)) !== JSON.stringify(baseline.markers))
            throw new Error('categories with repeated names mixed their results');
        return baseline.classified;
    },
};
runCategories(tester, data.categories).then(records => process.stdout.write(JSON.stringify(records)))
    .catch(error => { console.error(error.message); process.exitCode = 1; });
`, JSON.stringify(data)], { cwd: ROOT, encoding: 'utf8', timeout: 15000 });
        return { child, records: child.status === 0 ? JSON.parse(child.stdout) : null };
    }
    check('Pooled layout keeps partial baselines, regressions and repeated names in their categories', () => {
        const { child, records } = pooledLayout({ categories: ['partial', 'regression'],
            summaries: { partial: { total: 1, skipped: 0 }, regression: { total: 1, skipped: 0 } },
            rows: [{ testName: 'same', marker: 1 }, { testName: 'same', marker: 2 }],
            baselines: {
                partial: { size: 1, markers: [1], classified: { successful: 0, partial: 1, failures: [] } },
                regression: { size: 1, markers: [2], classified: { successful: 0, partial: 0, failures: [{}] } },
            } });
        assert.equal(child.status, 0, child.stderr);
        assert.equal(records[0].partial, 1);
        assert.equal(records[0].failed, 0);
        assert.equal(records[1].failed, 1);
    });
    check('Pooled layout fails missing categories and unrecorded failures', () => {
        const { child, records } = pooledLayout({ categories: ['full', 'missing'],
            summaries: { full: { total: 2, successful: 1, failed: 1, skipped: 3 } },
            rows: [{}, {}], baselines: {} });
        assert.equal(child.status, 0, child.stderr);
        assert.deepEqual(records.map(r => [r.passed, r.partial, r.failed, r.skipped]),
            [[1, 0, 1, 3], [0, 0, 1, 0]]);
    });
    check('Pooled layout rejects unaccounted results', () => {
        const { child } = pooledLayout({ categories: ['one'],
            summaries: { one: { total: 0, successful: 0, failed: 0, skipped: 0 } },
            rows: [{}], baselines: {} });
        assert.equal(child.status, 1);
        assert.match(child.stderr, /inventory does not match/);
    });
    check('Snapshot comparator owns the aggregate regression verdict', () => {
        const result = snapshot_result({ code: 1, signal: null },
            { code: 0, signal: null, output: 'Current: 51 tests, avg elements 92.1%, avg text 92.6%\n' });
        assert.equal(result.summary.failed, 0);
        assert.equal(result.summary.passed, 51);
    });
    check('Snapshot crashes, invalid output and comparator failures remain failures', () => {
        for (const [measured, checked] of [
            [{ code: null, signal: 'SIGKILL' }, { code: 0, output: 'Current: 51 tests\n' }],
            [{ code: 2 }, { code: 0, output: 'Current: 51 tests\n' }],
            [{ code: 1 }, { code: 1, output: 'Current: 51 tests\n' }],
            [{ code: 0 }, { code: 0, output: '' }],
        ]) assert.equal(snapshot_result(measured, checked).summary.failed, 1);
    });
} finally {
    fs.rmSync(fixture, { recursive: true, force: true });
}
const passed = results.filter(t => t.pass).length;
const failed = results.length - passed;
if (report) fs.writeFileSync(report, JSON.stringify({ summary: { passed, failed, total: results.length }, results }, null, 2) + '\n');
process.exitCode = failed ? 1 : 0;
