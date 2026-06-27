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

import { isNode, isText, lastIndex, node, nodeAt, parentPath, withContent } from '../model/doc.js'
import { pos, posEqual, textSelection } from '../model/source-pos.js'
import { charLen, caretPosInContent, mergeInlines } from '../model/inline.js'
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
import { defaultSplitBlock, isBlockTag, schemaAllowsInline } from '../model/schema.js'
import type { Schema } from '../model/schema.js'
import { caret, caretAt, isText as selIsText, selCollapsed, selHi, selLo, selSingleLeaf } from './sel.js'
import type { EditorState } from './types.js'
import type { AttrValue, Child, MarkDict, Node, SourcePath, SourcePos, TextLeaf, Transaction } from '../model/types.js'

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

// Canonicalize a position to a within-block leaf/node position. Descends
// through block-level (doc-root / container) node positions to the nearest
// leaf boundary, so `{path:[], offset:1}` (a gap between blocks) becomes the
// start of the relevant block's first leaf rather than a doc-root position.
function resolvePos(doc: Node, p: SourcePos): SourcePos {
  let path = [...p.path]
  let off = p.offset
  for (let guard = 0; guard < 50; guard++) {
    const n = nodeAt(doc, path)
    if (n === null || isText(n)) return pos(path, off)
    if (n.content.length === 0) return pos(path, off)  // empty block — node position stays
    if (off < n.content.length) {
      path = [...path, off]
      off = 0
    } else {
      const last = n.content[n.content.length - 1]
      path = [...path, n.content.length - 1]
      off = isText(last) ? last.text.length : isNode(last) ? last.content.length : 0
    }
  }
  return pos(path, off)
}

// Delete the current non-collapsed text selection. Single-leaf ranges go
// through the simple path; multi-leaf / multi-block ranges (e.g. select-all)
// are handled generally as long as the two endpoints share a block parent.
function deleteSelectionSimple(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null || !selIsText(sel) || selCollapsed(sel)) return null
  const lo = resolvePos(state.doc, selLo(sel))
  const hi = resolvePos(state.doc, selHi(sel))
  if (posEqual(lo, hi)) return null  // degenerate range (a block boundary only)
  if (pathEq(lo.path, hi.path)) return deleteRangeInLeaf(state, lo, hi)
  return deleteRangeResolved(state, lo, hi)
}

// Split a block's content around a position into (before, after), and report
// the block's path. Works for a leaf position (splits the leaf) or a node
// child-index position (empty/at-boundary block).
interface BlockSplit { blockPath: SourcePath; before: Child[]; after: Child[] }
function splitBlockAt(doc: Node, p: SourcePos): BlockSplit | null {
  const target = nodeAt(doc, p.path)
  if (target === null) return null
  if (isText(target)) {
    const blockPath = parentPath(p.path)
    const leafIdx = lastIndex(p.path)
    const block = nodeAt(doc, blockPath)
    if (block === null || !isNode(block)) return null
    const leftText = target.text.slice(0, p.offset)
    const rightText = target.text.slice(p.offset)
    const before = [...block.content.slice(0, leafIdx), ...(leftText ? [{ kind: 'text' as const, text: leftText, marks: target.marks }] : [])]
    const after = [...(rightText ? [{ kind: 'text' as const, text: rightText, marks: target.marks }] : []), ...block.content.slice(leafIdx + 1)]
    return { blockPath, before, after }
  }
  if (isNode(target)) {
    return { blockPath: p.path, before: target.content.slice(0, p.offset), after: target.content.slice(p.offset) }
  }
  return null
}

function pathEq(a: SourcePath, b: SourcePath): boolean {
  return a.length === b.length && a.every((v, i) => v === b[i])
}

