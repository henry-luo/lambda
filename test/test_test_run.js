#!/usr/bin/env node
'use strict';

// Exercise the runner as a subprocess so selection, exits and saved totals agree.
const assert = require('assert/strict');
const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const { snapshot_result } = require('./run_radiant_checks.js');

const ROOT = path.resolve(__dirname, '..');
const runner = path.join(ROOT, 'test/test_run.js');
const reportIndex = process.argv.indexOf('--report');
const report = reportIndex >= 0 ? process.argv[reportIndex + 1] : null;
const results = [];
fs.mkdirSync(path.join(ROOT, 'temp'), { recursive: true });
const fixture = fs.mkdtempSync(path.join(ROOT, 'temp/test-run-contracts-'));

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
const scenario = name === process.env.FAKE_TEST ? process.env.FAKE_SCENARIO : 'pass';
const output = process.argv.find(arg => arg.startsWith('--gtest_output=json:')).slice('--gtest_output=json:'.length);
if (scenario === 'no-json') process.exit(1);
if (scenario === 'invalid-json') { fs.writeFileSync(output, '{'); process.exit(0); }
const data = { tests: scenario === 'empty' ? 0 : 4, failures: 0,
    skipped: scenario === 'skip' ? 1 : 0, args: process.argv.slice(2),
    testsuites: [{ name: 'Fixture', testsuite: [{ name: 'Case', time: '0.001s' }] }] };
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
if (scenario === 'crash') process.kill(process.pid, 'SIGKILL');
if (scenario === 'timeout') setInterval(() => {}, 1000);
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

function runSelected(names, scenario = 'pass', extra = []) {
    const output = path.join(fixture, 'results-' + results.length);
    fs.mkdirSync(output, { recursive: true });
    // A stale passing artifact must never hide a child that fails to write JSON.
    fs.writeFileSync(path.join(output, names[0] + '_results.json'), '{"tests":99,"failures":0}');
    const excluded = inventory.filter(t => !names.includes(t.baseName)).map(t => t.baseName);
    const executable = path.join(fixture, names[0] + '.exe');
    if (scenario === 'missing') fs.unlinkSync(executable);
    let child;
    try {
        child = invoke(['--target=radiant', '--category=baseline', '--sequential',
            '--exclude-test=' + excluded.join(','), ...extra], {
            LAMBDA_TEST_BIN_DIR: fixture, LAMBDA_TEST_OUTPUT_DIR: output,
            LAMBDA_TEST_IDLE_TIMEOUT: '1', FAKE_TEST: names[0], FAKE_SCENARIO: scenario,
        });
    } finally {
        if (scenario === 'missing') fs.writeFileSync(executable, fake, { mode: 0o755 });
    }
    return { child, output, summary: JSON.parse(fs.readFileSync(path.join(output, 'test_summary.json'), 'utf8')) };
}

try {
    check('Radiant inventory includes every baseline phase', () => {
        const selected = new Set(inventory.map(t => t.baseName));
        for (const name of ['test_scene3d_gtest', 'test_map_gtest', 'test_view_reuse_gtest',
            'test_ui_automation_gtest', 'test_radiant_view_ui_gtest', 'test_dom_ui_gtest',
            'test_page_load_gtest', 'test_radiant_view_gtest', 'test_layout_fuzzy_gtest',
            'test_wpt_css_syntax_gtest', 'test_wpt_input_events_gtest',
            'test_radiant_layout_baseline', 'test_radiant_page_snapshot', 'test_radiant_render_baseline']) {
            assert.ok(selected.has(name), name);
        }
        assert.equal(selected.size, inventory.length);
        assert.ok(inventory.every(t => t.suite === 'radiant' && t.category === 'baseline'));
        const view = inventory.find(t => t.baseName === 'test_radiant_view_gtest');
        assert.ok(view.runnerArgs.includes('--gtest_filter=*-RadiantViewTest.Doom*'));
    });
    check('Native build targets deduplicate runner aliases', () => {
        const actual = invoke(['--target=radiant', '--category=baseline', '--list-build-targets']).stdout.trim().split(/\s+/);
        assert.deepEqual(actual, [...new Set(inventory.map(t => t.buildTarget).filter(Boolean))]);
        assert.equal(actual.filter(t => t === 'test_ui_automation_gtest').length, 1);
    });
    check('All selected suites reach the shared report', () => {
        const { child, summary } = runSelected(['test_scene3d_gtest', 'test_map_gtest']);
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
            const { child, summary } = runSelected(['test_scene3d_gtest', 'test_map_gtest'], scenario);
            assert.equal(child.status, 1, child.stderr);
            assert.equal(summary.total_failed, 1);
            assert.equal(summary.level2_c_tests.find(t => t.test === 'test_map_gtest').passed, 4);
            assert.ok(summary.failed_test_names.some(t => t.includes('test_scene3d_gtest')));
        });
    }
    check('Skipped cases are excluded from passing counts', () => {
        const { child, summary } = runSelected(['test_scene3d_gtest'], 'skip');
        assert.equal(child.status, 0);
        assert.equal(summary.total_passed, 3);
        assert.equal(summary.total_skipped, 1);
    });
    check('Parallel view subprocess timings and phases reach the shared report', () => {
        const { child, summary } = runSelected(['test_scene3d_gtest'], 'launches');
        assert.equal(child.status, 0, child.stderr);
        const result = summary.level2_c_tests[0];
        assert.equal(result.top_slow_tests[0].duration_ms, 125.5);
        assert.equal(result.launch_timings.length, 1);
        assert.equal(result.launch_timings[0].launch, 'fixture');
        assert.equal(result.launch_timings[0].duration_ms, 125.5);
        assert.deepEqual(result.launch_timings[0].phases, { load: 75.5, cleanup: 4.5 });
        assert.equal(result.launch_timings[0].log_file, 'view.log');
    });
    check('Raw output still saves the final summary', () => {
        const { child, summary } = runSelected(['test_scene3d_gtest'], 'exit', ['--raw']);
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
        for (const [index, suite] of ['baseline,dtna,timeline', 'view', 'dom'].entries()) {
            const data = JSON.parse(fs.readFileSync(path.join(output, names[index] + '_results.json'), 'utf8'));
            assert.equal(data.args[data.args.indexOf('--suite') + 1], suite);
        }
    });
    // Keep adapter tests independent of native binaries and the external corpus.
    fs.writeFileSync(path.join(fixture, 'make'), `#!/usr/bin/env node
const fs = require('fs');
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
