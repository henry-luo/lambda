// Helpers for reading shape attributes and translating to geometry / SVG.
// Centralized so commands, hit-test, and the renderer all agree on the
// kind→geometry mapping.

import { attrsGet, isNode, nodeAt } from '../model/doc.js'
import type { Doc, Node, SourcePath } from '../model/types.js'
import { rect, type Rect, type Vec2, v2, rectFromPoints } from './geom.js'

export type ShapeKind = 'rect' | 'ellipse' | 'line' | 'polyline' | 'polygon' | 'path' | 'freehand' | 'image'

export function getShapeKind(shape: Node): ShapeKind | null {
  if (shape.tag !== 'shape') return null
  const k = attrsGet(shape.attrs, 'kind')
  if (typeof k !== 'string') return null
  return k as ShapeKind
}

export function getShapeAttrNumber(shape: Node, name: string, def = 0): number {
  const v = attrsGet(shape.attrs, name)
  return typeof v === 'number' ? v : def
}

export function getShapeAttrString(shape: Node, name: string, def = ''): string {
  const v = attrsGet(shape.attrs, name)
  return typeof v === 'string' ? v : def
}

export function getShapeId(shape: Node): string | null {
  return getShapeAttrString(shape, 'id', '') || null
}

// Axis-aligned bounding box of a shape (rotation NOT applied).
export function getShapeBbox(shape: Node): Rect | null {
  const kind = getShapeKind(shape)
  if (kind === null) return null
  if (kind === 'polyline' || kind === 'polygon' || kind === 'path' || kind === 'freehand') {
    const x = getShapeAttrNumber(shape, 'x', 0)
    const y = getShapeAttrNumber(shape, 'y', 0)
    const w = getShapeAttrNumber(shape, 'width', 0)
    const h = getShapeAttrNumber(shape, 'height', 0)
    return rect(x, y, w, h)
  }
  if (kind === 'image' || kind === 'rect' || kind === 'ellipse' || kind === 'line') {
    const x = getShapeAttrNumber(shape, 'x', 0)
    const y = getShapeAttrNumber(shape, 'y', 0)
    const w = getShapeAttrNumber(shape, 'width', 0)
    const h = getShapeAttrNumber(shape, 'height', 0)
    return rect(x, y, w, h)
  }
  return null
}

// Parse polyline "x1,y1 x2,y2 ..." into Vec2[].
export function parsePoints(s: string): Vec2[] {
  const out: Vec2[] = []
  for (const pair of s.trim().split(/\s+/)) {
    const [xs, ys] = pair.split(',')
    if (xs === undefined || ys === undefined) continue
    const x = parseFloat(xs)
    const y = parseFloat(ys)
    if (Number.isFinite(x) && Number.isFinite(y)) out.push(v2(x, y))
  }
  return out
}

// Line endpoints from a 'line' shape: defaults to bbox top-left → bottom-right.
export function getLineEndpoints(shape: Node): { a: Vec2; b: Vec2 } {
  const x = getShapeAttrNumber(shape, 'x', 0)
  const y = getShapeAttrNumber(shape, 'y', 0)
  const w = getShapeAttrNumber(shape, 'width', 0)
  const h = getShapeAttrNumber(shape, 'height', 0)
  return { a: v2(x, y), b: v2(x + w, y + h) }
}

// ---------------------------------------------------------------------------
// Doc traversal — find by shape id
// ---------------------------------------------------------------------------

export function findShapeById(doc: Doc, id: string): { path: SourcePath; shape: Node } | null {
  function walk(n: Node, path: SourcePath): { path: SourcePath; shape: Node } | null {
    if ((n.tag === 'shape' || n.tag === 'group' || n.tag === 'connector' || n.tag === 'text-frame') &&
        getShapeAttrString(n, 'id', '') === id) {
      return { path, shape: n }
    }
    for (let i = 0; i < n.content.length; i++) {
      const c = n.content[i]!
      if (isNode(c)) {
        const r = walk(c, [...path, i])
        if (r !== null) return r
      }
    }
    return null
  }
  return walk(doc, [])
}

// Lookup the shape at a given path, or null if missing/wrong tag.
export function getShapeAtPath(doc: Doc, path: SourcePath): Node | null {
  const n = nodeAt(doc, path)
  if (n === null || !isNode(n)) return null
  return n
}

// Bbox of two points (used by tool drag-to-create).
export { rectFromPoints }
