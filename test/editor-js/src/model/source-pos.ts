// Port of lambda/package/editor/mod_source_pos.ls
//
// SourcePath = number[]
// SourcePos  = { path: SourcePath, offset: number }
// Selection  = TextSelection | NodeSelection | AllSelection | MultiNodeSelection
//
// All operations are pure; selections and positions are plain JSON values.

import { isNode, isText } from './doc.js'
import type {
  Child,
  Doc,
  MultiNodeSelection,
  NodeSelection,
  ResolvedAncestor,
  ResolvedPos,
  Selection,
  SourcePath,
  SourcePos,
  TextSelection
} from './types.js'

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

export function pos(path: SourcePath, offset: number): SourcePos {
  return { path, offset }
}

export function textSelection(anchor: SourcePos, head: SourcePos): TextSelection {
  return { kind: 'text', anchor, head }
}

export function nodeSelection(path: SourcePath): NodeSelection {
  return { kind: 'node', path }
}

export function multiNodeSelection(paths: SourcePath[]): MultiNodeSelection {
  return { kind: 'multi-node', paths }
}

// ---------------------------------------------------------------------------
// Path comparison
// ---------------------------------------------------------------------------

// Lexicographic compare; -1 if `a` appears earlier in document order, +1 later.
// Shorter path with matching prefix is the ancestor and compares less.
export function pathCompare(a: SourcePath, b: SourcePath): number {
  const n = Math.min(a.length, b.length)
  for (let i = 0; i < n; i++) {
    const ai = a[i] as number
    const bi = b[i] as number
    if (ai < bi) return -1
    if (ai > bi) return 1
  }
  if (a.length === b.length) return 0
  return a.length < b.length ? -1 : 1
}

export function pathEqual(a: SourcePath, b: SourcePath): boolean {
  return pathCompare(a, b) === 0
}

// True iff `prefix` is a (possibly equal) prefix of `path`.
export function pathIsPrefix(prefix: SourcePath, path: SourcePath): boolean {
  if (prefix.length > path.length) return false
  for (let i = 0; i < prefix.length; i++) {
    if (prefix[i] !== path[i]) return false
  }
  return true
}

// ---------------------------------------------------------------------------
// Position comparison
// ---------------------------------------------------------------------------

export function posCompare(a: SourcePos, b: SourcePos): number {
  const pc = pathCompare(a.path, b.path)
  if (pc !== 0) return pc
  if (a.offset < b.offset) return -1
  if (a.offset > b.offset) return 1
  return 0
}

export function posEqual(a: SourcePos, b: SourcePos): boolean {
  return posCompare(a, b) === 0
}

export function posMin(a: SourcePos, b: SourcePos): SourcePos {
  return posCompare(a, b) <= 0 ? a : b
}

export function posMax(a: SourcePos, b: SourcePos): SourcePos {
  return posCompare(a, b) >= 0 ? a : b
}

// ---------------------------------------------------------------------------
// Resolution — walk a path against a document tree
// ---------------------------------------------------------------------------

function treeChildCount(n: Child | null): number {
  if (n !== null && isNode(n)) return n.content.length
  return 0
}

function treeChild(n: Child | null, index: number): Child | null {
  if (n !== null && isNode(n)) return (n.content[index] as Child) ?? null
  return null
}

function treeText(n: Child | null): string | null {
  if (n !== null && isText(n)) return n.text
  return null
}

function resolveAt(n: Child | null, path: SourcePath, i: number): Child | null {
  if (n === null) return null
  if (i >= path.length) return n
  if (treeChildCount(n) === 0) return null
  const idx = path[i] as number
  if (idx >= treeChildCount(n)) return null
  return resolveAt(treeChild(n, idx), path, i + 1)
}

function resolvedAncestors(doc: Doc, path: SourcePath): ResolvedAncestor[] {
  const acc: ResolvedAncestor[] = []
  for (let depth = 0; depth <= path.length; depth++) {
    const p = path.slice(0, depth)
    const n = resolveAt(doc, p, 0)
    const idx = depth === 0 ? -1 : (p[depth - 1] as number)
    acc.push({ path: p, node: n, index: idx })
  }
  return acc
}

