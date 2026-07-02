// Stage 4C Phase A runner — build every plain-DOM test group to an IIFE bundle
// and run it headless under `lambda.exe js`, aggregating the harness summary.
//
// Groups:
//   core     — commands/ model/ input/ helpers/ smoke  (one conformance bundle)
//   view     — view/*.test.ts                           (needs --document)
//   drawing  — drawing/*.test.ts                        (Stage-5 scope, but green)
//   tier_*   — one data-driven bundle per tier corpus   (needs --document)
//
// React `.test.tsx` files are excluded by construction (only *.test.ts globbed).
//
// Usage (from test/editor-js):
//   node tools/run-phase-a.mjs                # all groups
//   node tools/run-phase-a.mjs core view      # only the named groups
//
// Emits one line per group and a final "PHASE-A TOTAL pass=… fail=… skip=…".
// Exits non-zero if any group fails to build/run or reports fail>0.

import { build } from 'esbuild'
import { execFileSync } from 'node:child_process'
import { globSync } from 'node:fs'
import path from 'node:path'
import fs from 'node:fs'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')  // test/editor-js
const repoRoot = path.resolve(root, '../..')
const lambda = path.join(repoRoot, 'lambda.exe')
const outDir = path.join(repoRoot, 'temp/4c-spikes')
const page = path.join(repoRoot, 'temp/4c-spikes/page.html')
const domPage = path.join(repoRoot, 'temp/4c-spikes/dom_page.html')
fs.mkdirSync(outDir, { recursive: true })

const shim = path.join(root, 'harness/inengine.ts')

// Build a conformance bundle (test files → IIFE, vitest aliased to the shim).
async function buildConformance(files, outFile) {
  const entry = [
    ...files.map(f => `import './${f.replace(/\\/g, '/')}'`),
    `import { __harnessRun } from './harness/inengine.ts'`,
    `__harnessRun()`
  ].join('\n')
  await build({
    stdin: { contents: entry, resolveDir: root, sourcefile: '_phaseA-entry.ts', loader: 'ts' },
    bundle: true, format: 'iife', platform: 'neutral', outfile: outFile,
    alias: { vitest: shim }, logLevel: 'silent'
  })
}

// Run a bundle under lambda.exe js and parse the harness summary.
function runBundle(bundle, useDocument, docPage) {
  const args = ['js', bundle]
  if (useDocument) args.push('--document', docPage || page)
  args.push('--no-log')
  let out = ''
  try {
    out = execFileSync(lambda, args, { cwd: repoRoot, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 })
  } catch (e) {
    // A crash (non-zero exit) still may have printed a summary before dying.
    out = (e.stdout || '') + (e.stderr || '')
    const m = out.match(/HARNESS pass=(\d+) fail=(\d+) skip=(\d+)/)
    if (!m) return { pass: 0, fail: 0, skip: 0, crashed: true, out }
  }
  const m = out.match(/HARNESS pass=(\d+) fail=(\d+) skip=(\d+)/)
  if (!m) return { pass: 0, fail: 0, skip: 0, crashed: true, out }
  const fails = [...out.matchAll(/^FAIL (.+)$/gm)].map(x => x[1])
  return { pass: +m[1], fail: +m[2], skip: +m[3], crashed: false, fails }
}

const glob = (p) => globSync(p, { cwd: root }).sort()

// Group definitions: which files, and whether they need a DOM document.
const groups = {
  core: {
    files: [
      ...glob('test/commands/*.test.ts'),
      ...glob('test/model/*.test.ts'),
      ...glob('test/input/*.test.ts'),
      ...glob('test/helpers/*.test.ts'),
      ...glob('test/smoke.test.ts')
    ],
    document: true
  },
  view: { files: glob('test/view/*.test.ts'), document: true, docPage: domPage },
  drawing: { files: glob('test/drawing/*.test.ts'), document: true }
}
const tiers = ['tier_a_slate', 'tier_b_prosemirror', 'tier_c_wpt', 'tier_d_structural',
  'tier_e_html', 'tier_f_chromium', 'tier_0_drawing']

const want = process.argv.slice(2)
const runConformance = want.length === 0 || want.some(w => groups[w])
const runTiers = want.length === 0 || want.includes('tiers') || want.some(w => w.startsWith('tier'))

let total = { pass: 0, fail: 0, skip: 0 }
let anyBad = false

function report(name, r) {
  total.pass += r.pass; total.fail += r.fail; total.skip += r.skip
  const tag = r.crashed ? 'CRASH' : (r.fail > 0 ? 'FAIL' : 'ok')
  console.log(`  ${tag.padEnd(5)} ${name.padEnd(16)} pass=${r.pass} fail=${r.fail} skip=${r.skip}`)
  if (r.crashed) { anyBad = true; if (r.out) console.log('    (no HARNESS summary — crashed)') }
  if (r.fails && r.fails.length) { anyBad = true; for (const f of r.fails.slice(0, 8)) console.log(`      FAIL ${f}`) }
}

for (const [name, g] of Object.entries(groups)) {
  if (want.length && !want.includes(name)) continue
  if (!g.files.length) continue
  const bundle = path.join(outDir, `phaseA_${name}.js`)
  await buildConformance(g.files, bundle)
  report(name, runBundle(bundle, g.document, g.docPage))
}

if (runTiers) {
  for (const tier of tiers) {
    if (want.length && !want.includes('tiers') && !want.includes(tier)) continue
    // Skip placeholder tiers that carry no runnable corpus (e.g. tier_c_wpt is
    // a README-only stub); they are not part of the ~1601 tier total.
    if (!fs.existsSync(path.join(root, 'test', tier, 'run.test.ts'))) {
      console.log(`  skip  ${tier.padEnd(16)} (no run.test.ts — placeholder)`)
      continue
    }
    const bundle = path.join(outDir, `phaseA_${tier}.js`)
    try {
      execFileSync('node', [path.join(root, 'tools/build-tier.mjs'), '--tier', tier, '--out', bundle],
        { cwd: root, encoding: 'utf8', stdio: 'pipe' })
    } catch (e) {
      console.log(`  BUILD ${tier.padEnd(16)} build failed: ${(e.stderr || e.message || '').slice(0, 120)}`)
      anyBad = true; continue
    }
    report(tier, runBundle(bundle, true, domPage))
  }
}

console.log(`PHASE-A TOTAL pass=${total.pass} fail=${total.fail} skip=${total.skip}`)
process.exit(anyBad || total.fail > 0 ? 1 : 0)
