// Paste — insert an already-parsed slice (Child[]) at the selection.
//
// HTML→slice parsing lives at the view/event boundary (it needs DOMParser);
// this command takes the parsed slice and does the pure model surgery, so the
// model layer stays free of any DOM dependency.
//
// Behaviour (a pragmatic PM-style "open paste"):
//   - inline slice (no block nodes) → splice the inline leaves at the caret.
//   - block slice → the current block is split at the caret; the first pasted
//     block merges into the head, middle blocks are inserted as siblings, and
//     the trailing content merges into the last pasted block.
// A non-collapsed selection within a single leaf is deleted first. Multi-leaf
// ranges and node/doc-root positions are not handled (returns null).

import { isNode, isText, node, nodeAt, parentPath, lastIndex, withContent } from '../model/doc.js'
import { stepReplace } from '../model/step.js'
import { charLen, caretPosInContent, mergeInlines } from '../model/inline.js'
import { isBlockTag } from '../model/schema.js'
import type { Schema } from '../model/schema.js'
import { caretAt, isText as selIsText, selHi, selLo } from './sel.js'
import { txBegin, txStep, txSetSelection } from '../model/transaction.js'
import type { Child, Node, SourcePath } from '../model/types.js'
import type { EditorState } from './types.js'
import type { Transaction } from '../model/types.js'

type Slice =
  | { kind: 'inline'; inlines: Child[] }
  | { kind: 'block'; blocks: Node[] }

// Classify a parsed fragment; wrap stray top-level inlines into paragraphs when
// the fragment also contains blocks.
function normalizeSlice(slice: Child[], schema: Schema): Slice | null {
  const hasBlock = slice.some(c => isNode(c) && isBlockTag(schema, c.tag))
  if (!hasBlock) return { kind: 'inline', inlines: slice }
  const blocks: Node[] = []
  let pending: Child[] = []
  const flush = (): void => { if (pending.length) { blocks.push(node('p', pending)); pending = [] } }
  for (const c of slice) {
    if (isNode(c) && isBlockTag(schema, c.tag)) { flush(); blocks.push(c) }
    else pending.push(c)
  }
  flush()
  return blocks.length ? { kind: 'block', blocks } : null
}

export function cmdPasteSlice(state: EditorState, slice: Child[]): Transaction | null {
  const sel = state.selection
  if (sel === null || !selIsText(sel) || slice.length === 0) return null
  const lo = selLo(sel)
  const hi = selHi(sel)
  // Only a caret or a range within ONE leaf/node is handled.
  if (lo.path.length !== hi.path.length || !lo.path.every((v, i) => v === hi.path[i])) return null

  const norm = normalizeSlice(slice, state.schema)
  if (norm === null) return null

  const target = nodeAt(state.doc, lo.path)
  if (target === null) return null

  // Resolve the containing block plus the content before/after the caret.
  let blockPath: SourcePath, block: Node, prefix: Child[], suffix: Child[]
  if (isText(target)) {
    if (lo.path.length < 2) return null
    blockPath = parentPath(lo.path)
    const leafIdx = lastIndex(lo.path)
    block = nodeAt(state.doc, blockPath) as Node
    const leftText = target.text.slice(0, lo.offset)
    const rightText = target.text.slice(hi.offset)
    const left: Child[] = leftText ? [{ kind: 'text', text: leftText, marks: target.marks }] : []
    const right: Child[] = rightText ? [{ kind: 'text', text: rightText, marks: target.marks }] : []
    prefix = [...block.content.slice(0, leafIdx), ...left]
    suffix = [...right, ...block.content.slice(leafIdx + 1)]
  } else if (isNode(target)) {
    blockPath = lo.path
    block = target
    prefix = block.content.slice(0, lo.offset)
    suffix = block.content.slice(lo.offset)
  } else {
    return null
  }
  if (blockPath.length < 1) return null  // never paste at the doc root position

  const blockParent = parentPath(blockPath)
  const blockIdx = lastIndex(blockPath)
  let tx = txBegin(state.doc, sel)

  if (norm.kind === 'inline') {
    const content = mergeInlines([...prefix, ...norm.inlines, ...suffix])
    const newBlock = withContent(block, content)
    tx = txStep(tx, stepReplace(blockParent, blockIdx, blockIdx + 1, [newBlock]))
    const cp = caretPosInContent(blockPath, content, charLen(prefix) + charLen(norm.inlines))
    return txSetSelection(tx, caretAt(cp.path, cp.offset))
  }

  const blocks = norm.blocks
  if (blocks.length === 1) {
    const content = mergeInlines([...prefix, ...(blocks[0] as Node).content, ...suffix])
    const newBlock = withContent(block, content)
    tx = txStep(tx, stepReplace(blockParent, blockIdx, blockIdx + 1, [newBlock]))
    const cp = caretPosInContent(blockPath, content, charLen(prefix) + charLen((blocks[0] as Node).content))
    return txSetSelection(tx, caretAt(cp.path, cp.offset))
  }

  // Multi-block: split the current block; merge first/last pasted blocks.
  const first = blocks[0] as Node
  const lastSrc = blocks[blocks.length - 1] as Node
  const head = withContent(block, mergeInlines([...prefix, ...first.content]))
  const lastContent = mergeInlines([...lastSrc.content, ...suffix])
  const last = withContent(lastSrc, lastContent)
  const replacement = [head, ...blocks.slice(1, -1), last]
  tx = txStep(tx, stepReplace(blockParent, blockIdx, blockIdx + 1, replacement))
  const lastBlockPath = [...blockParent, blockIdx + replacement.length - 1]
  const cp = caretPosInContent(lastBlockPath, lastContent, charLen(lastSrc.content))
  return txSetSelection(tx, caretAt(cp.path, cp.offset))
}
