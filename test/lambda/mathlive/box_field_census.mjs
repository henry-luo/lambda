#!/usr/bin/env node
// Count legacy math box vertical side-channel fields.
//
// This is a Phase A migration aid: it identifies where render_*,
// height_raw/depth_raw, and left_right_render_* are still produced or read.
// It intentionally scans source text rather than executing Lambda code, so it
// can be used before/after each incremental producer conversion.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const DEFAULT_ROOT = path.join(PROJECT_ROOT, 'lambda/package/math');

const FIELDS = [
  'render_height',
  'render_depth',
  'render_total',
  'height_raw',
  'depth_raw',
  'left_right_render_depth',
  'left_right_render_total',
];

function parseArgs(argv) {
  const opts = {
    root: DEFAULT_ROOT,
    json: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--json') opts.json = true;
    else if (arg === '--root') opts.root = path.resolve(argv[++i] ?? '');
    else if (arg === '--help' || arg === '-h') {
      printHelp();
      process.exit(0);
    } else {
      throw new Error(`unknown argument: ${arg}`);
    }
  }
  return opts;
}

function printHelp() {
  console.log(`Usage: node test/lambda/mathlive/box_field_census.mjs [options]

Options:
  --root DIR    Source root to scan, default lambda/package/math
  --json        Emit JSON instead of a text report
`);
}

function listLsFiles(root) {
  const out = [];
  walk(root, out);
  return out.sort();
}

function walk(dir, out) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(p, out);
    else if (entry.isFile() && entry.name.endsWith('.ls')) out.push(p);
  }
}

function classifyLine(line, field) {
  const escaped = field.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  if (new RegExp(`\\b${escaped}\\s*:`).test(line)) return 'producer';
  return 'reader';
}

function scanFile(file) {
  const rel = path.relative(PROJECT_ROOT, file);
  const text = fs.readFileSync(file, 'utf8');
  const lines = text.split(/\r?\n/);
  const hits = [];
  for (let i = 0; i < lines.length; i += 1) {
    const line = lines[i];
    for (const field of FIELDS) {
      const re = new RegExp(`\\b${field}\\b`);
      if (re.test(line)) {
        hits.push({
          file: rel,
          line: i + 1,
          field,
          kind: classifyLine(line, field),
          text: line.trim(),
        });
      }
    }
  }
  return hits;
}

function buildSummary(hits) {
  const summary = {};
  for (const field of FIELDS) {
    summary[field] = { producer: 0, reader: 0, total: 0, files: {} };
  }
  for (const hit of hits) {
    const bucket = summary[hit.field];
    bucket[hit.kind] += 1;
    bucket.total += 1;
    bucket.files[hit.file] = (bucket.files[hit.file] ?? 0) + 1;
  }
  return summary;
}

function printText(summary, hits) {
  console.log('Lambda math box-field census');
  console.log(`  hits: ${hits.length}`);
  console.log('');
  for (const field of FIELDS) {
    const s = summary[field];
    console.log(
      `${field.padEnd(24)} producers=${String(s.producer).padStart(3)} ` +
        `readers=${String(s.reader).padStart(3)} total=${String(s.total).padStart(3)}`
    );
    const files = Object.entries(s.files).sort((a, b) => b[1] - a[1]);
    for (const [file, n] of files.slice(0, 5)) {
      console.log(`  ${String(n).padStart(3)} ${file}`);
    }
  }
}

function main() {
  const opts = parseArgs(process.argv.slice(2));
  const hits = listLsFiles(opts.root).flatMap(scanFile);
  const summary = buildSummary(hits);
  if (opts.json) {
    console.log(JSON.stringify({ summary, hits }, null, 2));
  } else {
    printText(summary, hits);
  }
}

main();
