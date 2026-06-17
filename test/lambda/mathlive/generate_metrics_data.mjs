#!/usr/bin/env node
// Generate lambda/package/math/metrics_data.ls from MathLive's font-metrics-data.ts.
// Per-character metrics [depth, height, italic, skew, width] for:
//   - Main-Regular (cmr)    — upright Roman / non-italic text
//   - Math-Italic (cmmi)    — italic math letters
//   - AMS-Regular           — AMS extended symbols

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '../../..');
const SRC = path.join(PROJECT_ROOT, 'ref/mathlive/src/core/font-metrics-data.ts');
const OUT = path.join(PROJECT_ROOT, 'lambda/package/math/metrics_data.ls');

const text = fs.readFileSync(SRC, 'utf8');

// Collect M-constants.
const constRe = /^const (M\d+)\s*=\s*\[([^\]]+)\];/gm;
const constants = {};
let m;
while ((m = constRe.exec(text)) !== null) {
  constants[m[1]] = m[2].split(',').map(s => parseFloat(s.trim()));
}

function parseFontSection(fontName) {
  const headerRe = new RegExp(`^\\s*'${fontName}':\\s*\\{`, 'm');
  const headerMatch = text.match(headerRe);
  if (!headerMatch) throw new Error(`font section not found: ${fontName}`);
  const start = headerMatch.index + headerMatch[0].length;
  let i = start, depth = 1;
  while (i < text.length && depth > 0) {
    if (text[i] === '{') depth++;
    else if (text[i] === '}') depth--;
    if (depth === 0) break;
    i++;
  }
  const body = text.slice(start, i);
  const entries = {};
  const lineRe = /^\s*(\d+):\s*(M\d+|\[[-0-9.,\s]+\]),/gm;
  let lm;
  while ((lm = lineRe.exec(body)) !== null) {
    const codepoint = parseInt(lm[1], 10);
    const expr = lm[2];
    let values;
    if (expr.startsWith('M')) {
      values = constants[expr];
      if (!values) continue;
    } else {
      values = expr.slice(1, -1).split(',').map(s => parseFloat(s.trim()));
    }
    if (values.length !== 5) continue;
    entries[codepoint] = values;
  }
  return entries;
}

const mainRegular = parseFontSection('Main-Regular');
const mathItalic  = parseFontSection('Math-Italic');
const amsRegular  = parseFontSection('AMS-Regular');

console.log(`Main-Regular: ${Object.keys(mainRegular).length} entries`);
console.log(`Math-Italic:  ${Object.keys(mathItalic).length} entries`);
console.log(`AMS-Regular:  ${Object.keys(amsRegular).length} entries`);

// Convert codepoint → character string for keys.
function cpToChar(cp) {
  return String.fromCodePoint(cp);
}

// Escape a character for Lambda string literal.
function escapeCharForKey(ch) {
  if (ch === "'") return "\\'";
  if (ch === "\\") return "\\\\";
  return ch;
}

// Pre-rounded to 2 decimals matching MathLive's emission rules:
//   - height (always positive): CEIL to 2 decimals
//   - depth (positive = descender, below baseline): FLOOR to 2 decimals
//   - depth (negative = above-baseline like →): CEIL of magnitude, negated
//   - italic: CEIL to 2 decimals (MathLive emits italic correction as
//     margin-right via toString() which CEILs at 2 decimals)
//   - skew, width: half-up to 2 decimals (consumed only for layout math,
//     not for direct emission)
function roundCeil2(x) { return Math.ceil(x * 100) / 100; }
function roundHalfUp2(x) { return Math.round(x * 100) / 100; }
function roundFloor2(x) { return Math.floor(x * 100) / 100; }
// Depth rounding is asymmetric: for descenders (positive d) FLOOR pairs
// well with the h+d CEIL@2 emission. For negative depths (glyphs extending
// above baseline like arrows), we keep the CEIL-of-magnitude rule because
// the `-d` vertical-align matters more visually than the h+d strut. A
// future full-precision propagation would resolve both at the emit site.
function roundDepth(d) {
  if (d >= 0) return roundFloor2(d);
  return -roundCeil2(-d);
}
function roundItalic(it) {
  // Italic correction is emitted directly as margin-right via CEIL@2
  return roundCeil2(it);
}

// Round to 5 decimals for storage as the "raw" full-precision value the
// strut emission needs. 5 decimals preserves enough fidelity that CEIL@2
// of any sum gives the same result as CEIL@2 of the floating-point sum,
// while keeping the metrics_data file small.
function round5(x) { return Math.round(x * 1e5) / 1e5; }