// General range delete over two already-resolved (within-block) positions:
// keep the start block's prefix + the end block's suffix, removing everything
// between (and the blocks themselves). The two endpoints must live under the
// same block parent (covers select-all over a flat doc and any same-level
// multi-block range).
function deleteRangeResolved(state: EditorState, loPos: SourcePos, hiPos: SourcePos): Transaction | null {
  const lo = splitBlockAt(state.doc, loPos)
  const hi = splitBlockAt(state.doc, hiPos)
  if (lo === null || hi === null) return null
  if (lo.blockPath.length === 0 || hi.blockPath.length === 0) return null  // never treat the doc root as a block
  const loParent = parentPath(lo.blockPath)
  const hiParent = parentPath(hi.blockPath)
  if (!pathEq(loParent, hiParent)) return null  // cross-parent nested range — unsupported
  const loIdx = lastIndex(lo.blockPath)
  const hiIdx = lastIndex(hi.blockPath)
  if (loIdx > hiIdx) return null
  const loBlock = nodeAt(state.doc, lo.blockPath)
  if (loBlock === null || !isNode(loBlock)) return null
  const mergedContent = mergeInlines([...lo.before, ...hi.after])
  const merged = withContent(loBlock, mergedContent)
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepReplace(loParent, loIdx, hiIdx + 1, [merged]))
  const cp = caretPosInContent([...loParent, loIdx], mergedContent, charLen(lo.before))
  return txSetSelection(tx, caret(cp))
}

// Merge `incoming` inline content into `prev`'s last inline-content descendant.
// Returns the rebuilt node plus the caret's relative path/offset at the seam.
// Descends through containers (a paragraph backspacing into a list merges into
// that list's last item). Returns null if `prev` has no mergeable descendant.
interface MergeResult { node: Node; caretRelPath: number[]; caretOffset: number }
function mergeIntoLastInline(schema: Schema, prev: Node, incoming: Child[]): MergeResult | null {
  if (schemaAllowsInline(schema, prev.tag)) {
    const seam = charLen(prev.content)
    const merged = mergeInlines([...prev.content, ...incoming])
    const cp = caretPosInContent([], merged, seam)
    return { node: withContent(prev, merged), caretRelPath: cp.path, caretOffset: cp.offset }
  }
  const n = prev.content.length
  if (n === 0) return null
  const last = prev.content[n - 1]
  if (last === undefined || !isNode(last)) return null
  const res = mergeIntoLastInline(schema, last, incoming)
  if (res === null) return null
  return {
    node: withContent(prev, [...prev.content.slice(0, n - 1), res.node]),
    caretRelPath: [n - 1, ...res.caretRelPath],
    caretOffset: res.caretOffset
  }
}

