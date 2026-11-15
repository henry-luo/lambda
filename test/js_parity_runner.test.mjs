import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { SUITES, TARGETED_SUITES, parseArgs, parseGtestJson, divergence } from './run_js_parity.mjs';

test('focused parity selects nine logical contracts in all three modes', () => {
  const opts = parseArgs(['--suite=targeted']);
  assert.deepEqual(opts.suites, Object.keys(TARGETED_SUITES));
  assert.equal(opts.suites.reduce((count, key) => count + SUITES[key].cases.length, 0), 9);
  for (const key of opts.suites) assert.deepEqual(SUITES[key].modes, ['mir', 'ast', 'auto']);
  assert.deepEqual(SUITES.js.modes, ['mir', 'ast']);
  assert.deepEqual(SUITES.test262.modes, ['mir', 'ast']);
  assert.deepEqual(parseArgs(['--suite=targeted', '--mode=auto']).modes, ['auto']);
});

test('a stale filter or skipped contract cannot produce a passing focused leg', () => {
  fs.mkdirSync(path.resolve('temp'), { recursive: true });
  const directory = fs.mkdtempSync(path.resolve('temp/js-parity-contract-'));
  const file = path.join(directory, 'gtest.json');
  try {
    fs.writeFileSync(file, JSON.stringify({ testsuites: [{ name: 'Tiers/HotwireHttpTest', testsuite: [
      { name: 'ClientAndServer/mir', failures: [{ failure: 'deadline exceeded' }] },
      { name: 'ClientAndServer/ast', result: 'SKIPPED' },
    ] }] }));
    assert.deepEqual(parseGtestJson(file, 1), {
      total: 1, failures: ['Tiers/HotwireHttpTest.ClientAndServer'],
    });
    assert.throws(() => parseGtestJson(file, 2), /expected 2 selected tests, got 1/);
    fs.writeFileSync(file, JSON.stringify({ testsuites: [] }));
    assert.throws(() => parseGtestJson(file, 1), /expected 1 selected tests, got 0/);
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});

test('three-mode divergence retains failures shared by two modes', () => {
  const suite = SUITES.runtime.label;
  const legs = [
    { suite, mode: 'ast', failures: ['both-native', 'everywhere'] },
    { suite, mode: 'mir', failures: ['both-native', 'everywhere'] },
    { suite, mode: 'auto', failures: ['auto-only', 'everywhere'] },
  ];
  const [result] = divergence(legs);
  assert.deepEqual(result.only, { ast: [], mir: [], auto: ['auto-only'] });
  assert.deepEqual(result.shared, ['everywhere']);
  assert.deepEqual(result.divergent, ['both-native', 'auto-only']);
});
