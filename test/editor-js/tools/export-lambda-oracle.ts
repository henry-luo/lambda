// JS→Lambda oracle bridge (POC).
//
// Drives real JS-reference fixtures through the JS editor (the oracle), then
// emits a Lambda test script that rebuilds the same input + runs the equivalent
// Lambda commands and checks the result fingerprint against the JS-derived
// expected. Green ⇒ the Lambda port matches the verified JS behaviour for that
// case; red ⇒ a concrete divergence to reconcile.
//
// Scope (POC): mark-free, text-structural commands only — insertText,
// insertParagraph, insertLineBreak, deleteContentBackward/Forward, setBlockType.
//
// Run:  npx vite-node tools/export-lambda-oracle.ts

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { JSDOM } from 'jsdom'
import { walk } from './chromium-adapt.ts'

const jsdom = new JSDOM()
;(globalThis as unknown as { DOMParser: typeof DOMParser }).DOMParser = jsdom.window.DOMParser as unknown as typeof DOMParser

const { parseHtmlToDoc } = await import('../src/view/html-parser.ts')
const { docSchema } = await import('../src/schemas/doc.ts')
const { runFixtureCase } = await import('../test/helpers/fixture-runner.ts')

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const FIXTURE_ROOTS = ['tier_e_html', 'tier_a_slate', 'tier_b_prosemirror'].map(t => path.resolve(__dirname, '../test', t))
const OUT_LS = path.resolve(__dirname, '../../lambda/editor/oracle_poc.ls')
const OUT_TXT = path.resolve(__dirname, '../../lambda/editor/oracle_poc.txt')

// JS mark name → Lambda mark name (boolean marks only, for this POC).
const JS_TO_LAMBDA: Record<string, string> = { bold: 'strong', italic: 'em', underline: 'u', strikethrough: 's', code: 'code' }
const SUPPORTED_MARKS = new Set(Object.keys(JS_TO_LAMBDA))

// JS command name → Lambda command call (state var substituted as $S).
// A handler may return null to mark the fixture unconvertible (→ skipped).
const CMD: Record<string, (args: Record<string, unknown>) => string | null> = {
  insertText:            a => `cmd_insert_text($S, ${jsStr(String(a['text'] ?? ''))})`,
  insertParagraph:       () => `cmd_split_block($S)`,
  insertLineBreak:       () => `cmd_insert_line_break($S)`,
  deleteContentBackward: () => `cmd_delete_backward($S)`,
  deleteContentForward:  () => `cmd_delete_forward($S)`,
  setBlockType:          a => `cmd_set_block_type($S, '${String(a['tag'])}')`,
  toggleMark:            a => {
    const m = String(a['mark'] ?? '')
    if (!SUPPORTED_MARKS.has(m) || (a['value'] !== undefined && a['value'] !== true)) return null
    return `cmd_toggle_mark($S, '${JS_TO_LAMBDA[m]}', true)`
  }
}

function jsStr(s: string): string { return JSON.stringify(s) }

// Fingerprint identical in JS and Lambda: text → "raw"; node → tag(child,child).
// (Restricted to safe text so quote-wrapping needs no escaping on either side.)
type Child = { kind: 'text'; text: string; marks: Record<string, unknown> }
  | { kind: 'node'; tag: string; attrs: { name: string; value: unknown }[]; content: Child[] }

// Canonical mark order (Lambda names) — both fingerprints use it, so no
// lexicographic sort is needed (Lambda's sort() is numeric-only).
const MARK_ORDER = ['strong', 'em', 'u', 's', 'code']

function fp(n: Child): string {
  if (n.kind === 'node') return `${n.tag}(${n.content.map(fp).join(',')})`
  const ks = Object.keys(n.marks).map(k => JS_TO_LAMBDA[k] ?? k)
  ks.sort((a, b) => MARK_ORDER.indexOf(a) - MARK_ORDER.indexOf(b))
  return ks.length === 0 ? `"${n.text}"` : `"${n.text}"^${ks.join('+')}`
}