export function resolvePos(doc: Doc, p: SourcePos): ResolvedPos {
  const node_ = resolveAt(doc, p.path, 0)
  if (node_ === null) {
    return { node: null, parent: null, parent_index: -1, depth: p.path.length, ancestors: [], found: false }
  }
  if (p.path.length === 0) {
    return { node: doc, parent: null, parent_index: -1, depth: 0, ancestors: resolvedAncestors(doc, p.path), found: true }
  }
  const parentP = p.path.slice(0, p.path.length - 1)
  const parent = resolveAt(doc, parentP, 0)
  const pi = p.path[p.path.length - 1] as number
  return {
    node: node_,
    parent,
    parent_index: pi,
    depth: p.path.length,
    ancestors: resolvedAncestors(doc, p.path),
    found: true
  }
}

// Position immediately before the ancestor at `depth` within its parent.
export function resolveBefore(r: ResolvedPos, depth: number): SourcePos | null {
  if (!r.found || depth < 0 || depth > r.depth) return null
  if (depth === 0) return pos([], 0)
  const p = (r.ancestors[depth] as ResolvedAncestor).path
  return pos(p.slice(0, p.length - 1), p[p.length - 1] as number)
}

// Position immediately after the ancestor at `depth` within its parent.
export function resolveAfter(r: ResolvedPos, depth: number): SourcePos | null {
  if (!r.found || depth < 0 || depth > r.depth) return null
  if (depth === 0) return pos([], treeChildCount((r.ancestors[0] as ResolvedAncestor).node))
  const p = (r.ancestors[depth] as ResolvedAncestor).path
  return pos(p.slice(0, p.length - 1), (p[p.length - 1] as number) + 1)
}

// ---------------------------------------------------------------------------
// Text extraction across a selection
// ---------------------------------------------------------------------------

interface TextLeafEntry {
  path: SourcePath
  text: string
}

function textLeavesAt(n: Child, path: SourcePath, acc: TextLeafEntry[]): void {
  const t = treeText(n)
  if (t !== null) {
    acc.push({ path, text: t })
    return
  }
  if (isNode(n)) {
    for (let i = 0; i < n.content.length; i++) {
      textLeavesAt(n.content[i] as Child, [...path, i], acc)
    }
  }
}

function textLeaves(doc: Doc): TextLeafEntry[] {
  const acc: TextLeafEntry[] = []
  textLeavesAt(doc, [], acc)
  return acc
}

function selectionLeafText(leaf: TextLeafEntry, lo: SourcePos, hi: SourcePos): string {
  if (pathEqual(leaf.path, lo.path)) return leaf.text.slice(lo.offset)
  if (pathEqual(leaf.path, hi.path)) return leaf.text.slice(0, hi.offset)
  return leaf.text
}

function selectionTextAcrossLeaves(doc: Doc, lo: SourcePos, hi: SourcePos): string {
  const leaves = textLeaves(doc)
  let out = ''
  for (const leaf of leaves) {
    if (pathCompare(leaf.path, lo.path) < 0) continue
    if (pathCompare(leaf.path, hi.path) > 0) break
    out += selectionLeafText(leaf, lo, hi)
  }
  return out
}

export function nodeText(n: Child): string {
  const t = treeText(n)
  if (t !== null) return t
  if (isNode(n)) {
    let out = ''
    for (const c of n.content) out += nodeText(c)
    return out
  }
  return ''
}

export function selectionToString(doc: Doc, sel: Selection): string {
  if (sel.kind === 'node') {
    const r = resolvePos(doc, pos(sel.path, 0))
    return r.found && r.node !== null ? nodeText(r.node) : ''
  }
  if (sel.kind === 'multi-node') {
    let out = ''
    for (const p of sel.paths) {
      const r = resolvePos(doc, pos(p, 0))
      if (r.found && r.node !== null) out += nodeText(r.node)
    }
    return out
  }
  // text selection (covers "select all" too — anchor/head sit at doc boundaries)
  const lo = posMin(sel.anchor, sel.head)
  const hi = posMax(sel.anchor, sel.head)
  if (pathEqual(lo.path, hi.path)) {
    const r = resolvePos(doc, lo)
    if (r.found && r.node !== null) {
      const t = treeText(r.node)
      if (t !== null) return t.slice(lo.offset, hi.offset)
      // Non-leaf same-path range — lo/hi.offset are CHILD INDICES into
      // r.node.content. Concatenate the selected children's nodeText.
      if (isNode(r.node)) {
        let out = ''
        for (let i = lo.offset; i < hi.offset && i < r.node.content.length; i++) {
          out += nodeText(r.node.content[i] as Child)
        }
        return out
      }
    }
    return ''
  }
  return selectionTextAcrossLeaves(doc, lo, hi)
}
