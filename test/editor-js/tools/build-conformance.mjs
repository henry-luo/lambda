// Stage 4C — build a conformance bundle that runs editor `.test.ts` files on
// the LambdaJS runtime (`lambda.exe js`). Aliases `vitest` to the in-engine
// harness shim (harness/inengine.ts), bundles the selected tests to a classic
// IIFE, and appends a __harnessRun() call so the bundle self-reports on load.
//
// Usage (from test/editor-js):
//   node tools/build-conformance.mjs --out <file.js> <test-file> [test-file...]
//   node tools/build-conformance.mjs --slice        # a small default slice
//
// `.test.tsx` (React) files are rejected — Stage 4C targets the plain-DOM suite.

import { build } from 'esbuild'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import fs from 'node:fs'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')  // test/editor-js
const args = process.argv.slice(2)

let outFile = path.resolve(root, '../../temp/4c-spikes/conformance.js')
const files = []
let slice = false
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--out') { outFile = path.resolve(args[++i]) }
  else if (args[i] === '--slice') { slice = true }
  else { files.push(args[i]) }
}

if (slice && files.length === 0) {
  files.push('test/commands/text-commands.test.ts', 'test/commands/caret.test.ts',
    'test/model/doc.test.ts', 'test/model/step.test.ts', 'test/input/intent.test.ts')
}
if (files.length === 0) { console.error('no test files given (use --slice or pass paths)'); process.exit(2) }
for (const f of files) {
  if (f.endsWith('.tsx')) { console.error('refusing .tsx (React) file: ' + f); process.exit(2) }
  if (!fs.existsSync(path.resolve(root, f))) { console.error('missing: ' + f); process.exit(2) }
}

// Synthesize the entry: import each test (registers cases via the aliased
// `vitest` shim), then run and report.
const entry = [
  ...files.map(f => `import './${f.replace(/\\/g, '/')}'`),
  `import { __harnessRun } from './harness/inengine.ts'`,
  `__harnessRun()`
].join('\n')

fs.mkdirSync(path.dirname(outFile), { recursive: true })

await build({
  stdin: { contents: entry, resolveDir: root, sourcefile: '_conformance-entry.ts', loader: 'ts' },
  bundle: true,
  format: 'iife',
  platform: 'neutral',
  outfile: outFile,
  alias: { vitest: path.resolve(root, 'harness/inengine.ts') },
  logLevel: 'warning'
})

const kb = (fs.statSync(outFile).size / 1024).toFixed(1)
console.error(`built ${outFile} (${kb} kb) from ${files.length} test file(s)`)
