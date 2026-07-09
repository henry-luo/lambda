#!/usr/bin/env node
// Rule 18 script geometry fixture.
//
// Probes Lambda box metadata directly for representative superscript,
// subscript, combined, nested, descender, fraction-child, and inline big-op
// script cases. This guards the Math5 one-box-field migration without
// depending on MathLive SSR.

import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const PROBE = path.join(__dirname, 'box_model_probe.mjs');

const CASES = [
  { id: 'sup-only', formula: 'x^2', height: 0.814108, depth: 0.0, width: 1.36 },
  { id: 'sub-only', formula: 'x_2', height: 0.43056, depth: 0.15, width: 1.36 },
  { id: 'both', formula: 'x_2^3', height: 0.864108, depth: 0.247, width: 1.36 },
  { id: 'delimiter-sup', formula: '|z|^2', height: 0.864108, depth: 0.25, width: 2.96 },
  { id: 'descender-sub', formula: 'x_y', height: 0.43056, depth: 0.286108, width: 1.36 },
  { id: 'nested-sup', formula: 'x^{y^2}', height: 0.98692, depth: 0.0, width: 1.76 },
  { id: 'fraction-sub', formula: 'a_{\\frac{1}{2}}', height: 0.43056, depth: 0.69982, width: 1.528 },
  { id: 'inline-sum-limits', formula: '\\sum_{i=1}^n', height: 1.6514, depth: 1.27767, width: 1.68 },
];

function approxEq(a, b, eps = 1e-6) {
  return Math.abs(a - b) <= eps;
}

function probe(testCase) {
  const scriptPath = path.join(PROJECT_ROOT, 'temp', `script_fixture_${testCase.id}.ls`);
  const result = spawnSync(
    process.execPath,
    [PROBE, '--inline', '--script', scriptPath, testCase.formula],
    { cwd: PROJECT_ROOT, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 }
  );
  if (result.status !== 0) {
    throw new Error(result.stderr || result.stdout || `probe failed for ${testCase.id}`);
  }
  return JSON.parse(result.stdout);
}

let passed = 0;
const failures = [];

for (const testCase of CASES) {
  process.stdout.write(`  ${testCase.id.padEnd(14)} ${testCase.formula.padEnd(20)} `);
  try {
    const actual = probe(testCase);
    const ok =
      actual.error === 'no-error' &&
      actual.one_box_fields === true &&
      actual.has_height_raw === false &&
      actual.has_depth_raw === false &&
      actual.has_render_height === false &&
      actual.has_render_depth === false &&
      actual.has_render_total === false &&
      actual.has_left_right_render_depth === false &&
      actual.has_left_right_render_total === false &&
      approxEq(actual.height, testCase.height) &&
      approxEq(actual.depth, testCase.depth) &&
      approxEq(actual.width, testCase.width);

    if (ok) {
      passed += 1;
      process.stdout.write('PASS\n');
    } else {
      failures.push({ testCase, actual });
      process.stdout.write('FAIL\n');
    }
  } catch (err) {
    failures.push({ testCase, error: err.message });
    process.stdout.write('ERROR\n');
  }
}

console.log(`\n${passed}/${CASES.length} Rule 18 script geometry cases pass.`);

if (failures.length > 0) {
  for (const failure of failures) {
    console.log(`\n[${failure.testCase.id}] ${failure.testCase.formula}`);
    if (failure.error) console.log(failure.error);
    else console.log(JSON.stringify(failure.actual, null, 2));
  }
  process.exit(1);
}
