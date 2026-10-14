// Exercise the real CLI and both execution tiers without font/Radiant services.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const EXE = path.resolve(process.env.LAMBDA_MATHML_EXE || path.join(ROOT, 'lambda.exe'));
fs.mkdirSync(path.join(ROOT, 'temp'), { recursive: true });
const dir = fs.mkdtempSync(path.join(ROOT, 'temp', 'mathml-cli-'));
const URI = 'http://www.w3.org/1998/Math/MathML';

function run(args, tier = 'auto', extra = {}) {
  return spawnSync(EXE, ['--no-log', ...args], {
    cwd: ROOT, encoding: 'utf8', timeout: 60000,
    env: { ...process.env, LAMBDA_HOME: path.join(ROOT, 'lmd'), LAMBDA_EXEC_BACKEND: tier, ...extra },
  });
}

for (const tier of ['interp', 'jit', 'auto']) {
  test(`MathML goldens and native nested conversion on ${tier}`, () => {
    for (const [file, expected] of [
      ['test/lambda/math/test_mathml_export.ls', '[]'],
      ['test/lambda/math/test_mathml_input.ls', '[]'],
      ['test/lambda/mathml_format.ls', '[]'],
      ['test/lambda/match_raise_join.ls', '[true, true, true, true, true, true, true, true, true]'],
    ]) {
      const result = run([file], tier);
      assert.equal(result.status, 0, `${file}: ${result.stdout}\n${result.stderr}`);
      assert.equal(result.stdout.trim(), expected, file);
    }
  });
}

test('MathML conversion keeps exact roots under forced compacting GC', () => {
  const result = run(['test/lambda/mathml_format.ls'], 'auto', {
    LAMBDA_GC_FORCE_EVERY: '1', LAMBDA_GC_POISON_FREED: '1',
  });
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.equal(result.stdout.trim(), '[]');
});

for (const flavor of ['math', 'math-latex', 'math:latex', 'math-ascii', 'math:ascii']) {
  test(`CLI exports ${flavor} input as MathML`, () => {
    const source = path.join(dir, `${flavor.replaceAll(':', '-')}-input with spaces.txt`);
    const output = `${source}.mml`;
    // This checks flavor dispatch; the existing ASCII parser's fraction gap is separate.
    fs.writeFileSync(source, flavor.includes('ascii') ? 'a+b' : String.raw`\frac{a+b}{c}`);
    const result = run(['convert', source, '-f', flavor, '-t', 'math-mathml', '-o', output]);
    assert.equal(result.status, 0, result.stdout + result.stderr);
    const xml = fs.readFileSync(output, 'utf8');
    assert.ok(xml.startsWith(`<math xmlns="${URI}"`));
    if (!flavor.includes('ascii')) assert.match(xml, /<mfrac>/);
    assert.match(xml, /<mo>\+<\/mo>/);
    assert.doesNotMatch(xml, /<svg|<value>|font-face/);
  });
}

test('CLI preserves imported MathML and rejects unsupported TeX conversion', () => {
  const source = path.join(dir, 'import.mml');
  const output = `${source}.out`;
  fs.writeFileSync(source, `<math xmlns="${URI}"><mtext> a  b </mtext><mrow/></math>`);
  const success = run(['convert', source, '-f', 'math-mathml', '-t', 'math-mathml', '-o', output]);
  assert.equal(success.status, 0, success.stdout + success.stderr);
  assert.match(fs.readFileSync(output, 'utf8'), /<mtext> a  b <\/mtext>/);
  const unsupported = path.join(dir, 'unsupported.tex');
  const rejected = `${unsupported}.mml`;
  fs.writeFileSync(unsupported, String.raw`\undefinedcommand`);
  const failure = run(['convert', unsupported, '-f', 'math', '-t', 'math-mathml', '-o', rejected]);
  assert.notEqual(failure.status, 0);
  assert.equal(fs.existsSync(rejected), false);
});
