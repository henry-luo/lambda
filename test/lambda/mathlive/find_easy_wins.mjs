#!/usr/bin/env node
// Find failing cases with the fewest findings — these are the closest to
// passing and the highest-yield to fix. Streams each failing case through
// the diff harness logic and ranks by total finding count.

import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '../../..');

const REPORT = path.join(PROJECT_ROOT, 'temp/lambda_mathlive_markup_report.json');
const HARNESS = path.join(__dirname, 'diff_harness.mjs');

const args = process.argv.slice(2);
const limit = args.includes('--limit')
  ? Number(args[args.indexOf('--limit') + 1])
  : 30;
const minFindings = args.includes('--min-findings')
  ? Number(args[args.indexOf('--min-findings') + 1])
  : 1;
const maxFindings = args.includes('--max-findings')
  ? Number(args[args.indexOf('--max-findings') + 1])
  : 3;

const report = JSON.parse(fs.readFileSync(REPORT, 'utf8'));
const failing = (report.results || []).filter((r) => !r.pass);

const ranked = [];
let i = 0;
for (const r of failing) {
  i += 1;
  process.stderr.write(`\r${i}/${failing.length}...`);
  try {
    const result = spawnSync(
      'node',
      [HARNESS, '--json', r.display ? '--display' : null, r.formula].filter(Boolean),
      { cwd: PROJECT_ROOT, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 }
    );
    if (result.status !== 0 && result.status !== 1) continue;
    const parsed = JSON.parse(result.stdout || '{}');
    const total = parsed.findings ? parsed.findings.length : 999;
    if (total < minFindings || total > maxFindings) continue;
    ranked.push({
      key: r.key,
      formula: r.formula,
      display: r.display,
      total,
      structural: parsed.classified?.structural ?? 0,
      em: parsed.classified?.em ?? 0,
      style: parsed.classified?.style ?? 0,
      text: parsed.classified?.text ?? 0,
      class: parsed.classified?.class ?? 0,
    });
  } catch (err) {
    // skip
  }
}

process.stderr.write('\n');
ranked.sort((a, b) => a.total - b.total);
for (const r of ranked.slice(0, limit)) {
  console.log(
    `[${r.total} findings  s=${r.structural} c=${r.class} e=${r.em} st=${r.style} t=${r.text}] ${r.key}`
  );
  console.log(`    ${r.formula}`);
}
console.log(`\n${ranked.length} cases with ${minFindings}-${maxFindings} findings.`);
