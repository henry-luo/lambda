// Gap cursor (ProseMirror GapCursor): a caret at a position with no text — at a
// container edge or adjacent to a block atom (image, hr, table, drawing). Typing
// or Enter at a gap inserts a paragraph there; Backspace/Delete removes the
// adjacent atom; caret movement (caret.ts) lands on a gap when it would otherwise
// be stuck before/after an atom.

import { isNode, node, nodeAt, text as textLeaf } from '../model/doc.js'
import { stepReplace } from '../model/step.js'
import { gapSelection, pos } from '../model/source-pos.js'
import { txBegin, txSetSelection, txStep } from '../model/transaction.js'
import { caret } from './sel.js'
import type { EditorState } from './types.js'
import type { Doc, SourcePath, Transaction } from '../model/types.js'

// Leaf block atoms — a text caret can't sit inside them, so a gap is the only
// caret position around them. (Tables/figures are containers you arrow into.)
const ATOM_TAGS = new Set(['hr', 'img', 'image', 'drawing'])

// A block that can't hold a text caret — a gap is the only caret position around it.
export function isBlockAtom(tag: string): boolean {
  return ATOM_TAGS.has(tag)
}

// Is the child at index `i` of the container at `containerPath` a block atom?
export function childIsAtom(doc: Doc, containerPath: SourcePath, i: number): boolean {
  const parent = nodeAt(doc, containerPath)
  if (parent === null || !isNode(parent)) return false
  const child = parent.content[i]
  return child !== undefined && child.kind === 'node' && isBlockAtom(child.tag)
}

function paragraphTag(state: EditorState): string {
  return state.schema.entries['p'] !== undefined ? 'p' : 'paragraph'
}

// Insert a paragraph at the gap (carrying `txt` if any); caret lands inside it.
export function cmdInsertTextAtGap(state: EditorState, txt: string): Transaction | null {
  const sel = state.selection
  if (sel === null || sel.kind !== 'gap') return null
  const container = sel.path.slice(0, -1)
  const index = sel.path[sel.path.length - 1] ?? 0
  const para = txt.length > 0 ? node(paragraphTag(state), [textLeaf(txt)]) : node(paragraphTag(state), [])
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(container, index, index, [para]))
  const caretPos = txt.length > 0 ? pos([...container, index, 0], txt.length) : pos([...container, index], 0)
  return txSetSelection(tx, caret(caretPos))
}

// Delete the block adjacent to the gap (forward = the one after, backward = before).
export function cmdDeleteAtGap(state: EditorState, forward: boolean): Transaction | null {
  const sel = state.selection
  if (sel === null || sel.kind !== 'gap') return null
  const container = sel.path.slice(0, -1)
  const index = sel.path[sel.path.length - 1] ?? 0
  const parent = nodeAt(state.doc, container)
  if (parent === null || !isNode(parent)) return null
  const target = forward ? index : index - 1
  if (target < 0 || target >= parent.content.length) return null
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(container, target, target + 1, []))
  // forward: the gap now sits before the next block (same index); backward: it shifts left.
  return txSetSelection(tx, gapSelection([...container, forward ? index : target]))
}
