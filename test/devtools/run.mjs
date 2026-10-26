import { run } from 'node:test';
import { spec } from 'node:test/reporters';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// Preserve the native Node verdicts in the repository runner's report format.
const reportIndex = process.argv.indexOf('--report');
const report = reportIndex < 0 ? null : process.argv[reportIndex + 1];
if (reportIndex >= 0 && !report) throw new Error('Expected --report <path>');
const results = [];
const tests = run({ files: [fileURLToPath(new URL('./protocol.test.mjs', import.meta.url))] });
for (const [event, pass] of [['test:pass', true], ['test:fail', false]]) {
  tests.on(event, data => {
    if (data.nesting === 0) results.push({ name: data.name, pass });
  });
}
for await (const output of tests.compose(spec)) process.stdout.write(output);
const passed = results.filter(result => result.pass).length;
const summary = { passed, failed: results.length - passed, total: results.length };
if (!summary.total) throw new Error('DevTools runner returned no test verdicts');
if (report) {
  await fs.mkdir(path.dirname(path.resolve(report)), { recursive: true });
  await fs.writeFile(report, JSON.stringify({ summary, results }, null, 2) + '\n');
}
process.exitCode = summary.failed ? 1 : 0;
