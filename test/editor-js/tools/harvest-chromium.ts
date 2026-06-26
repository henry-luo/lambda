// Scenario-harvest generator for the Chromium editing test suite.
//
// Reads Chromium `assert_selection(input, tester, expected)` tests, converts
// the convertible subset (supported execCommands, flat tester body, no
// selection.modify / clipboard) into our fixture format, runs them through our
// editor, and writes (input.html, events.json, output.html) triples under
// test/tier_f_chromium/<category>/<name>/.
//
// Run with:  npx vite-node tools/harvest-chromium.ts
// Requires jsdom (devDependency) for DOMParser.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { JSDOM } from 'jsdom'

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

// ---------------------------------------------------------------------------
// execCommand → our event mapping
// ---------------------------------------------------------------------------

type Op = { command: string; args?: Record<string, unknown> }

function mapExec(name: string, arg: string | null): Op | null {
  const n = name.toLowerCase()
  switch (n) {
    case 'inserttext':        return { command: 'insertText', args: { text: arg ?? '' } }
    case 'insertparagraph':   return { command: 'insertParagraph' }
    case 'insertlinebreak':   return { command: 'insertLineBreak' }
    case 'delete':            return { command: 'deleteContentBackward' }
    case 'forwarddelete':     return { command: 'deleteContentForward' }
    case 'bold':              return { command: 'toggleMark', args: { mark: 'bold' } }
    case 'italic':            return { command: 'toggleMark', args: { mark: 'italic' } }
    case 'underline':         return { command: 'toggleMark', args: { mark: 'underline' } }
    case 'strikethrough':     return { command: 'toggleMark', args: { mark: 'strikethrough' } }
    case 'subscript':         return { command: 'toggleMark', args: { mark: 'subscript' } }
    case 'superscript':       return { command: 'toggleMark', args: { mark: 'superscript' } }
    case 'forecolor':         return arg ? { command: 'toggleMark', args: { mark: 'color', value: arg } } : null
    case 'backcolor':
    case 'hilitecolor':       return arg ? { command: 'toggleMark', args: { mark: 'background', value: arg } } : null
    case 'createlink':        return arg ? { command: 'toggleMark', args: { mark: 'link', value: arg } } : null
    case 'unlink':            return { command: 'toggleMark', args: { mark: 'link' } }
    case 'formatblock':       return arg ? { command: 'setBlockType', args: { tag: arg.replace(/[<>]/g, '').toLowerCase() } } : null
    case 'insertorderedlist': return { command: 'wrapInList', args: { kind: 'ol' } }
    case 'insertunorderedlist':return { command: 'wrapInList', args: { kind: 'ul' } }
    case 'indent':            return { command: 'indentListItem' }
    case 'outdent':           return { command: 'outdentListItem' }
    case 'selectall':         return { command: 'selectAll' }
    default:                  return null  // unsupported → caller skips the whole test
  }
}

// ---------------------------------------------------------------------------
// Tester body → operation list (flat sequences + simple for-loops only)
// ---------------------------------------------------------------------------

