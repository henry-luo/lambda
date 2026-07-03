// Stage 4C Milestone 3 — parity report.
//
// Cross-checks the LambdaJS/Radiant Phase-A pass-set (run under `lambda.exe js`
// via run-phase-a.mjs) against the jsdom/vitest oracle (the editor's own suite
// run under Node). The acceptance criterion (Radiant_Editor_Stage4C.md §10) is:
// "A parity report cross-checks Radiant vs. jsdom pass sets; matches, or each
// difference is a recorded, intentional distinction."
//
// The only intentional distinction is the React `*.test.tsx` files (§1.2), which
// Phase A excludes by construction (esbuild globs only `*.test.ts`). Everything
// else must match group-for-group.
//
// Reconciliation is at the run-phase-a GROUP level (core / view / drawing /
// tier_*): the oracle's per-file test counts are summed into the same groups and
// compared to Radiant's per-group pass counts. This is robust (no fragile
// per-test-name normalization across two runners) yet still localizes any
// divergence to a group, and — because Phase A runs at 0 failures — a matching
// per-group count means Radiant ran and passed exactly the oracle's tests.
//
// Usage (from test/editor-js):
//   node tools/parity-report.mjs                 # reuse cached oracle if present
//   node tools/parity-report.mjs --refresh-oracle # re-run vitest first
//   node tools/parity-report.mjs --no-run        # reuse a cached phase-a log
//
// Writes vibe/editing/Stage4C_Parity_Report.md and prints a summary.
// Exit 0 iff every non-React group reconciles and Phase A had 0 failures.

import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')   // test/editor-js
const repoRoot = path.resolve(root, '../..')
const oracleJson = path.join(repoRoot, 'temp/4c-spikes/vitest-oracle.json')
const phaseALog = path.join(repoRoot, 'temp/4c-spikes/phase-a.log')
const reportOut = path.join(repoRoot, 'vibe/editing/Stage4C_Parity_Report.md')

const args = process.argv.slice(2)
const refreshOracle = args.includes('--refresh-oracle')
const noRun = args.includes('--no-run')