function emitMap(name, entries) {
  const lines = [];
  lines.push(`let ${name} = {`);
  const keys = Object.keys(entries).map(Number).sort((a, b) => a - b);
  const out = [];
  for (const cp of keys) {
    // Skip Private Use Area (PUA) — these are MathLive's font-private glyphs
    // (e.g. drawn-as-SVG large delimiter parts) that Lambda doesn't render directly.
    if (cp >= 0xE000 && cp <= 0xF8FF) continue;
    const ch = cpToChar(cp);
    const escaped = escapeCharForKey(ch);
    const [d, h, it, sk, w] = entries[cp];
    const rd = roundDepth(d);
    const rh = roundCeil2(h);
    const rit = roundItalic(it);
    const rsk = roundHalfUp2(sk);
    const rw = roundHalfUp2(w);
    // Raw (un-rounded-to-2) height/depth for strut emission. Keep at 5
    // decimals to match MathLive's font-metrics-data.ts precision.
    const hRaw = round5(h);
    const dRaw = round5(d);
    out.push(`    '${escaped}': [${rd}, ${rh}, ${rit}, ${rsk}, ${rw}, ${hRaw}, ${dRaw}]`);
  }
  lines.push(out.join(',\n'));
  lines.push(`}`);
  return lines.join('\n');
}

const header = `// math/metrics_data.ls — Per-character font metrics ported from
// MathLive's font-metrics-data.ts. Generated by
// test/lambda/mathlive/generate_metrics_data.mjs — do not edit by hand.
//
// Each entry is keyed by character (string) and has value
// [depth, height, italic, skew, width, height_raw, depth_raw] in em units.
//   depth      — round-toward-baseline rounded to 2dp (used by layout math)
//   height     — CEIL@2 (matches MathLive's emit-side rounding)
//   italic     — CEIL@2 (margin-right emission)
//   skew, width— half-up to 2dp (consumed by layout, not direct emission)
//   height_raw — 5dp (used by strut emission: max(h_raw) → CEIL@2 at emit)
//   depth_raw  — 5dp (used by strut emission: max(d_raw) → CEIL@2 at emit)

`;

const content = header +
  '// ============================================================\n' +
  '// Main-Regular (cmr) — upright Roman / non-italic\n' +
  '// ============================================================\n' +
  emitMap('main_regular', mainRegular) + '\n\n' +
  '// ============================================================\n' +
  '// Math-Italic (cmmi) — italic math letters\n' +
  '// ============================================================\n' +
  emitMap('math_italic', mathItalic) + '\n\n' +
  '// ============================================================\n' +
  '// AMS-Regular — AMS extended symbols\n' +
  '// ============================================================\n' +
  emitMap('ams_regular', amsRegular) + '\n\n' +
  '// ============================================================\n' +
  '// Lookup: single-character string + font name → metrics or null\n' +
  '// font_name: "cmr" / "main", "mathit" / "cmmi", "ams"\n' +
  '// ============================================================\n' +
  '\n' +
  'pub fn lookup(ch, font_name) {\n' +
  '    if (ch == null) null\n' +
  '    else if (font_name == "cmr" or font_name == "main") main_regular[ch]\n' +
  '    else if (font_name == "mathit" or font_name == "cmmi") math_italic[ch]\n' +
  '    else if (font_name == "ams") ams_regular[ch]\n' +
  '    else null\n' +
  '}\n' +
  '\n' +
  '// Convenience accessors\n' +
  'pub fn depth_of(metrics)  { if (metrics == null) null else metrics[0] }\n' +
  'pub fn height_of(metrics) { if (metrics == null) null else metrics[1] }\n' +
  'pub fn italic_of(metrics) { if (metrics == null) null else metrics[2] }\n' +
  'pub fn skew_of(metrics)   { if (metrics == null) null else metrics[3] }\n' +
  'pub fn width_of(metrics)  { if (metrics == null) null else metrics[4] }\n' +
  '// Full-precision (5dp) values — for strut emission only.\n' +
  'pub fn height_raw_of(metrics) { if (metrics == null) null else metrics[5] }\n' +
  'pub fn depth_raw_of(metrics)  { if (metrics == null) null else metrics[6] }\n';

fs.writeFileSync(OUT, content, 'utf8');
const sizeKb = (content.length / 1024).toFixed(1);
console.log(`\nWrote ${path.relative(PROJECT_ROOT, OUT)} (${sizeKb} KB)`);
console.log(`  ${Object.keys(mainRegular).length + Object.keys(mathItalic).length + Object.keys(amsRegular).length} total entries`);