const UNSUPPORTED_SIGNAL = /setClipboardData|clipboardData|execCommand\(\s*['"](paste|copy|cut|undo|redo|inserthtml|insertimage|removeformat|fontsize|fontname|justify|inserthorizontalrule|stylewithcss|defaultparagraph)/i
// selection.modify granularities we cannot do headless (need layout / not modeled).
const UNSUPPORTED_MODIFY = /\.modify\s*\(\s*['"][a-z]+['"]\s*,\s*['"][a-z]+['"]\s*,\s*['"](line|lineboundary|sentence|paragraph|paragraphboundary)['"]/i
// Selection setup calls we can't replay → reject the whole test.
const OTHER_SELECTION = /selection\.(collapse|extend|setBaseAndExtent|setPosition|removeAllRanges|addRange|deleteFromDocument|selectAllChildren)\(/
const SUPPORTED_GRAN = new Set(['character', 'word', 'documentboundary'])

const skipReasons: Record<string, number> = {}
function skip(reason: string): null { skipReasons[reason] = (skipReasons[reason] ?? 0) + 1; return null }

// Extract a flat op list from a tester body, interleaving execCommand and
// selection.modify in source order. Returns null on anything we don't handle.
function parseTester(body: string): Op[] | null {
  // String-form tester: assert_selection(input, 'insertParagraph', expected) or
  // 'insertText abcd' — the 2nd arg is `command` or `command arg` (space-split).
  const asString = stripStringLiteral(body)
  if (asString !== null) {
    const sp = asString.indexOf(' ')
    const cmd = sp === -1 ? asString : asString.slice(0, sp)
    const arg = sp === -1 ? null : asString.slice(sp + 1)
    const op = mapExec(cmd, arg)
    return op ? [op] : skip(`exec-unsupported:${cmd.toLowerCase()}`)
  }
  if (UNSUPPORTED_SIGNAL.test(body)) return skip('clipboard/undo/unsupported-exec')
  if (UNSUPPORTED_MODIFY.test(body)) return skip('modify-line/sentence/paragraph')

  // Unroll simple `for (var i=0; i<N; ++i) <stmt>;` loops (one level).
  const forLoop = /for\s*\(\s*(?:var|let)\s+\w+\s*=\s*0\s*;\s*\w+\s*<\s*(\d+)\s*;\s*\+\+?\w+\+?\+?\s*\)\s*([\s\S]*?;)/g
  let expanded = body
  let m: RegExpExecArray | null, guard = 0
  const loops: { full: string; n: number; stmt: string }[] = []
  forLoop.lastIndex = 0
  while ((m = forLoop.exec(body)) !== null && guard++ < 200) {
    loops.push({ full: m[0]!, n: parseInt(m[1]!, 10), stmt: m[2]! })
  }
  for (const l of loops) {
    if (l.n > 50) return null
    expanded = expanded.replace(l.full, Array.from({ length: l.n }, () => l.stmt).join('\n'))
  }

  if (OTHER_SELECTION.test(expanded)) return skip('selection.collapse/extend setup')

  // Linear scan: pick up every execCommand(...) and .modify(...) in order.
  const callRe = /(?:selection\.)?document\.execCommand\(|selection\.modify\(/g
  const ops: Op[] = []
  callRe.lastIndex = 0
  guard = 0
  while ((m = callRe.exec(expanded)) !== null && guard++ < 5000) {
    const isModify = m[0]!.includes('modify')
    const argsStart = m.index + m[0]!.length
    const argsText = balancedSlice(expanded, argsStart)
    if (argsText === null) return skip('unbalanced-args')
    const args = splitTopLevel(argsText).map(looseLiteral)
    if (isModify) {
      const [alter, dir, gran] = args
      if (!alter || !dir || !gran || !SUPPORTED_GRAN.has(gran)) return skip('modify-unsupported-gran')
      ops.push({ command: 'moveCaret', args: { alter, direction: dir, granularity: gran } })
    } else {
      const name = args[0]
      const arg = args.length >= 3 ? args[2]! : null  // execCommand(name, false, arg)
      if (!name) return skip('exec-no-name')
      const op = mapExec(name, arg)
      if (op === null) return skip(`exec-unsupported:${name.toLowerCase()}`)
      ops.push(op)
    }
    callRe.lastIndex = argsStart + argsText.length + 1
  }
  // A tester with statements but no recognized call → likely a helper-based body.
  if (ops.length === 0) return skip('no-recognized-ops')
  return ops
}

// Unquote a string-literal token; otherwise return the trimmed token (false/true/number).
function looseLiteral(s: string): string {
  const t = s.trim()
  const q = t[0]
  if ((q === "'" || q === '"' || q === '`') && t[t.length - 1] === q) return unescapeJs(t.slice(1, -1))
  return t
}

function unescapeJs(s: string): string {
  return s.replace(/\\n/g, '\n').replace(/\\t/g, '\t').replace(/\\(['"\\])/g, '$1')
}

// ---------------------------------------------------------------------------
// Input HTML → our <doc> fixture HTML (marker + structure conversion)
// ---------------------------------------------------------------------------

function convertInput(html: string): string | null {
  // Strip the contenteditable wrapper → take its inner HTML.
  const m = html.match(/<div[^>]*contenteditable[^>]*>([\s\S]*)<\/div>\s*$/i)
  let inner = m ? m[1]! : html
  // Markers: ^ = anchor, the | after ^ = focus; a lone | = cursor.
  const hasAnchor = inner.includes('^')
  if (hasAnchor) {
    inner = inner.replace('^', '<anchor></anchor>').replace('|', '<focus></focus>')
  } else {
    inner = inner.replace('|', '<cursor></cursor>')
  }
  if (inner.includes('|') || inner.includes('^')) return null  // leftover markers → unhandled
  // Reject structures our schema does not model (a clean strip leaves none of these).
  if (/contenteditable|<\s*(div|hr|pre|section|article|figure|figcaption|input|button|svg|style|script)\b/i.test(inner)) return null

  // Wrap bare inline content in a <p> (our doc needs block children).
  const isBlock = /^\s*<(p|h[1-6]|ul|ol|blockquote|table)\b/i.test(inner)
  const body = isBlock ? inner : `<p>${inner}</p>`
  return `<doc>${body}</doc>`
}

// ---------------------------------------------------------------------------
// Run the events through the SAME path the runner uses, so the harvested
// output.html is guaranteed to replay identically (no state-threading drift).
// ---------------------------------------------------------------------------

function eventsJsonOf(ops: Op[]): string {
  return JSON.stringify(ops.map(o => ({ type: 'command', name: o.command, ...(o.args ? { args: o.args } : {}) })), null, 0)
}

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
  // The serialized output must re-parse to the same doc (else it can't be a fixture).
  const reparsed = parseHtmlToDoc(outputHtml, docSchema)
  if (JSON.stringify(reparsed.doc) !== JSON.stringify(res.actualDoc)) return { outputHtml, eventsJson, ok: false }
  return { outputHtml, eventsJson, ok: true }
}

// ---------------------------------------------------------------------------
// assert_selection(...) extraction
// ---------------------------------------------------------------------------

interface AssertCall { input: string; tester: string; expected: string; name: string }

function extractAsserts(src: string): AssertCall[] {
  const out: AssertCall[] = []
  let idx = 0
  while ((idx = src.indexOf('assert_selection(', idx)) !== -1) {
    const argsStart = idx + 'assert_selection('.length
    const argsText = balancedSlice(src, argsStart)
    idx = argsStart
    if (argsText === null) continue
    const args = splitTopLevel(argsText)
    if (args.length < 3) continue
    const input = evalStringExpr(args[0]!)
    const expected = evalStringExpr(args[2]!)
    const name = args.length >= 4 ? (stripStringLiteral(args[3]!) ?? '') : ''
    if (input === null || expected === null) continue
    out.push({ input, tester: args[1]!, expected, name })
  }
  return out
}

// Return the substring inside the parens starting at `start` (just past '('),
// up to the matching ')'. Respects strings and nested brackets.
function balancedSlice(src: string, start: number): string | null {
  let depth = 1, i = start, inStr: string | null = null
  for (; i < src.length; i++) {
    const c = src[i]!
    if (inStr) {
      if (c === '\\') { i++; continue }
      if (c === inStr) inStr = null
      continue
    }
    if (c === "'" || c === '"' || c === '`') { inStr = c; continue }
    if (c === '(' || c === '{' || c === '[') depth++
    else if (c === ')' || c === '}' || c === ']') { depth--; if (depth === 0) return src.slice(start, i) }
  }
  return null
}

function splitTopLevel(s: string): string[] {
  const parts: string[] = []
  let depth = 0, inStr: string | null = null, last = 0
  for (let i = 0; i < s.length; i++) {
    const c = s[i]!
    if (inStr) { if (c === '\\') { i++; continue } if (c === inStr) inStr = null; continue }
    if (c === "'" || c === '"' || c === '`') { inStr = c; continue }
    if (c === '(' || c === '{' || c === '[') depth++
    else if (c === ')' || c === '}' || c === ']') depth--
    else if (c === ',' && depth === 0) { parts.push(s.slice(last, i)); last = i + 1 }
  }
  parts.push(s.slice(last))
  return parts.map(p => p.trim())
}

function stripStringLiteral(s: string): string | null {
  const t = s.trim()
  const q = t[0]
  if ((q === "'" || q === '"' || q === '`') && t[t.length - 1] === q) {
    return unescapeJs(t.slice(1, -1))
  }
  return null
}

// Evaluate a static string expression: a plain literal, an `[...].join('sep')`,
// or a `'a' + 'b'` concatenation of literals. Returns null if not statically a string.
function evalStringExpr(s: string): string | null {
  const t = s.trim()
  const lit = stripStringLiteral(t)
  if (lit !== null) return lit
  const jm = t.match(/^\[([\s\S]*)\]\s*\.join\(\s*(['"])([\s\S]*?)\2\s*\)$/)
  if (jm) {
    const sep = unescapeJs(jm[3]!)
    const parts = splitTopLevel(jm[1]!).filter(p => p.length > 0).map(stripStringLiteral)
    if (parts.some(p => p === null)) return null
    return parts.join(sep)
  }
  if (t.includes('+')) {
    const pieces = splitTopLevelPlus(t).map(p => stripStringLiteral(p.trim()))
    if (pieces.length > 1 && !pieces.some(p => p === null)) return pieces.join('')
  }
  return null
}

function splitTopLevelPlus(s: string): string[] {
  const parts: string[] = []
  let depth = 0, inStr: string | null = null, last = 0
  for (let i = 0; i < s.length; i++) {
    const c = s[i]!
    if (inStr) { if (c === '\\') { i++; continue } if (c === inStr) inStr = null; continue }
    if (c === "'" || c === '"' || c === '`') { inStr = c; continue }
    if (c === '(' || c === '{' || c === '[') depth++
    else if (c === ')' || c === '}' || c === ']') depth--
    else if (c === '+' && depth === 0) { parts.push(s.slice(last, i)); last = i + 1 }
  }
  parts.push(s.slice(last))
  return parts
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

function walk(dir: string, acc: string[]): void {
  if (!fs.existsSync(dir)) return
  for (const e of fs.readdirSync(dir)) {
    const f = path.join(dir, e)
    const st = fs.statSync(f)
    if (st.isDirectory()) walk(f, acc)
    else if (e.endsWith('.html')) acc.push(f)
  }
}

function slug(s: string, fallback: string): string {
  const base = (s || fallback).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 60)
  return base || fallback
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
    const asserts = extractAsserts(src)
    let n = 0
    for (const a of asserts) {
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