// ── Map an oracle test-file (repo-relative under editor-js) to a Phase-A group.
// Mirrors the grouping in run-phase-a.mjs exactly. `.test.tsx` → excluded.
function groupOf(rel) {
  if (rel.endsWith('.tsx')) return 'EXCLUDED_react'
  if (/^test\/(commands|model|input|helpers)\//.test(rel) || rel === 'test/smoke.test.ts') return 'core'
  if (rel.startsWith('test/view/')) return 'view'
  if (rel.startsWith('test/drawing/')) return 'drawing'
  const m = rel.match(/^test\/(tier_[a-z0-9_]+)\//)
  if (m) return m[1]
  return 'UNMAPPED:' + rel
}

// ── 1. Oracle: run vitest (jsdom) with the JSON reporter, or reuse the cache.
function ensureOracle() {
  if (fs.existsSync(oracleJson) && !refreshOracle) return
  console.log('[parity] running vitest oracle (jsdom)…')
  execFileSync('npx', ['vitest', 'run', '--reporter=json', '--outputFile=' + oracleJson],
    { cwd: root, encoding: 'utf8', stdio: 'inherit' })
}

function loadOracle() {
  const d = JSON.parse(fs.readFileSync(oracleJson, 'utf8'))
  const groups = {}          // group → { pass, total, files:[{rel,n,failed}] }
  const excluded = []        // react files
  let totalPass = 0, totalFail = 0
  for (const f of d.testResults) {
    const rel = f.name.split('/editor-js/')[1] || f.name
    const results = f.assertionResults || []
    const passed = results.filter(r => r.status === 'passed').length
    const failed = results.filter(r => r.status === 'failed').length
    totalPass += passed; totalFail += failed
    const g = groupOf(rel)
    if (g === 'EXCLUDED_react') { excluded.push({ rel, n: results.length }); continue }
    ;(groups[g] ??= { pass: 0, total: 0, files: [] })
    groups[g].pass += passed
    groups[g].total += results.length
    groups[g].files.push({ rel, n: results.length, failed })
  }
  return { groups, excluded, totalPass, totalFail, numTotalTests: d.numTotalTests, ok: d.success }
}

// ── 2. Radiant: run Phase A under lambda.exe js and parse per-group lines.
function runPhaseA() {
  let out
  if (noRun && fs.existsSync(phaseALog)) {
    out = fs.readFileSync(phaseALog, 'utf8')
  } else {
    console.log('[parity] running Phase A under lambda.exe js…')
    try {
      out = execFileSync('node', ['tools/run-phase-a.mjs'], { cwd: root, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 })
    } catch (e) {
      out = (e.stdout || '') + (e.stderr || '')   // non-zero exit still carries the group lines
    }
    fs.writeFileSync(phaseALog, out)
  }
  const groups = {}
  // e.g. "  ok    tier_e_html      pass=520 fail=0 skip=0"
  for (const m of out.matchAll(/^\s+(ok|FAIL|CRASH|BUILD|skip)\s+(\S+)\s+pass=(\d+)\s+fail=(\d+)\s+skip=(\d+)/gm)) {
    groups[m[2]] = { tag: m[1], pass: +m[3], fail: +m[4], skip: +m[5] }
  }
  const t = out.match(/PHASE-A TOTAL pass=(\d+)\s+fail=(\d+)\s+skip=(\d+)/)
  const total = t ? { pass: +t[1], fail: +t[2], skip: +t[3] } : null
  // placeholder tiers report "skip <name> (no run.test.ts …)" with no counts
  const placeholders = [...out.matchAll(/^\s+skip\s+(\S+)\s+\(no run\.test\.ts/gm)].map(m => m[1])
  return { groups, total, placeholders, raw: out }
}

// ── 3. Reconcile and emit.
ensureOracle()
const oracle = loadOracle()
const radiant = runPhaseA()

const groupNames = [...new Set([...Object.keys(oracle.groups), ...Object.keys(radiant.groups)])].sort()
const rows = []
let divergences = 0
for (const g of groupNames) {
  const o = oracle.groups[g]
  const r = radiant.groups[g]
  const oPass = o ? o.pass : 0
  const rPass = r ? r.pass : 0
  const rFail = r ? r.fail : 0
  let status
  if (!o) { status = '⚠️ radiant-only'; divergences++ }
  else if (!r) {
    // an oracle group with no Radiant run: OK only if it is a known placeholder
    if (radiant.placeholders.includes(g)) status = 'ⓘ placeholder (no corpus)'
    else { status = '❌ not run by Radiant'; divergences++ }
  }
  else if (rFail > 0) { status = `❌ ${rFail} failed`; divergences++ }
  else if (oPass === rPass) { status = '✅ match' }
  else { status = `❌ Δ${rPass - oPass}`; divergences++ }
  rows.push({ g, oPass, rPass, rFail, status })
}

// unmapped oracle files (mapping gap) are hard divergences
const unmapped = Object.keys(oracle.groups).filter(k => k.startsWith('UNMAPPED:'))
divergences += unmapped.length

const excludedTotal = oracle.excluded.reduce((a, f) => a + f.n, 0)
const parity = divergences === 0 &&
  radiant.total && radiant.total.fail === 0 &&
  (radiant.total.pass + excludedTotal === oracle.numTotalTests)

// ── Markdown report
const L = []
L.push('# Stage 4C — Phase A ↔ vitest/jsdom Parity Report')
L.push('')
L.push(`**Verdict:** ${parity ? '✅ **PARITY** — Radiant runs & passes exactly the oracle\'s non-React suite.' : '❌ **DIVERGENCE** — see below.'}`)
L.push('')
L.push('Generated by `test/editor-js/tools/parity-report.mjs`. Oracle = the editor\'s own vitest suite under jsdom (Node); Radiant = the same `*.test.ts` files bundled and run under `lambda.exe js` (Phase A, `run-phase-a.mjs`). React `*.test.tsx` files are excluded from Phase A by construction (§1.2) and are the only intended distinction.')
L.push('')
L.push('## Totals')
L.push('')
L.push('| | Tests |')
L.push('|---|---:|')
L.push(`| Oracle (vitest/jsdom) total | ${oracle.numTotalTests} |`)
L.push(`| — React \`*.test.tsx\` (excluded, intentional) | ${excludedTotal} |`)
L.push(`| — Non-React \`*.test.ts\` (Radiant scope) | ${oracle.numTotalTests - excludedTotal} |`)
L.push(`| Radiant Phase-A pass (\`lambda.exe js\`) | ${radiant.total ? radiant.total.pass : '?'} |`)
L.push(`| Radiant Phase-A fail | ${radiant.total ? radiant.total.fail : '?'} |`)
L.push('')
L.push(`Reconciliation: Radiant pass (${radiant.total ? radiant.total.pass : '?'}) + excluded React (${excludedTotal}) = ${radiant.total ? radiant.total.pass + excludedTotal : '?'} vs oracle total ${oracle.numTotalTests} → ${radiant.total && radiant.total.pass + excludedTotal === oracle.numTotalTests ? 'exact' : 'MISMATCH'}.`)
L.push('')
L.push('## Per-group reconciliation')
L.push('')
L.push('| Group | Oracle pass | Radiant pass | Status |')
L.push('|---|---:|---:|---|')
for (const r of rows) L.push(`| \`${r.g}\` | ${r.oPass} | ${r.rPass} | ${r.status} |`)
L.push('')
L.push('## Intentional exclusions — React `*.test.tsx` (§1.2)')
L.push('')
L.push('| File | Tests |')
L.push('|---|---:|')
for (const f of oracle.excluded.sort((a, b) => a.rel.localeCompare(b.rel))) L.push(`| \`${f.rel}\` | ${f.n} |`)
L.push(`| **Total excluded** | **${excludedTotal}** |`)
if (unmapped.length) {
  L.push('')
  L.push('## ⚠️ Unmapped oracle files (grouping gap — fix `groupOf`)')
  for (const u of unmapped) L.push(`- \`${u.replace('UNMAPPED:', '')}\``)
}
L.push('')
L.push('---')
L.push('*Regenerate: `make editor-4c-parity` (or `node test/editor-js/tools/parity-report.mjs --refresh-oracle`).*')

fs.writeFileSync(reportOut, L.join('\n') + '\n')

// ── Console summary
console.log('')
console.log('Stage 4C parity ' + (parity ? 'PASS ✅' : 'FAIL ❌'))
for (const r of rows) console.log(`  ${r.status.padEnd(24)} ${r.g.padEnd(20)} oracle=${r.oPass} radiant=${r.rPass}`)
console.log(`  oracle total=${oracle.numTotalTests}  react-excluded=${excludedTotal}  radiant=${radiant.total ? radiant.total.pass : '?'}/${radiant.total ? radiant.total.fail : '?'}`)
console.log(`  report → ${path.relative(repoRoot, reportOut)}`)
process.exit(parity ? 0 : 1)
