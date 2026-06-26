// Core text commands. Subset of lambda/package/editor/mod_commands.ls
// sufficient for the Slate `transforms/` Tier-A baseline:
//
//   cmdInsertText, cmdInsertParagraph (split block), cmdInsertLineBreak,
//   cmdDeleteBackward, cmdDeleteForward, cmdDeleteSelection,
//   cmdToggleMark (formatBold/italic/underline collapse to this),
//   cmdSetBlockType, cmdSelectAll.
//
// Each command targets the "collapsed text selection inside a single leaf"
// happy path first — the most common case. Edge cases (cross-leaf, cross-
// block, all/node selections) get expanded as the corresponding Slate
// fixtures are ported.

import { isText, lastIndex, nodeAt, parentPath } from '../model/doc.js'
import { pos, textSelection } from '../model/source-pos.js'
import {
  hasMark,
  stepReplace,
  stepReplaceText,
  stepSetNodeType,
  withMark,
  withoutMark
} from '../model/step.js'
import {
  txBegin,
  txSetMeta,
  txSetSelection,
  txStep
} from '../model/transaction.js'
import { defaultSplitBlock, isBlockTag } from '../model/schema.js'
import { caret, caretAt, isText as selIsText, selCollapsed, selHi, selLo, selSingleLeaf } from './sel.js'
import type { EditorState } from './types.js'
import type { AttrValue, MarkDict, TextLeaf, Transaction } from '../model/types.js'

// ---------------------------------------------------------------------------
// Common: delete a range within a single text leaf
// ---------------------------------------------------------------------------

function deleteRangeInLeaf(state: EditorState, from: import('../model/types.js').SourcePos, to: import('../model/types.js').SourcePos): Transaction | null {
  const leaf = nodeAt(state.doc, from.path)
  if (leaf === null || !isText(leaf)) return null
  const sel = state.selection
  let tx = txBegin(state.doc, sel)
  // If the entire text leaf is being emptied, drop the leaf via `replace`
  // rather than leaving an empty leaf via `replace_text`. Matches the
  // parser's round-trip representation of an empty block.
  if (from.offset === 0 && to.offset === leaf.text.length) {
    const blockPath = parentPath(from.path)
    const leafIdx = lastIndex(from.path)
    tx = txStep(tx, stepReplace(blockPath, leafIdx, leafIdx + 1, []))
    return txSetSelection(tx, caret(pos(blockPath, leafIdx)))
  }
  tx = txStep(tx, stepReplaceText(from.path, from.offset, to.offset, ''))
  return txSetSelection(tx, caret(pos(from.path, from.offset)))
}

// Delete the current (non-collapsed, single-leaf) selection.
function deleteSelectionSimple(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null || !selIsText(sel) || selCollapsed(sel) || !selSingleLeaf(sel)) return null
  return deleteRangeInLeaf(state, selLo(sel), selHi(sel))
}

// ---------------------------------------------------------------------------
// cmdInsertText
// ---------------------------------------------------------------------------

export function cmdInsertText(state: EditorState, txt: string): Transaction | null {
  const sel = state.selection
  if (sel === null) return null
  if (!selIsText(sel)) return null
  if (!selSingleLeaf(sel)) return null  // cross-leaf edits — TODO when fixtures demand

  const lo = selLo(sel)
  const hi = selHi(sel)
  const leaf = nodeAt(state.doc, lo.path)
  if (leaf === null || !isText(leaf)) return null

  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplaceText(lo.path, lo.offset, hi.offset, txt))
  return txSetSelection(tx, caretAt(lo.path, lo.offset + txt.length))
}

// ---------------------------------------------------------------------------
// cmdInsertParagraph (split_block)
//
// Splits the block containing the caret into two siblings. The default new
// block tag is schema.default_block (e.g., 'p') — caller can override via
// cmdSetBlockType after the fact.
// ---------------------------------------------------------------------------

