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
import { isBlockTag, schemaAllowsInline } from '../model/schema.js'
import type { Schema } from '../model/schema.js'
import { pos } from '../model/source-pos.js'
import type { SourcePos } from '../model/types.js'
import { caretAt, isText as selIsText, selHi, selLo } from './sel.js'
import { cmdDeleteBackward } from './text-commands.js'
import { txBegin, txStep, txSetSelection } from '../model/transaction.js'
import type { Child, Node, SourcePath } from '../model/types.js'
import type { EditorState } from './types.js'
import type { Transaction } from '../model/types.js'

type Slice =
  | { kind: 'inline'; inlines: Child[] }
  | { kind: 'block'; blocks: Node[] }

// Group consecutive bare top-level <li> nodes (copied list items that lost their
// container) into a single <ul>, so a list paste reconstructs a real list.
function wrapBareListItems(blocks: Node[]): Node[] {
  const out: Node[] = []
  for (const b of blocks) {
    if (b.tag === 'li') {
      const prev = out[out.length - 1]
      if (prev !== undefined && prev.tag === 'ul') out[out.length - 1] = withContent(prev, [...prev.content, b])
      else out.push(node('ul', [b]))
    } else {
      out.push(b)
    }
  }
  return out
}

// Classify a parsed fragment; wrap stray top-level inlines into paragraphs when
// the fragment also contains blocks, and wrap bare <li>s in a <ul>.
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
  const wrapped = wrapBareListItems(blocks)
  return wrapped.length ? { kind: 'block', blocks: wrapped } : null
}

// Position at the end of a node's last text leaf (descends; node position if empty).
function deepEndPos(n: Child, basePath: SourcePath): SourcePos {
  if (isText(n)) return pos(basePath, n.text.length)
  if (isNode(n) && n.content.length > 0) {
    const li = n.content.length - 1
    return deepEndPos(n.content[li] as Child, [...basePath, li])
  }
  return pos(basePath, 0)
}

export function cmdPasteSlice(state: EditorState, slice: Child[]): Transaction | null {
  const sel = state.selection
  if (sel === null || !selIsText(sel) || slice.length === 0) return null
  const lo = selLo(sel)
  const hi = selHi(sel)
  // A multi-leaf / cross-block target range: delete it first, then paste at the
  // resulting caret. (Caret / within-one-leaf ranges fall through to the surgery below.)
  if (lo.path.length !== hi.path.length || !lo.path.every((v, i) => v === hi.path[i])) {
    const del = cmdDeleteBackward(state)
    if (del === null) return null
    const pasted = cmdPasteSlice({ ...state, doc: del.doc_after, selection: del.sel_after }, slice)
    if (pasted === null) return del
    return {
      doc_before: state.doc, doc_after: pasted.doc_after,
      steps: [...del.steps, ...pasted.steps],
      sel_before: state.selection, sel_after: pasted.sel_after, meta: []
    }
  }

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
  const allowsT = schemaAllowsInline(state.schema, block.tag)
  const first = blocks[0] as Node
  const last = blocks[blocks.length - 1] as Node
  // Only inline-content blocks merge into the surrounding paragraph halves;
  // container blocks (ul/ol/table/blockquote) are inserted as siblings.
  const mergeFirst = allowsT && schemaAllowsInline(state.schema, first.tag)
  const mergeLast = blocks.length > 1 && allowsT && schemaAllowsInline(state.schema, last.tag)

  // Fast path: a single inline-content block merges inline, no split (p/h/li).
  if (blocks.length === 1 && mergeFirst) {
    const content = mergeInlines([...prefix, ...first.content, ...suffix])
    tx = txStep(tx, stepReplace(blockParent, blockIdx, blockIdx + 1, [withContent(block, content)]))
    const cp = caretPosInContent(blockPath, content, charLen(prefix) + charLen(first.content))
    return txSetSelection(tx, caretAt(cp.path, cp.offset))
  }

  // General open paste: split current block into head (prefix) + tail (suffix);
  // merge inline-content first/last blocks into the halves; insert the rest as siblings.
  const headContent = mergeFirst ? mergeInlines([...prefix, ...first.content]) : prefix
  const middle = blocks.slice(mergeFirst ? 1 : 0, mergeLast ? blocks.length - 1 : blocks.length)
  const repl: Node[] = []
  if (headContent.length > 0) repl.push(withContent(block, headContent))
  for (const b of middle) repl.push(b)
  if (mergeLast) repl.push(withContent(last, mergeInlines([...last.content, ...suffix])))
  else if (suffix.length > 0) repl.push(withContent(block, suffix))
  if (repl.length === 0) return null

  tx = txStep(tx, stepReplace(blockParent, blockIdx, blockIdx + 1, repl))
  const lastReplPath = [...blockParent, blockIdx + repl.length - 1]
  const lastRepl = repl[repl.length - 1] as Node
  let cp: SourcePos
  if (mergeLast) cp = caretPosInContent(lastReplPath, lastRepl.content, charLen(last.content))
  else if (suffix.length > 0) cp = caretPosInContent(lastReplPath, lastRepl.content, 0)
  else cp = deepEndPos(lastRepl, lastReplPath)
  return txSetSelection(tx, caretAt(cp.path, cp.offset))
}
