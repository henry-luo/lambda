// Port of lambda/package/editor/mod_history.ls
//
// Undo/redo stacks holding inverted transactions. All operations are pure:
// each push/undo/redo returns a new history value alongside the new doc state.

import { txBegin, txGetMeta, txInvert, txSetSelection, txStep } from './transaction.js'
import type { Doc, History, Selection, Step, Transaction } from './types.js'

export function historyNew(): History {
  return { undo: [], redo: [], undo_groups: [], redo_groups: [] }
}

export function canUndo(h: History): boolean { return h.undo.length > 0 }
export function canRedo(h: History): boolean { return h.redo.length > 0 }

function txMerge(a: Transaction, b: Transaction): Transaction {
  return {
    doc_before: a.doc_before,
    doc_after: b.doc_after,
    steps: [...a.steps, ...b.steps],
    sel_before: a.sel_before,
    sel_after: b.sel_after,
    meta: b.meta
  }
}

function asGroup(v: unknown): string | null {
  return typeof v === 'string' ? v : null
}

// Record `tx` after it has been applied. Pushes the inverse onto the undo
// stack and clears redo. Two consecutive transactions sharing a non-null
// `historyGroup` meta are compressed into one undo item.
export function historyPush(h: History, tx: Transaction): History {
  const group = asGroup(txGetMeta(tx, 'historyGroup'))
  const n = h.undo.length
  if (group !== null && n > 0 && h.undo_groups.length === n && h.undo_groups[n - 1] === group) {
    const prevInv = h.undo[n - 1] as Transaction
    const previous = txInvert(prevInv)
    const merged = txMerge(previous, tx)
    return {
      undo: [...h.undo.slice(0, n - 1), txInvert(merged)],
      redo: [],
      undo_groups: [...h.undo_groups.slice(0, n - 1), group],
      redo_groups: []
    }
  }
  return {
    undo: [...h.undo, txInvert(tx)],
    redo: [],
    undo_groups: [...h.undo_groups, group],
    redo_groups: []
  }
}

export interface UndoResult {
  doc: Doc
  hist: History
  sel: Selection | null
  ok: boolean
}

export function historyUndo(h: History, doc: Doc): UndoResult {
  if (!canUndo(h)) return { doc, hist: h, sel: null, ok: false }
  const n = h.undo.length
  const inv = h.undo[n - 1] as Transaction
  const group = h.undo_groups.length === n ? (h.undo_groups[n - 1] as string | null) : null
  const orig = txInvert(inv)
  const newUndo = h.undo.slice(0, n - 1)
  const newUndoGroups = h.undo_groups.length === n ? h.undo_groups.slice(0, n - 1) : h.undo_groups
  return {
    doc: inv.doc_after,
    hist: {
      undo: newUndo,
      redo: [...h.redo, orig],
      undo_groups: newUndoGroups,
      redo_groups: [...h.redo_groups, group]
    },
    sel: inv.sel_after,
    ok: true
  }
}

export function historyRedo(h: History, doc: Doc): UndoResult {
  if (!canRedo(h)) return { doc, hist: h, sel: null, ok: false }
  const n = h.redo.length
  const tx = h.redo[n - 1] as Transaction
  const group = h.redo_groups.length === n ? (h.redo_groups[n - 1] as string | null) : null
  const newRedo = h.redo.slice(0, n - 1)
  const newRedoGroups = h.redo_groups.length === n ? h.redo_groups.slice(0, n - 1) : h.redo_groups
  return {
    doc: tx.doc_after,
    hist: {
      undo: [...h.undo, txInvert(tx)],
      redo: newRedo,
      undo_groups: [...h.undo_groups, group],
      redo_groups: newRedoGroups
    },
    sel: tx.sel_after,
    ok: true
  }
}

export interface ApplyStepResult {
  doc: Doc
  hist: History
  sel: Selection | null
  tx: Transaction
}

// Convenience: wrap a single step into a one-step transaction and record it.
export function historyApplyStep(
  h: History,
  doc: Doc,
  step: Step,
  selBefore: Selection | null,
  selAfter: Selection | null
): ApplyStepResult {
  const tx0 = txBegin(doc, selBefore)
  const tx1 = txStep(tx0, step)
  const tx2 = selAfter === null ? tx1 : txSetSelection(tx1, selAfter)
  return { doc: tx2.doc_after, hist: historyPush(h, tx2), sel: tx2.sel_after, tx: tx2 }
}
