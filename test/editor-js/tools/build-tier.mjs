// Stage 4C — bundle a data-driven TIER conformance test to run under
// `lambda.exe js --document`. The tier loaders (helpers/fixture-runner.ts) read
// their `(input.html, events.json, output.html, NOTES.md)` fixtures via Node
// `fs`/`path`/`url`, which don't exist in-engine. This bundler:
//   1. walks the tier dir and INLINES every fixture file into a virtual FS,
//   2. supplies in-engine shims for `node:fs` / `node:path` / `node:url`
//      backed by that VFS (only the surface the tiers use),
//   3. defines `import.meta.url` so `path.dirname(fileURLToPath(import.meta.url))`
//      resolves to the tier dir,
//   4. aliases `vitest` to the harness shim and appends `__harnessRun()`.
//
// Usage (from test/editor-js):
//   node tools/build-tier.mjs --tier tier_e_html --out ../../temp/4c-spikes/tier_e.js

import { build } from 'esbuild'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import fs from 'node:fs'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')  // test/editor-js
const args = process.argv.slice(2)
let tier = null, outFile = null
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--tier') tier = args[++i]
  else if (args[i] === '--out') outFile = path.resolve(args[++i])
}
if (!tier || !outFile) { console.error('usage: --tier <name> --out <file.js>'); process.exit(2) }

const tierDir = path.resolve(root, 'test', tier)          // absolute tier dir on disk
const entryTs = path.join(tierDir, 'run.test.ts')
if (!fs.existsSync(entryTs)) { console.error('missing: ' + entryTs); process.exit(2) }

// --- walk the tier dir → virtual FS (fixture files only; skip .ts source) ----
const FILES = {}   // absPath -> content
const DIRS = {}    // absPath -> [childName,...]
function walk(dir) {
  const entries = fs.readdirSync(dir)
  DIRS[dir] = entries
  for (const e of entries) {
    const full = path.join(dir, e)
    const st = fs.statSync(full)
    if (st.isDirectory()) walk(full)
    else if (!e.endsWith('.ts')) FILES[full] = fs.readFileSync(full, 'utf8')
  }
}
walk(tierDir)

// --- in-engine shim sources (only the fs/path/url surface the tiers use) -----
const fsShim = `
const FILES = ${JSON.stringify(FILES)};
const DIRS = ${JSON.stringify(DIRS)};
const FS = {
  readFileSync(p) { if (Object.prototype.hasOwnProperty.call(FILES, p)) return FILES[p]; throw new Error('ENOENT: ' + p); },
  existsSync(p) { return Object.prototype.hasOwnProperty.call(FILES, p) || Object.prototype.hasOwnProperty.call(DIRS, p); },
  statSync(p) { const isDir = Object.prototype.hasOwnProperty.call(DIRS, p); return { isDirectory: () => isDir, isFile: () => !isDir }; },
  readdirSync(p) { return DIRS[p] || []; }
};
export default FS;
export const readFileSync = FS.readFileSync, existsSync = FS.existsSync, statSync = FS.statSync, readdirSync = FS.readdirSync;
`
const pathShim = `
function normalize(s) {
  const abs = s.startsWith('/'); const out = [];
  for (const seg of s.split('/')) {
    if (seg === '' || seg === '.') continue;
    if (seg === '..') { if (out.length && out[out.length-1] !== '..') out.pop(); else if (!abs) out.push('..'); }
    else out.push(seg);
  }
  return (abs ? '/' : '') + out.join('/');
}
const P = {
  join(...parts) { return normalize(parts.filter(p => p != null && p !== '').join('/')); },
  dirname(p) { const i = p.lastIndexOf('/'); return i < 0 ? '.' : (i === 0 ? '/' : p.slice(0, i)); },
  basename(p, ext) { let b = p.slice(p.lastIndexOf('/') + 1); if (ext && b.endsWith(ext)) b = b.slice(0, -ext.length); return b; },
  relative(from, to) {
    const f = normalize(from).split('/').filter(Boolean), t = normalize(to).split('/').filter(Boolean);
    let i = 0; while (i < f.length && i < t.length && f[i] === t[i]) i++;
    const r = [...f.slice(i).map(() => '..'), ...t.slice(i)].join('/');
    return r || '.';
  }
};
export default P;
export const join = P.join, dirname = P.dirname, basename = P.basename, relative = P.relative;
`
const urlShim = `export function fileURLToPath(u){ return String(u).replace(/^file:\\/\\//, ''); }\n`

const vfsPlugin = {
  name: 'node-vfs',
  setup(b) {
    b.onResolve({ filter: /^node:(fs|path|url)$/ }, a => ({ path: a.path, namespace: 'vfs' }))
    b.onLoad({ filter: /.*/, namespace: 'vfs' }, a => {
      const src = a.path === 'node:fs' ? fsShim : a.path === 'node:path' ? pathShim : urlShim
      return { contents: src, loader: 'js' }
    })
  }
}

const entry = [
  `import '${entryTs.replace(/\\/g, '/')}'`,
  `import { __harnessRun } from '${path.join(root, 'harness/inengine.ts').replace(/\\/g, '/')}'`,
  `__harnessRun()`
].join('\n')

fs.mkdirSync(path.dirname(outFile), { recursive: true })

await build({
  stdin: { contents: entry, resolveDir: root, sourcefile: '_tier-entry.ts', loader: 'ts' },
  bundle: true,
  format: 'iife',
  platform: 'neutral',
  outfile: outFile,
  alias: { vitest: path.resolve(root, 'harness/inengine.ts') },
  plugins: [vfsPlugin],
  define: { 'import.meta.url': JSON.stringify('file://' + entryTs) },
  logLevel: 'warning'
})

const kb = (fs.statSync(outFile).size / 1024).toFixed(0)
const fileCount = Object.keys(FILES).length
console.error(`built ${outFile} (${kb} kb) — tier ${tier}, ${fileCount} fixture files inlined`)