// Backspace at the very start of a block: merge it into the previous sibling
// block (PM joinBackward). Returns null at the first block (no previous sibling)
// or when the previous block has no mergeable descendant.
function joinBlockBackward(state: EditorState, blockPath: SourcePath): Transaction | null {
  const parent = parentPath(blockPath)
  const blockIdx = lastIndex(blockPath)
  if (blockIdx === 0) return null  // first block under its parent — nothing to merge into
  const cur = nodeAt(state.doc, blockPath)
  const prev = nodeAt(state.doc, [...parent, blockIdx - 1])
  if (cur === null || !isNode(cur) || prev === null || !isNode(prev)) return null
  const merged = mergeIntoLastInline(state.schema, prev, cur.content)
  if (merged === null) return null
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepReplace(parent, blockIdx - 1, blockIdx + 1, [merged.node]))
  const caretPath = [...parent, blockIdx - 1, ...merged.caretRelPath]
  return txSetSelection(tx, caret(pos(caretPath, merged.caretOffset)))
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
  const target = nodeAt(state.doc, lo.path)
  if (target === null) return null

  // Caret sits on a NODE at a child-index boundary (empty block, or after an
  // inline node like <br>). Insert a fresh text leaf carrying stored marks —
  // but ONLY where the schema permits inline content, so we never splice a
  // bare text leaf among block children (e.g. select-all + type into <doc>).
  if (isNode(target)) {
    if (txt.length === 0) return null
    if (lo.path.length < 1 || !schemaAllowsInline(state.schema, target.tag)) return null
    const off = Math.min(lo.offset, target.content.length)
    const marks: MarkDict = state.stored_marks ?? {}
    const newLeaf: TextLeaf = { kind: 'text', text: txt, marks }
    let tx = txBegin(state.doc, sel)
    tx = txStep(tx, stepReplace(lo.path, off, off, [newLeaf]))
    return txSetSelection(tx, caretAt([...lo.path, off], txt.length))
  }

  if (!isText(target)) return null
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

  // Caret on a NODE (empty block, or a child-index boundary inside a block) —
  // split the node's children at the boundary. For an empty block this just
  // inserts an empty sibling; repeated Enter accumulates blank blocks.
  const target0 = nodeAt(state.doc, p.path)
  if (target0 !== null && isNode(target0) && p.path.length >= 1) {
    const off = Math.min(p.offset, target0.content.length)
    const blockParentPath = parentPath(p.path)
    const blockIdx = lastIndex(p.path)
    const splitTag = defaultSplitBlock(state.schema, target0.tag)
    const first: Node = {
      kind: 'node', tag: target0.tag, attrs: target0.attrs, content: target0.content.slice(0, off)
    }
    const second = node(splitTag, target0.content.slice(off))
    let tx = txBegin(state.doc, sel)
    tx = txStep(tx, stepReplace(blockParentPath, blockIdx, blockIdx + 1, [first, second]))
    const secondPath = [...blockParentPath, blockIdx + 1]
    const firstChild = second.content[0] as Child | undefined
    const caretSel = firstChild !== undefined && isText(firstChild)
      ? caretAt([...secondPath, 0], 0)
      : caretAt(secondPath, 0)
    return txSetSelection(tx, caretSel)
  }

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
  const afterText = leaf.text.slice(hi.offset)
  const after = afterText.length > 0
    ? [{ kind: 'text' as const, text: afterText, marks: leaf.marks }]
    : []
  const slice = [
    ...before,
    { kind: 'node' as const, tag: 'br', attrs: [], content: [] },
    ...after
  ]
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(blockPath, leafIdx, leafIdx + 1, slice))
  const brIdx = leafIdx + before.length
  // Caret at the start of the trailing text if any, else just after the <br>.
  const caretSel = after.length > 0
    ? caretAt([...blockPath, brIdx + 1], 0)
    : caretAt(blockPath, brIdx + 1)
  return txSetSelection(tx, caretSel)
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

// Delete one unit immediately before child index `childIdx` of `blockPath`:
// the last char of the previous text leaf, the previous inline node, or — when
// `childIdx` is 0 (block start) — join into the previous block.
function deleteBeforeChild(state: EditorState, blockPath: SourcePath, childIdx: number): Transaction | null {
  if (childIdx === 0) return joinBlockBackward(state, blockPath)
  const block = nodeAt(state.doc, blockPath)
  if (block === null || !isNode(block)) return null
  const prev = block.content[childIdx - 1]
  if (prev !== undefined && isText(prev) && prev.text.length > 0) {
    const prevPath = [...blockPath, childIdx - 1]
    return deleteRangeInLeaf(state, pos(prevPath, prev.text.length - 1), pos(prevPath, prev.text.length))
  }
  // Previous child is an inline node (or an empty leaf) — remove it wholesale.
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepReplace(blockPath, childIdx - 1, childIdx, []))
  return txSetSelection(tx, caret(pos(blockPath, childIdx - 1)))
}

// Backspace at a single (collapsed) position: previous char, previous inline
// node, or join into the previous block at a block start.
function deleteBackwardAt(state: EditorState, p: SourcePos): Transaction | null {
  const target = nodeAt(state.doc, p.path)
  if (target === null) return null
  if (isText(target)) {
    if (p.offset > 0) return deleteRangeInLeaf(state, pos(p.path, p.offset - 1), p)
    return deleteBeforeChild(state, parentPath(p.path), lastIndex(p.path))
  }
  return deleteBeforeChild(state, p.path, p.offset)
}

export function cmdDeleteBackward(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null) return null
  if (sel.kind === 'node') return cmdDeleteNode(state)
  if (!selIsText(sel)) return null
  // Resolve endpoints so a selection that spans only a block boundary collapses
  // to a single point (→ join) instead of corrupting the tree as a range.
  const lo = resolvePos(state.doc, selLo(sel))
  const hi = resolvePos(state.doc, selHi(sel))
  if (!posEqual(lo, hi)) return deleteRangeResolved(state, lo, hi)
  return deleteBackwardAt(state, lo)
}

