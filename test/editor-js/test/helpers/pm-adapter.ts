// ProseMirror integer-position ↔ Slate-path adapter.
//
// PM tests express positions as integers into a flat token stream where:
//   - entering / leaving a non-text node each contribute 1 token
//   - each character of a text leaf is 1 token
//   - the root doc has no open / close tokens (it IS the root)
//
// Example for `doc(p("hi"))` (PM size = 4):
//   pos 0 = before everything                → { path: [],     offset: 0 }
//   pos 1 = inside <p>, before 'h'           → { path: [0, 0], offset: 0 }
//   pos 2 = after 'h'                        → { path: [0, 0], offset: 1 }
//   pos 3 = after 'i' (end of text)          → { path: [0, 0], offset: 2 }
//   pos 4 = after <p> in <doc>               → { path: [],     offset: 1 }
//
// This adapter lets us port PM transform/state/commands tests verbatim by
// translating `doc.tag.a` (int) → our SourcePos for the events.json payload.

import { isText } from '../../src/model/doc.js'
import type { Child, Doc, SourcePath, SourcePos } from '../../src/model/types.js'

// ---------------------------------------------------------------------------
// pmPosToSourcePos
// ---------------------------------------------------------------------------

export function pmPosToSourcePos(doc: Doc, target: number): SourcePos | null {
  const ctx = { pos: 0, result: null as SourcePos | null }
  walkForPmPos(doc, [], target, ctx, /*isRoot*/ true)
  return ctx.result
}

interface WalkCtx { pos: number; result: SourcePos | null }

function walkForPmPos(node: Child, path: SourcePath, target: number, ctx: WalkCtx, isRoot: boolean): void {
  if (ctx.result !== null) return
  if (isText(node)) {
    // Each character advances pos. A position at offset O sits AFTER the
    // O-th character (0-indexed).
    if (ctx.pos === target) {
      ctx.result = { path, offset: 0 }
      return
    }
    for (let i = 1; i <= node.text.length; i++) {
      ctx.pos++
      if (ctx.pos === target) {
        ctx.result = { path, offset: i }
        return
      }
    }
    return
  }

  // Non-text node — entering counts as 1 token (except for the root doc)
  if (!isRoot) {
    if (ctx.pos === target) {
      // boundary "before this node" in its parent — but the parent's loop
      // captured that already; this branch is unreachable from the root walk.
      return
    }
    ctx.pos++
  }

  // Iterate children. Position BEFORE child i is (after the open-token of
  // the parent + sum of sizes of preceding children).
  for (let i = 0; i < node.content.length; i++) {
    if (ctx.pos === target) {
      // Prefer a leaf anchor when the next child is a text leaf — matches the
      // html-parser's preferLeafAnchor normalization.
      const next = node.content[i] as Child
      if (isText(next)) {
        ctx.result = { path: [...path, i], offset: 0 }
      } else {
        ctx.result = { path, offset: i }
      }
      return
    }
    walkForPmPos(node.content[i] as Child, [...path, i], target, ctx, false)
    if (ctx.result !== null) return
  }

  // After all children, before the close token.
  if (ctx.pos === target) {
    // Prefer attaching to the last text leaf at its end.
    const last = node.content[node.content.length - 1]
    if (last !== undefined && isText(last)) {
      ctx.result = { path: [...path, node.content.length - 1], offset: last.text.length }
    } else {
      ctx.result = { path, offset: node.content.length }
    }
    return
  }
  if (!isRoot) {
    ctx.pos++  // leave token
  }
}

// ---------------------------------------------------------------------------
// sourcePosToPmPos (inverse)
// ---------------------------------------------------------------------------

export function sourcePosToPmPos(doc: Doc, pos: SourcePos): number | null {
  const ctx = { pos: 0, found: null as number | null }
  walkForSourcePos(doc, [], pos, ctx, true)
  return ctx.found
}

interface SrcWalkCtx { pos: number; found: number | null }

function pathEqual(a: SourcePath, b: SourcePath): boolean {
  if (a.length !== b.length) return false
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false
  return true
}

function walkForSourcePos(node: Child, path: SourcePath, target: SourcePos, ctx: SrcWalkCtx, isRoot: boolean): void {
  if (ctx.found !== null) return
  if (isText(node)) {
    if (pathEqual(path, target.path)) {
      ctx.found = ctx.pos + target.offset
      return
    }
    ctx.pos += node.text.length
    return
  }
  if (!isRoot) ctx.pos++  // enter
  if (pathEqual(path, target.path)) {
    // target is at child boundary `target.offset` within THIS non-text node
    let acc = ctx.pos
    for (let i = 0; i < target.offset; i++) {
      acc += sizeOf(node.content[i] as Child)
    }
    ctx.found = acc
    return
  }
  for (let i = 0; i < node.content.length; i++) {
    walkForSourcePos(node.content[i] as Child, [...path, i], target, ctx, false)
    if (ctx.found !== null) return
  }
  if (!isRoot) ctx.pos++  // leave
}

function sizeOf(c: Child): number {
  if (isText(c)) return c.text.length
  // non-text: enter + (sum of child sizes) + leave
  let s = 2  // open + close
  for (const child of c.content) s += sizeOf(child)
  return s
}
