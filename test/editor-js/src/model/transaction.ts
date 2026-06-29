// Port of lambda/package/editor/mod_transaction.ls
//
// A Transaction bundles steps + before/after doc snapshots + selection mapping.
// Pure value: every modifier returns a new transaction.

import { stepApply, stepInvert, stepMap } from './step.js'
import { nodeSelection, pos, textSelection } from './source-pos.js'
import type {
  AttrValue,
  Doc,
  MetaEntry,
  Selection,
  SourcePath,
  SourcePos,
  Step,
  Transaction
} from './types.js'

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

export function txBegin(doc: Doc, sel: Selection | null): Transaction {
  return {
    doc_before: doc,
    doc_after: doc,
    steps: [],
    sel_before: sel,
    sel_after: sel,
    meta: []
  }
}

// Add a step. Returns a new transaction with the step applied to doc_after
// and the selection mapped through the step.
export function txStep(tx: Transaction, step: Step): Transaction {
  const newDoc = stepApply(step, tx.doc_after)
  const newSel = tx.sel_after === null ? null : selMap(step, tx.sel_after)
  return {
    doc_before: tx.doc_before,
    doc_after: newDoc,
    steps: [...tx.steps, step],
    sel_before: tx.sel_before,
    sel_after: newSel,
    meta: tx.meta
  }
}

export function txSetSelection(tx: Transaction, sel: Selection | null): Transaction {
  return {
    doc_before: tx.doc_before,
    doc_after: tx.doc_after,
    steps: tx.steps,
    sel_before: tx.sel_before,
    sel_after: sel,
    meta: tx.meta
  }
}

function metaSet(meta: MetaEntry[], name: string, value: AttrValue): MetaEntry[] {
  const out: MetaEntry[] = []
  let replaced = false
  for (const e of meta) {
    if (e.name === name && !replaced) {
      out.push({ name, value })
      replaced = true
    } else if (e.name !== name) {
      out.push(e)
    }
  }
  if (!replaced) out.push({ name, value })
  return out
}

export function txSetMeta(tx: Transaction, name: string, value: AttrValue): Transaction {
  return {
    doc_before: tx.doc_before,
    doc_after: tx.doc_after,
    steps: tx.steps,
    sel_before: tx.sel_before,
    sel_after: tx.sel_after,
    meta: metaSet(tx.meta, name, value)
  }
}

export function txGetMeta(tx: Transaction, name: string): AttrValue | null {
  for (const e of tx.meta) if (e.name === name) return e.value
  return null
}

// ---------------------------------------------------------------------------
// Selection mapping through one step
// ---------------------------------------------------------------------------

export function selMap(step: Step, sel: Selection): Selection {
  switch (sel.kind) {
    case 'text': return textSelection(stepMap(step, sel.anchor), stepMap(step, sel.head))
    case 'node': return selMapNode(step, sel.path)
    case 'multi-node': {
      // Drop paths whose target was deleted by the step; keep the survivors.
      const survivors: SourcePath[] = []
      for (const p of sel.paths) {
        const mapped = mapNodePath(step, p)
        if (mapped !== null) survivors.push(mapped)
      }
      return { kind: 'multi-node', paths: survivors }
    }
    case 'gap': {
      // Map the gap's container position (its index can shift when siblings are
      // inserted/removed before it).
      const container = sel.path.slice(0, -1)
      const index = sel.path[sel.path.length - 1] ?? 0
      const mapped = stepMap(step, pos(container, index))
      return { kind: 'gap', path: [...mapped.path, mapped.offset] }
    }
  }
}

// Map a node path through a step. Returns the new path if the node survives at
// the same depth; null if it was deleted/collapsed (caller decides degradation).
function mapNodePath(step: Step, path: SourcePath): SourcePath | null {
  const mapped = stepMap(step, pos(path, 0))
  return mapped.path.length === path.length ? mapped.path : null
}

// For a single NodeSelection: surviving node → NodeSelection; deleted → degrade
// to a collapsed TextSelection at the boundary (matches Lambda's mod_transaction).
function selMapNode(step: Step, path: SourcePath): Selection {
  const mapped = stepMap(step, pos(path, 0))
  if (mapped.path.length === path.length) return nodeSelection(mapped.path)
  return textSelection(mapped, mapped)
}

// ---------------------------------------------------------------------------
// Inversion
// ---------------------------------------------------------------------------

// Walk the original steps forward so each invert sees the doc it was applied
// against, accumulating inverses in reverse order.
export function txInvert(tx: Transaction): Transaction {
  const invSteps: Step[] = []
  let cursor = tx.doc_before
  for (const s of tx.steps) {
    invSteps.unshift(stepInvert(s, cursor))
    cursor = stepApply(s, cursor)
  }
  return {
    doc_before: tx.doc_after,
    doc_after: tx.doc_before,
    steps: invSteps,
    sel_before: tx.sel_after,
    sel_after: tx.sel_before,
    meta: []
  }
}

// ---------------------------------------------------------------------------
// Map a SourcePos through every step in this transaction in order.
// ---------------------------------------------------------------------------

export function txMapPos(tx: Transaction, p: SourcePos): SourcePos {
  let cur = p
  for (const s of tx.steps) cur = stepMap(s, cur)
  return cur
}