// Symmetric to mergeIntoLastInline: pull the FIRST inline-content run out of
// `node`, descending through containers. `remainder` is `node` with that run
// removed (null when `node` is wholly consumed).
interface PullResult { inline: Child[]; remainder: Node | null }
function pullFirstInline(schema: Schema, node: Node): PullResult | null {
  if (schemaAllowsInline(schema, node.tag)) {
    return { inline: node.content, remainder: null }
  }
  if (node.content.length === 0) return null
  const first = node.content[0]
  if (first === undefined || !isNode(first)) return null
  const res = pullFirstInline(schema, first)
  if (res === null) return null
  const rest = res.remainder === null ? node.content.slice(1) : [res.remainder, ...node.content.slice(1)]
  return { inline: res.inline, remainder: rest.length === 0 ? null : withContent(node, rest) }
}

// Forward delete at a block end: merge the NEXT sibling block's first inline run
// into this block (PM joinForward). Symmetric to joinBlockBackward.
function joinBlockForward(state: EditorState, blockPath: SourcePath): Transaction | null {
  const parent = parentPath(blockPath)
  const parentNode = nodeAt(state.doc, parent)
  const blockIdx = lastIndex(blockPath)
  const block = nodeAt(state.doc, blockPath)
  if (block === null || !isNode(block) || parentNode === null || !isNode(parentNode)) return null
  if (!schemaAllowsInline(state.schema, block.tag)) return null  // can only pull inline into an inline block
  if (blockIdx >= parentNode.content.length - 1) return null     // last block — nothing after
  const next = parentNode.content[blockIdx + 1]
  if (next === undefined || !isNode(next)) return null
  const pulled = pullFirstInline(state.schema, next)
  if (pulled === null) return null
  const seam = charLen(block.content)
  const mergedContent = mergeInlines([...block.content, ...pulled.inline])
  const merged = withContent(block, mergedContent)
  const replacement = pulled.remainder === null ? [merged] : [merged, pulled.remainder]
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepReplace(parent, blockIdx, blockIdx + 2, replacement))
  const cp = caretPosInContent(blockPath, mergedContent, seam)
  return txSetSelection(tx, caret(cp))
}

// Delete one unit forward at child index `childIdx` of `blockPath`: first char of
// the next text leaf, the next inline node, or — at the block end — join the next block.
function deleteAfterChild(state: EditorState, blockPath: SourcePath, childIdx: number): Transaction | null {
  const block = nodeAt(state.doc, blockPath)
  if (block === null || !isNode(block)) return null
  if (childIdx >= block.content.length) return joinBlockForward(state, blockPath)
  const next = block.content[childIdx]
  if (next !== undefined && isText(next) && next.text.length > 0) {
    const nextPath = [...blockPath, childIdx]
    return deleteRangeInLeaf(state, pos(nextPath, 0), pos(nextPath, 1))
  }
  // Next child is an inline node (e.g. <br>) or an empty leaf — remove it,
  // merging any now-adjacent same-mark leaves so block content stays canonical.
  const before = block.content.slice(0, childIdx)
  const newContent = mergeInlines([...before, ...block.content.slice(childIdx + 1)])
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepReplace(blockPath, 0, block.content.length, newContent))
  const cp = caretPosInContent(blockPath, newContent, charLen(before))
  return txSetSelection(tx, caret(cp))
}

function deleteForwardAt(state: EditorState, p: SourcePos): Transaction | null {
  const target = nodeAt(state.doc, p.path)
  if (target === null) return null
  if (isText(target)) {
    if (p.offset < target.text.length) return deleteRangeInLeaf(state, p, pos(p.path, p.offset + 1))
    return deleteAfterChild(state, parentPath(p.path), lastIndex(p.path) + 1)
  }
  return deleteAfterChild(state, p.path, p.offset)
}

export function cmdDeleteForward(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null) return null
  if (sel.kind === 'node') return cmdDeleteNode(state)
  if (!selIsText(sel)) return null
  const lo = resolvePos(state.doc, selLo(sel))
  const hi = resolvePos(state.doc, selHi(sel))
  if (!posEqual(lo, hi)) return deleteRangeResolved(state, lo, hi)
  return deleteForwardAt(state, lo)
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

