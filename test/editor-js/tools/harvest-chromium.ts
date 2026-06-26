// Scenario-harvest generator for the Chromium editing test suite.
//
// Reads Chromium `assert_selection(input, tester, expected)` tests, converts
// the convertible subset (via tools/chromium-adapt.ts) into our fixture
// vocabulary, runs them through our editor, and writes (input.html,
// events.json, output.html) triples under test/tier_f_chromium/<category>/.
// The expected output is OUR editor's result (scenario harvest).
//
// Run with:  npx vite-node tools/harvest-chromium.ts [categories…]
// Requires jsdom (devDependency) for DOMParser.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { JSDOM } from 'jsdom'
import { extractAsserts, parseTester, convertInput, eventsJsonOf, walk, slug, skipReasons, type Op } from './chromium-adapt.ts'

// Provide DOMParser for the parser before importing modules that use it.
const jsdom = new JSDOM()
;(globalThis as unknown as { DOMParser: typeof DOMParser }).DOMParser = jsdom.window.DOMParser as unknown as typeof DOMParser

const { parseHtmlToDoc, serializeDocToHtml } = await import('../src/view/html-parser.ts')
const { docSchema } = await import('../src/schemas/doc.ts')
const { runFixtureCase } = await import('../test/helpers/fixture-runner.ts')

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const CHROMIUM = path.resolve(__dirname, '../../../../lambda-test/editing')
const OUT = path.resolve(__dirname, '../test/tier_f_chromium')
const CATEGORIES = process.argv.slice(2).length ? process.argv.slice(2) : ['inserting', 'deleting', 'execCommand']

// Run the events through the SAME path the runner uses, so the harvested
// output.html is guaranteed to replay identically (no state-threading drift).
function runOps(inputHtml: string, ops: Op[]): { outputHtml: string; eventsJson: string; ok: boolean } {
  const eventsJson = eventsJsonOf(ops)
  let res
  try {
    res = runFixtureCase({ name: '', dir: '', inputHtml, eventsJson, outputHtml: inputHtml })
  } catch {
    return { outputHtml: '', eventsJson, ok: false }
  }
  if (res.steps_applied === 0) return { outputHtml: '', eventsJson, ok: false }  // no doc change
  if (res.invertRoundtrips === false) return { outputHtml: '', eventsJson, ok: false }
  const outputHtml = serializeDocToHtml(res.actualDoc, { schema: docSchema })
  const reparsed = parseHtmlToDoc(outputHtml, docSchema)
  if (JSON.stringify(reparsed.doc) !== JSON.stringify(res.actualDoc)) return { outputHtml, eventsJson, ok: false }
  return { outputHtml, eventsJson, ok: true }
}

const report = { files: 0, asserts: 0, converted: 0, skipped: { tester: 0, input: 0, run: 0 }, written: 0 }
const usedNames = new Set<string>()

for (const cat of CATEGORIES) {
  const files: string[] = []
  walk(path.join(CHROMIUM, cat), files)
  let catWritten = 0
  for (const file of files) {
    report.files++
    const src = fs.readFileSync(file, 'utf8')
    if (!src.includes('assert_selection')) continue
    const base = path.basename(file, '.html')
    let n = 0
    for (const a of extractAsserts(src)) {
      report.asserts++
      const ops = parseTester(a.tester)
      if (ops === null) { report.skipped.tester++; continue }
      const inputHtml = convertInput(a.input)
      if (inputHtml === null) { report.skipped.input++; continue }
      const res = runOps(inputHtml, ops)
      if (!res.ok) { report.skipped.run++; continue }
      report.converted++
      const name = `${cat}/${slug(base, 'test')}-${slug(a.name, String(n++))}`
      let unique = name, k = 1
      while (usedNames.has(unique)) unique = `${name}-${k++}`
      usedNames.add(unique)
      const dir = path.join(OUT, unique)
      fs.mkdirSync(dir, { recursive: true })
      fs.writeFileSync(path.join(dir, 'input.html'), inputHtml + '\n')
      fs.writeFileSync(path.join(dir, 'events.json'), res.eventsJson + '\n')
      fs.writeFileSync(path.join(dir, 'output.html'), res.outputHtml + '\n')
      report.written++
      catWritten++
    }
  }
  console.log(`${cat}: wrote ${catWritten} fixtures`)
}

console.log('\n=== HARVEST REPORT ===')
console.log(JSON.stringify(report, null, 2))
console.log('\n=== TESTER SKIP REASONS ===')
for (const [r, n] of Object.entries(skipReasons).sort((a, b) => b[1] - a[1])) console.log(`  ${n}\t${r}`)
