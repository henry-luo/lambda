// Build the plain-DOM editor page as a SINGLE self-contained HTML with a
// CLASSIC <script> (IIFE) — NOT an ES module.
//
// Radiant's script runner executes classic inline scripts but skips
// `type="module"` scripts (confirmed 2026-06-30 via the de-risk spike), so the
// Vite single-file build (which emits `<script type="module">`) cannot run under
// Radiant. This script bundles demo/main-dom.ts as an IIFE via Vite's build API
// (so the `.js`→`.ts` import resolution still works) and inlines it into the
// HTML template as a plain <script>, producing test/html/editor-dom.html.
//
//   node tools/build-dom-page.mjs

import { build } from 'vite'
import { readFileSync, writeFileSync } from 'node:fs'
import { resolve, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const root = resolve(here, '..')               // test/editor-js
const outFile = resolve(root, '../html/editor-dom.html')

const result = await build({
  root,
  configFile: false,
  logLevel: 'warn',
  build: {
    write: false,
    target: 'es2020',
    minify: !process.env.DEBUG_BUILD,
    lib: {
      entry: resolve(root, 'demo/main-dom.ts'),
      formats: ['iife'],
      name: 'LambdaEditorDom',
      fileName: () => 'editor-dom.js'
    }
  }
})

const output = Array.isArray(result) ? result[0].output : result.output
const chunk = output.find(o => o.type === 'chunk')
if (!chunk) throw new Error('build produced no JS chunk')

const template = readFileSync(resolve(root, 'demo/editor-dom.html'), 'utf8')
const marker = '<script type="module" src="./main-dom.ts"></script>'
if (!template.includes(marker)) throw new Error('script marker not found in demo/editor-dom.html')

const html = template.replace(marker, `<script>\n${chunk.code}\n</script>`)
writeFileSync(outFile, html)
console.log(`wrote ${outFile} (classic IIFE, ${(html.length / 1024).toFixed(1)} kB)`)

// Also emit a table-seeded variant for the column-resize gesture fixture. A
// classic inline script sets window.__RDT_SEED='table' BEFORE the bundle runs,
// so main-dom.ts picks the TABLE_DOC seed.
const tableFile = resolve(root, '../html/editor-table.html')
const tableHtml = template.replace(
  marker,
  `<script>window.__RDT_SEED='table'</script>\n<script>\n${chunk.code}\n</script>`
)
writeFileSync(tableFile, tableHtml)
console.log(`wrote ${tableFile} (table seed, ${(tableHtml.length / 1024).toFixed(1)} kB)`)

// Seeded variant for gap-cursor fixtures. The HR is present at load time so the
// tests focus on gap selection/editing instead of also depending on autoformat.
const gapFile = resolve(root, '../html/editor-gap.html')
const gapHtml = template.replace(
  marker,
  `<script>window.__RDT_SEED='gap'</script>\n<script>\n${chunk.code}\n</script>`
)
writeFileSync(gapFile, gapHtml)
console.log(`wrote ${gapFile} (gap seed, ${(gapHtml.length / 1024).toFixed(1)} kB)`)