export function cmdInsertParagraph(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null || !selIsText(sel)) return null

  // If non-collapsed, delete first then split at lo.
  if (!selCollapsed(sel)) {
    const after = deleteSelectionSimple(state)
    if (after === null) return null
    const nextState: EditorState = { ...state, doc: after.doc_after, selection: after.sel_after }
    const next = cmdInsertParagraph(nextState)
    if (next === null) return null
    // chain: re-issue with merged before/after
    return {
      doc_before: state.doc,
      doc_after: next.doc_after,
      steps: [...after.steps, ...next.steps],
      sel_before: state.selection,
      sel_after: next.sel_after,
      meta: []
    }
  }

  const p = sel.anchor
  const leafPath = p.path
  if (leafPath.length < 2) return null  // need at least block→leaf

  const leaf = nodeAt(state.doc, leafPath)
  if (leaf === null || !isText(leaf)) return null

  const blockPath = parentPath(leafPath)
  const blockNode = nodeAt(state.doc, blockPath)
  if (blockNode === null || blockNode.kind !== 'node') return null

  const leafIdx = lastIndex(leafPath)
  const blockParentPath = parentPath(blockPath)
  const blockIdx = lastIndex(blockPath)
  const splitTag = defaultSplitBlock(state.schema, blockNode.tag)

  // First block: original block tag + content[0..leafIdx] + leaf-prefix
  const prefixText = leaf.text.slice(0, p.offset)
  const suffixText = leaf.text.slice(p.offset)
  const leafPrefix: TextLeaf[] = prefixText.length > 0 ? [{ kind: 'text', text: prefixText, marks: leaf.marks }] : []
  const leafSuffix: TextLeaf[] = suffixText.length > 0 ? [{ kind: 'text', text: suffixText, marks: leaf.marks }] : []
  const trailingChildren = blockNode.content.slice(leafIdx + 1)

  const first = {
    kind: 'node' as const,
    tag: blockNode.tag,
    attrs: blockNode.attrs,
    content: [...blockNode.content.slice(0, leafIdx), ...leafPrefix]
  }
  const second = {
    kind: 'node' as const,
    tag: splitTag,
    attrs: [],
    content: [...leafSuffix, ...trailingChildren]
  }

  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(blockParentPath, blockIdx, blockIdx + 1, [first, second]))

  // Caret lands at start of the second block's first text leaf
  // (or at child-index 0 of an empty second block).
  const secondBlockPath = [...blockParentPath, blockIdx + 1]
  const newCaret = leafSuffix.length > 0
    ? caretAt([...secondBlockPath, 0], 0)
    : caretAt(secondBlockPath, 0)

  return txSetSelection(tx, newCaret)
}

// ---------------------------------------------------------------------------
// cmdInsertLineBreak — insert a <br> inline element at the caret.
// ---------------------------------------------------------------------------

export function cmdInsertLineBreak(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null || !selIsText(sel) || !selSingleLeaf(sel)) return null
  const lo = selLo(sel)
  const hi = selHi(sel)
  const leaf = nodeAt(state.doc, lo.path)
  if (leaf === null || !isText(leaf) || lo.path.length < 2) return null

  const blockPath = parentPath(lo.path)
  const leafIdx = lastIndex(lo.path)

  const before = lo.offset > 0
    ? [{ kind: 'text' as const, text: leaf.text.slice(0, lo.offset), marks: leaf.marks }]
    : []
  const after  = {
    kind: 'text' as const,
    text: leaf.text.slice(hi.offset),
    marks: leaf.marks
  }
  const slice = [
    ...before,
    { kind: 'node' as const, tag: 'br', attrs: [], content: [] },
    after
  ]
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(blockPath, leafIdx, leafIdx + 1, slice))
  return txSetSelection(tx, caretAt([...blockPath, leafIdx + before.length + 1], 0))
}

// ---------------------------------------------------------------------------
// cmdDeleteNode — remove a NodeSelection's target (an image, table, drawing,
// or any selectable block addressed as a unit). Caret lands at the gap.
// ---------------------------------------------------------------------------

export function cmdDeleteNode(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null || sel.kind !== 'node') return null
  if (sel.path.length === 0) return null
  const parent = parentPath(sel.path)
  const idx = lastIndex(sel.path)
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(parent, idx, idx + 1, []))
  return txSetSelection(tx, caret(pos(parent, idx)))
}

// ---------------------------------------------------------------------------
// cmdDeleteBackward / cmdDeleteForward (single-leaf happy path)
// ---------------------------------------------------------------------------

export function cmdDeleteBackward(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null) return null
  if (sel.kind === 'node') return cmdDeleteNode(state)
  if (!selIsText(sel)) return null
  if (!selCollapsed(sel)) return deleteSelectionSimple(state)
  if (!selSingleLeaf(sel)) return null
  const p = sel.anchor
  if (p.offset <= 0) return null  // boundary: caller can chain to merge_blocks_backward later
  return deleteRangeInLeaf(state, pos(p.path, p.offset - 1), p)
}

export function cmdDeleteForward(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null) return null
  if (sel.kind === 'node') return cmdDeleteNode(state)
  if (!selIsText(sel)) return null
  if (!selCollapsed(sel)) return deleteSelectionSimple(state)
  if (!selSingleLeaf(sel)) return null
  const p = sel.anchor
  const leaf = nodeAt(state.doc, p.path)
  if (leaf === null || !isText(leaf)) return null
  if (p.offset >= leaf.text.length) return null  // boundary: merge_blocks_forward later
  return deleteRangeInLeaf(state, p, pos(p.path, p.offset + 1))
}

// ---------------------------------------------------------------------------
// cmdToggleMark — toggle a single mark over a text-range selection.
//
// For a same-leaf range selection, splits the leaf into up to three pieces
// (before, target, after) and toggles the mark on the target middle leaf
// via a single `replace` step. Adjacent same-mark leaves remain mergeable
// post-toggle but no merging happens inside this step — that's the
// renderer / parser's job.
//
// For a collapsed selection, updates stored_marks instead so the next typed
// character picks up the mark (PM/Slate idiom).
// ---------------------------------------------------------------------------