const SAFE = /^[A-Za-z0-9 ,.!?:;'’\-()]*$/
// A leaf is supported if its text is safe and every mark is a boolean mark in
// the supported set (value === true). Mark-free leaves trivially qualify.
function markSupported(n: Child): boolean {
  if (n.kind === 'text') {
    if (!SAFE.test(n.text)) return false
    return Object.entries(n.marks).every(([k, v]) => SUPPORTED_MARKS.has(k) && v === true)
  }
  return n.attrs.length === 0 && n.content.every(markSupported)
}

// Serialize a parsed doc/child to Lambda constructor calls. Marks are mapped
// to Lambda names and emitted as {name, value:true} entries (value-carrying form).
function toLambda(n: Child): string {
  if (n.kind === 'text') {
    const ks = Object.keys(n.marks).map(k => JS_TO_LAMBDA[k] ?? k)
    if (ks.length === 0) return `text(${jsStr(n.text)})`
    const entries = ks.map(name => `{name: '${name}', value: true}`).join(', ')
    return `text_marked(${jsStr(n.text)}, [${entries}])`
  }
  return `node('${n.tag}', [${n.content.map(toLambda).join(', ')}])`
}

function posLambda(p: { path: number[]; offset: number }): string {
  return `pos([${p.path.join(', ')}], ${p.offset})`
}

interface Sel { kind: string; anchor?: { path: number[]; offset: number }; head?: { path: number[]; offset: number } }
function selLambda(sel: Sel | null): string | null {
  if (sel === null || sel.kind !== 'text' || !sel.anchor || !sel.head) return null
  return `text_selection(${posLambda(sel.anchor)}, ${posLambda(sel.head)})`
}

const stanzas: string[] = []
const names: string[] = []
const skip: Record<string, number> = {}
const bump = (k: string): void => { skip[k] = (skip[k] ?? 0) + 1 }
let idx = 0

for (const root of FIXTURE_ROOTS) {
  const dirs: string[] = []
  walk(root, dirs)
  const seen = new Set<string>()
  for (const file of dirs) {
    if (path.basename(file) !== 'input.html') continue
    const dir = path.dirname(file)
    if (seen.has(dir)) continue
    seen.add(dir)
    const inputHtml = fs.readFileSync(file, 'utf8').trim()
    const eventsPath = path.join(dir, 'events.json')
    if (!fs.existsSync(eventsPath)) continue
    let events: { type: string; name?: string; args?: Record<string, unknown> }[]
    try { events = JSON.parse(fs.readFileSync(eventsPath, 'utf8')) } catch { continue }

    // Only command events in our supported set.
    if (events.length === 0 || events.some(e => e.type !== 'command' || !e.name || !(e.name in CMD))) { bump('unsupported-events'); continue }

    let parsed
    try { parsed = parseHtmlToDoc(inputHtml, docSchema) } catch { bump('parse-error'); continue }
    const inDoc = parsed.doc as unknown as Child
    if (!markSupported(inDoc)) { bump('marks-or-unsafe-input'); continue }
    const sel = selLambda(parsed.selection as Sel | null)
    if (sel === null) { bump('non-text-selection'); continue }

    // Map each event to a Lambda command call (any null → unconvertible).
    const calls = events.map(e => CMD[e.name!]!(e.args ?? {}))
    if (calls.some(c => c === null)) { bump('unsupported-mark-arg'); continue }

    let res
    try { res = runFixtureCase({ name: '', dir: '', inputHtml, eventsJson: JSON.stringify(events), outputHtml: inputHtml }) } catch { bump('run-error'); continue }
    const out = res.actualDoc as unknown as Child
    if (!markSupported(out)) { bump('marks-or-unsafe-output'); continue }
    const expected = fp(out)
    if (expected.includes('\n')) { bump('newline-in-fp'); continue }

    // Build the Lambda stanza: thread state through each command.
    const i = idx++
    const rel = path.relative(path.resolve(__dirname, '../test'), dir)
    names.push(rel)
    const lines: string[] = [`// [${i}] ${rel}`]
    lines.push(`let d_${i} = ${toLambda(inDoc)}`)
    lines.push(`let st_${i}_0 = {doc: d_${i}, selection: ${sel}, schema: html5_subset_schema}`)
    calls.forEach((call, k) => {
      lines.push(`let st_${i}_${k + 1} = next_state(st_${i}_${k}, ${(call as string).replace('$S', `st_${i}_${k}`)})`)
    })
    lines.push(`fp(st_${i}_${events.length}.doc) == ${jsStr(expected)}`)
    stanzas.push(lines.join('\n'))
    if (idx >= 9999) break
  }
  if (idx >= 9999) break
}

const header = `// oracle_poc.ls — JS→Lambda parity check (generated by tools/export-lambda-oracle.ts).
// Each case rebuilds a JS-reference fixture's input, runs the equivalent Lambda
// command(s), and compares the result fingerprint to the JS oracle's output.
// Every line should print \`true\`. A \`false\` marks a Lambda/JS divergence.
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_transaction
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_commands
import lambda.package.editor.mod_md_schema

fn next_state(s, tx) =>
  if (tx == null) { s }
  else { {doc: tx.doc_after, selection: tx.sel_after, schema: s.schema} }

fn join_sep_at(arr, sep, i, n, acc) {
  if (i >= n) { acc }
  else if (i == 0) { join_sep_at(arr, sep, i + 1, n, arr[i]) }
  else { join_sep_at(arr, sep, i + 1, n, acc ++ sep ++ arr[i]) }
}
fn join_parts(arr) => join_sep_at(arr, ",", 0, len(arr), "")
fn join_plus(arr) => join_sep_at(arr, "+", 0, len(arr), "")

// Canonical mark order (matches the JS exporter) so fingerprints agree without
// a string sort (Lambda's sort() is numeric-only).
let MARK_ORDER = ['strong', 'em', 'u', 's', 'code']
fn mark_present_at(marks, name, i, n) {
  if (i >= n) { false }
  else if (marks[i].name == name) { true }
  else { mark_present_at(marks, name, i + 1, n) }
}
fn ordered_mark_names(marks) =>
  [for (name in MARK_ORDER where mark_present_at(marks, name, 0, len(marks))) string(name)]

fn fp(n) {
  if (is_text(n)) {
    let ks = ordered_mark_names(n.marks)
    if (len(ks) == 0) { '"' ++ n.text ++ '"' }
    else { '"' ++ n.text ++ '"^' ++ join_plus(ks) }
  }
  else if (is_node(n)) { string(n.tag) ++ "(" ++ join_parts([for (c in n.content) fp(c)]) ++ ")" }
  else { "?" }
}
`

fs.writeFileSync(OUT_LS, header + '\n' + stanzas.join('\n\n') + '\n')
fs.writeFileSync(OUT_TXT, Array.from({ length: idx }, () => 'true').join('\n') + '\n')
console.log(`exported ${idx} oracle cases to ${path.relative(path.resolve(__dirname, '../../..'), OUT_LS)}`)
console.log('skips:', JSON.stringify(skip))
