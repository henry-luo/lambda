// Exercise the runtime and CLI adapters on full and runtime-only hosts.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const EXE = path.resolve(process.env.LAMBDA_MATH_FORMAT_EXE || path.join(ROOT, 'lambda.exe'));
const dir = fs.mkdtempSync(path.join(ROOT, 'temp', 'math-source-format-'));
function run(args, tier = 'auto', extra = {}) {
  return spawnSync(EXE, ['--no-log', ...args], {
    cwd: ROOT, encoding: 'utf8', timeout: 60000,
    env: { ...process.env, LAMBDA_HOME: path.join(ROOT, 'lmd'), LAMBDA_EXEC_BACKEND: tier, ...extra },
  });
}
for (const tier of ['interp', 'jit', 'auto']) {
  test(`math source formats agree with compatibility goldens on ${tier}`, () => {
    for (const file of [
      'test/lambda/math_source_format.ls',
      'test/lambda/math/test_math_source_format.ls',
      'test/lambda/math/test_math_format_package.ls',
      'test/lambda/math/test_math_token_arrays.ls',
      'test/lambda/mathml_format.ls',
    ]) {
      const result = run([file], tier);
      assert.equal(result.status, 0, `${file}: ${result.stdout}\n${result.stderr}`);
      assert.equal(result.stdout.trim(), fs.readFileSync(path.join(ROOT, file.replace(/\.ls$/, '.txt')), 'utf8').trim(), file);
    }
  });
}
test('math source formatting preserves roots during forced compacting GC', () => {
  const result = run(['test/lambda/math_source_format.ls'], 'auto', {
    LAMBDA_GC_FORCE_EVERY: '1', LAMBDA_GC_POISON_FREED: '1',
  });
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.equal(result.stdout.trim(), '[]');
});
for (const [flavor, source, target, expected] of [
  ['math', String.raw`\frac{a+b}{c}`, 'math-latex', String.raw`\frac{a + b}{c}`],
  ['math-latex', String.raw`\varepsilon+\leq+\infty`, 'math-ascii', 'epsilon + <= + oo'],
  ['math-ascii', 'sin(x)', 'math-ascii', 'sin ( x )'],
  ['math:ascii', 'x^2', 'math-latex', 'x^2'],
]) {
  test(`CLI converts ${flavor} to ${target}`, () => {
    const input = path.join(dir, `${flavor.replace(':', '-')}-${target} with spaces.txt`);
    const output = `${input}.out`;
    fs.writeFileSync(input, source);
    const result = run(['convert', input, '-f', flavor, '-t', target, '-o', output]);
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.equal(fs.readFileSync(output, 'utf8'), expected);
  });
}
test('procedural output uses the package route', () => {
  const output = path.join(dir, 'output with spaces.txt');
  const script = path.join(dir, '_output.ls');
  fs.writeFileSync(script, `pn main() {\n let ast = parse("x^2",'math')^\n output(ast,${JSON.stringify(output)},"math-ascii")^\n}\n`);
  const result = run(['run', script]);
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.equal(fs.readFileSync(output, 'utf8'), 'x^2');
});
test('unsupported Typst math output leaves the destination intact', () => {
  const input = path.join(dir, 'unsupported.txt');
  const output = `${input}.out`;
  fs.writeFileSync(input, 'x');
  fs.writeFileSync(output, 'existing content');
  const result = run(['convert', input, '-f', 'math', '-t', 'math-typst', '-o', output]);
  assert.notEqual(result.status, 0);
  assert.equal(fs.readFileSync(output, 'utf8'), 'existing content');
});