export function cmdToggleMark(state: EditorState, name: string, value: AttrValue = true): Transaction | null {
  const sel = state.selection
  if (sel === null) return null
  if (!selIsText(sel)) return null

  if (selCollapsed(sel)) return cmdToggleStoredMark(state, name, value)
  if (!selSingleLeaf(sel)) return null

  const lo = selLo(sel)
  const hi = selHi(sel)
  const leaf = nodeAt(state.doc, lo.path)
  if (leaf === null || !isText(leaf)) return null

  const targetText = leaf.text.slice(lo.offset, hi.offset)
  if (targetText.length === 0) return null

  // Decide: is the mark currently present on the whole target range?
  // (Range is within a single leaf, so the answer is just "does the leaf have it?")
  const present = hasMark(leaf.marks, name)
  const newMarks: MarkDict = present
    ? withoutMark(leaf.marks, name)
    : withMark(leaf.marks, name, value)

  const beforeText = leaf.text.slice(0, lo.offset)
  const afterText  = leaf.text.slice(hi.offset)
  const newLeaves: TextLeaf[] = []
  if (beforeText.length > 0) newLeaves.push({ kind: 'text', text: beforeText, marks: leaf.marks })
  newLeaves.push({ kind: 'text', text: targetText, marks: newMarks })
  if (afterText.length  > 0) newLeaves.push({ kind: 'text', text: afterText,  marks: leaf.marks })

  const blockPath = parentPath(lo.path)
  const leafIdx = lastIndex(lo.path)
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(blockPath, leafIdx, leafIdx + 1, newLeaves))
  // Selection endpoints follow the parser's "marker at boundary attaches to
  // previous leaf at its end" convention (Inline_Formatting §4.4):
  //   - anchor: if a before-leaf exists, end of before-leaf; else start of middle
  //   - head:   end of middle-leaf (consistent with markers placed OUTSIDE the span wrapper)
  // This means the new TextSelection bit-equals what the parser produces if
  // the same doc is round-tripped through render → HTML → parse.
  const middleIdx = leafIdx + (beforeText.length > 0 ? 1 : 0)
  const anchorPos = beforeText.length > 0
    ? pos([...blockPath, leafIdx], beforeText.length)
    : pos([...blockPath, middleIdx], 0)
  const headPos = pos([...blockPath, middleIdx], targetText.length)
  tx = txSetSelection(tx, { kind: 'text', anchor: anchorPos, head: headPos })
  return tx
}

export function cmdToggleStoredMark(state: EditorState, name: string, value: AttrValue = true): Transaction | null {
  const cur: MarkDict = state.stored_marks ?? {}
  const next: MarkDict = hasMark(cur, name) ? withoutMark(cur, name) : withMark(cur, name, value)
  let tx = txBegin(state.doc, state.selection)
  tx = txSetMeta(tx, 'storedMarks', next)
  tx = txSetMeta(tx, 'addToHistory', false)
  return tx
}

export function cmdFormatBold(state: EditorState):       Transaction | null { return cmdToggleMark(state, 'bold') }
export function cmdFormatItalic(state: EditorState):     Transaction | null { return cmdToggleMark(state, 'italic') }
export function cmdFormatUnderline(state: EditorState):  Transaction | null { return cmdToggleMark(state, 'underline') }
export function cmdFormatCode(state: EditorState):       Transaction | null { return cmdToggleMark(state, 'code') }

// ---------------------------------------------------------------------------
// cmdSetBlockType — retag the block containing the caret.
// ---------------------------------------------------------------------------

export function cmdSetBlockType(state: EditorState, tag: string): Transaction | null {
  const sel = state.selection
  if (sel === null || !selIsText(sel)) return null
  if (!isBlockTag(state.schema, tag)) return null
  const leafPath = sel.anchor.path
  if (leafPath.length < 2) return null
  const blockPath = parentPath(leafPath)
  const blockNode = nodeAt(state.doc, blockPath)
  if (blockNode === null || blockNode.kind !== 'node') return null

  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepSetNodeType(blockPath, tag))
  return tx
}

// ---------------------------------------------------------------------------
// cmdSelectAll
//
// Per Inline_Formatting design doc §11: AllSelection was dropped. "Select
// all" is a TextSelection spanning the doc-root boundaries — same state,
// fewer concepts.
// ---------------------------------------------------------------------------

export function cmdSelectAll(state: EditorState): Transaction | null {
  const sel = textSelection(pos([], 0), pos([], state.doc.content.length))
  let tx = txBegin(state.doc, state.selection)
  tx = txSetSelection(tx, sel)
  tx = txSetMeta(tx, 'addToHistory', false)
  return tx
}

