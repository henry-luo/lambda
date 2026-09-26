#!/usr/bin/env node
// Generate the math metrics loader and Mark data from MathLive's
// font-metrics-data.ts.
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
const OUT_SCRIPT = path.join(PROJECT_ROOT, 'lambda/package/math/metrics_data.ls');
const OUT_DATA = path.join(PROJECT_ROOT, 'lambda/package/math/metrics_data.mark');

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
const mainBold    = parseFontSection('Main-Bold');
const typewriter  = parseFontSection('Typewriter-Regular');
const fraktur     = parseFontSection('Fraktur-Regular');
const script      = parseFontSection('Script-Regular');
const caligraphic = parseFontSection('Caligraphic-Regular');
const sansSerif   = parseFontSection('SansSerif-Regular');

console.log(`Main-Regular: ${Object.keys(mainRegular).length} entries`);
console.log(`Math-Italic:  ${Object.keys(mathItalic).length} entries`);
console.log(`AMS-Regular:  ${Object.keys(amsRegular).length} entries`);
console.log(`Main-Bold:    ${Object.keys(mainBold).length} entries`);
console.log(`Typewriter:   ${Object.keys(typewriter).length} entries`);
console.log(`Fraktur:      ${Object.keys(fraktur).length} entries`);
console.log(`Script:       ${Object.keys(script).length} entries`);
console.log(`Caligraphic:  ${Object.keys(caligraphic).length} entries`);
console.log(`SansSerif:    ${Object.keys(sansSerif).length} entries`);

// Convert codepoint → character string for keys.
function cpToChar(cp) {
  return String.fromCodePoint(cp);
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
// while keeping the metrics data file small.
function round5(x) { return Math.round(x * 1e5) / 1e5; }

function is_renderable_codepoint(cp) {
  return cp < 0xE000 || cp > 0xF8FF;
}

function rendered_entry_count(entries) {
  return Object.keys(entries).reduce((count, key) =>
    count + (is_renderable_codepoint(Number(key)) ? 1 : 0), 0);
}

function emitMarkMap(entries) {
  const lines = [];
  lines.push('{');
  const keys = Object.keys(entries).map(Number).sort((a, b) => a - b);
  const out = [];
  for (const cp of keys) {
    // Skip Private Use Area (PUA) — these are MathLive's font-private glyphs
    // (e.g. drawn-as-SVG large delimiter parts) that Lambda doesn't render directly.
    if (!is_renderable_codepoint(cp)) continue;
    const ch = cpToChar(cp);
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
    // Raw width (5dp) — used for accent centering, where MathLive divides
    // the full-precision width by 2 (rounding to 2dp loses the half-pixel).
    const wRaw = round5(w);
    out.push(`    ${JSON.stringify(ch)}: [${rd}, ${rh}, ${rit}, ${rsk}, ${rw}, ${hRaw}, ${dRaw}, ${wRaw}]`);
  }
  lines.push(out.join(',\n'));
  lines.push(`}`);
  return lines.join('\n');
}

const dataHeader = `// math/metrics_data.mark — Per-character font metrics ported from
// MathLive's font-metrics-data.ts. Generated by
// test/lambda/mathlive/generate_metrics_data.mjs — do not edit by hand.
//
// Each entry is keyed by character (string) and has value
// [depth, height, italic, skew, width, height_exact, depth_exact, width_raw]
// in em units.
//   depth        — round-toward-baseline rounded to 2dp (used by layout math)
//   height       — CEIL@2 (matches MathLive's emit-side rounding)
//   italic       — CEIL@2 (margin-right emission)
//   skew, width  — half-up to 2dp (consumed by layout, not direct emission)
//   height_exact — precise metric for root strut emission
//   depth_exact  — precise metric for root strut emission
//   width_raw    — precise metric for accent centering

`;

const dataContent = dataHeader +
  '{\n' +
  '  main_regular: ' + emitMarkMap(mainRegular) + ',\n' +
  '  math_italic: ' + emitMarkMap(mathItalic) + ',\n' +
  '  ams_regular: ' + emitMarkMap(amsRegular) + ',\n' +
  '  main_bold: ' + emitMarkMap(mainBold) + ',\n' +
  '  typewriter: ' + emitMarkMap(typewriter) + ',\n' +
  '  fraktur: ' + emitMarkMap(fraktur) + ',\n' +
  '  script_font: ' + emitMarkMap(script) + ',\n' +
  '  caligraphic: ' + emitMarkMap(caligraphic) + ',\n' +
  '  sans_serif: ' + emitMarkMap(sansSerif) + '\n' +
  '}\n';

const scriptContent = `// math/metrics_data.ls — Font metrics accessors.
// The generated metrics are parsed from metrics_data.mark so module loading
// does not compile the static data as Lambda source.

let metric_tables = input(sys.lambda.home# ++ "/package/math/metrics_data.mark", 'mark')^

// Lookup: single-character string + font name → metrics or null
pub fn lookup(ch, font_name) {
    if (ch == null) null
    else if (font_name == "cmr" or font_name == "main") metric_tables.main_regular[ch]
    else if (font_name == "mathit" or font_name == "cmmi") metric_tables.math_italic[ch]
    else if (font_name == "ams") metric_tables.ams_regular[ch]
    else if (font_name == "mathbf" or font_name == "bold") metric_tables.main_bold[ch]
    else if (font_name == "tt") metric_tables.typewriter[ch]
    else if (font_name == "frak") metric_tables.fraktur[ch]
    else if (font_name == "script") metric_tables.script_font[ch]
    else if (font_name == "cal") metric_tables.caligraphic[ch]
    else if (font_name == "sans") metric_tables.sans_serif[ch]
    else null
}

// Convenience accessors
pub fn depth_of(metrics)  { if (metrics == null) null else metrics[0] }
pub fn height_of(metrics) { if (metrics == null) null else metrics[1] }
pub fn italic_of(metrics) { if (metrics == null) null else metrics[2] }
pub fn skew_of(metrics)   { if (metrics == null) null else metrics[3] }
pub fn width_of(metrics)  { if (metrics == null) null else metrics[4] }
// Full-precision (5dp) values — for strut emission and accent centering.
pub fn height_exact_of(metrics) { if (metrics == null) null else metrics[5] }
pub fn depth_exact_of(metrics)  { if (metrics == null) null else metrics[6] }
pub fn width_raw_of(metrics)    { if (metrics == null) null else metrics[7] }
`;

fs.writeFileSync(OUT_SCRIPT, scriptContent, 'utf8');
fs.writeFileSync(OUT_DATA, dataContent, 'utf8');
const scriptSizeKb = (scriptContent.length / 1024).toFixed(1);
const dataSizeKb = (dataContent.length / 1024).toFixed(1);
console.log(`\nWrote ${path.relative(PROJECT_ROOT, OUT_SCRIPT)} (${scriptSizeKb} KB)`);
console.log(`Wrote ${path.relative(PROJECT_ROOT, OUT_DATA)} (${dataSizeKb} KB)`);
console.log(`  ${rendered_entry_count(mainRegular) + rendered_entry_count(mathItalic) +
  rendered_entry_count(amsRegular) + rendered_entry_count(mainBold) +
  rendered_entry_count(typewriter) + rendered_entry_count(fraktur) +
  rendered_entry_count(script) + rendered_entry_count(caligraphic) +
  rendered_entry_count(sansSerif)} emitted entries`);
