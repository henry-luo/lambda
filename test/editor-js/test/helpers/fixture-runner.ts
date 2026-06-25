// Fixture runner — loads a `(input.html, events.json, output.html)` triple
// from disk, runs the events against the initial state, and returns the
// actual + expected docs so the test can assert deep equality.
//
// Fixture directory layout:
//   <case-name>/
//     input.html        — source doc with cursor/anchor/focus markers
//     events.json       — sequence of operations (see event-driver.ts)
//     output.html       — expected doc after the events
//     NOTES.md          — (optional) rationale for divergence from upstream

import fs from 'node:fs'
import path from 'node:path'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'
import { docSchema } from '../../src/schemas/doc.js'
import { historyApplyStep, historyNew } from '../../src/model/history.js'
import type { Doc, History, MarkDict, Selection, Step, Transaction } from '../../src/model/types.js'
import type { Schema } from '../../src/model/schema.js'
import type { EditorState } from '../../src/commands/types.js'
import { driveEvents, type FixtureEvent } from './event-driver.js'

export interface FixtureCase {
  name: string
  dir: string
  inputHtml: string
  eventsJson: string
  outputHtml: string
  notes?: string
}

export interface FixtureResult {
  actualDoc: Doc
  actualSelection: Selection | null
  expectedDoc: Doc
  expectedSelection: Selection | null
  history: History
  stored_marks: MarkDict | null
  steps_applied: number
}

// ---------------------------------------------------------------------------
// Disk loaders
// ---------------------------------------------------------------------------

export function loadFixture(dir: string): FixtureCase {
  const inputHtml  = fs.readFileSync(path.join(dir, 'input.html'),   'utf8')
  const eventsJson = fs.readFileSync(path.join(dir, 'events.json'),  'utf8')
  const outputHtml = fs.readFileSync(path.join(dir, 'output.html'),  'utf8')
  const notesPath  = path.join(dir, 'NOTES.md')
  const notes = fs.existsSync(notesPath) ? fs.readFileSync(notesPath, 'utf8') : undefined
  return {
    name: path.basename(dir),
    dir,
    inputHtml,
    eventsJson,
    outputHtml,
    ...(notes !== undefined ? { notes } : {})
  }
}

// Find every direct child of `rootDir` that looks like a fixture dir
// (contains input.html). Returns absolute paths.
export function findFixtureDirs(rootDir: string): string[] {
  const out: string[] = []
  walk(rootDir, out)
  return out.sort()
}

function walk(dir: string, acc: string[]): void {
  if (!fs.existsSync(dir) || !fs.statSync(dir).isDirectory()) return
  // Skip dirs that begin with `_` (reserved for examples + scaffolding)
  const entries = fs.readdirSync(dir)
  const isCase = entries.includes('input.html') &&
                 entries.includes('events.json') &&
                 entries.includes('output.html')
  if (isCase) {
    acc.push(dir)
    return  // a case dir is not also a container
  }
  for (const e of entries) {
    if (e.startsWith('_')) continue
    const full = path.join(dir, e)
    if (fs.statSync(full).isDirectory()) walk(full, acc)
  }
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

export function runFixtureCase(c: FixtureCase, schema: Schema = docSchema): FixtureResult {
  const initial = parseHtmlToDoc(c.inputHtml, schema)
  const expected = parseHtmlToDoc(c.outputHtml, schema)

  const events = parseEvents(c.eventsJson)
  let state: EditorState = {
    doc: initial.doc,
    schema,
    selection: initial.selection,
    stored_marks: null
  }
  let history: History = historyNew()
  let stepsApplied = 0

  for (const ev of events) {
    const tx = driveEvents(state, ev)
    if (tx !== null) {
      const out = applyTx(state, history, tx)
      state = out.state
      history = out.history
      stepsApplied += tx.steps.length
    }
  }

  return {
    actualDoc: state.doc,
    actualSelection: state.selection,
    expectedDoc: expected.doc,
    expectedSelection: expected.selection,
    history,
    stored_marks: state.stored_marks,
    steps_applied: stepsApplied
  }
}

function applyTx(state: EditorState, history: History, tx: Transaction): { state: EditorState; history: History } {
  // For simplicity we replicate the editorReducer's `apply` logic inline so
  // this runner does not depend on React.
  const storedMarksMeta = tx.meta.find(m => m.name === 'storedMarks')
  const stored = (storedMarksMeta?.value as MarkDict | undefined) ?? null
  const addToHistory = tx.meta.find(m => m.name === 'addToHistory')?.value !== false
  const newHistory = addToHistory
    ? // Push the whole transaction onto history. `historyApplyStep` is for
      // single steps; for multi-step txs we just call historyPush directly.
      pushTransactionToHistory(history, tx)
    : history
  return {
    state: {
      doc: tx.doc_after,
      schema: state.schema,
      selection: tx.sel_after,
      stored_marks: stored ?? state.stored_marks
    },
    history: newHistory
  }
}

function pushTransactionToHistory(history: History, tx: Transaction): History {
  // Inline historyPush to avoid the React-side helper.
  // We piggyback on historyApplyStep when the tx has a single step (most
  // common); for multi-step txs we just chain.
  if (tx.steps.length === 0) return history
  if (tx.steps.length === 1) {
    const r = historyApplyStep(history, tx.doc_before, tx.steps[0] as Step, tx.sel_before, tx.sel_after)
    return r.hist
  }
  // Chain: apply steps one by one to keep history granular
  let h = history
  let docBefore = tx.doc_before
  for (let i = 0; i < tx.steps.length; i++) {
    const step = tx.steps[i] as Step
    const r = historyApplyStep(h, docBefore, step, i === 0 ? tx.sel_before : null, i === tx.steps.length - 1 ? tx.sel_after : null)
    h = r.hist
    docBefore = r.doc
  }
  return h
}

// ---------------------------------------------------------------------------
// Events JSON parser
// ---------------------------------------------------------------------------

function parseEvents(json: string): FixtureEvent[] {
  const parsed = JSON.parse(json)
  if (Array.isArray(parsed)) return parsed as FixtureEvent[]
  if (typeof parsed === 'object' && parsed !== null && Array.isArray(parsed.events)) {
    return parsed.events as FixtureEvent[]
  }
  throw new Error('events.json must be an array or {events: [...]}')
}
