// Geometric hit-test per shape kind. Used by the select tool to find which
// shape (if any) is under a pointer position. Z-order: later children win,
// matching SVG painting order.

import { isNode } from '../model/doc.js'
import type { Child, Node, SourcePath } from '../model/types.js'
import {
  pointInEllipse,
  pointInPolygon,
  pointToSegmentDist,
  rectContains,
  rectCenter,
  rotatePoint,
  type Rect,
  type Vec2
} from './geom.js'
import {
  getLineEndpoints,
  getShapeAttrNumber,
  getShapeAttrString,
  getShapeBbox,
  getShapeKind,
  parsePoints
} from './shape-utils.js'

const HIT_TOLERANCE = 4  // px

export interface HitResult {
  kind: 'shape' | 'connector' | 'empty'
  path: SourcePath
  shape_id: string | null
}

// Hit-test a single shape against a point in drawing-local coords.
export function hitTestShape(shape: Node, p: Vec2): boolean {
  const kind = getShapeKind(shape)
  if (kind === null) return false
  const bbox = getShapeBbox(shape)
  if (bbox === null) return false

  // Apply rotation: rotate the test point in the OPPOSITE direction around
  // the shape's center, then test against the unrotated geometry.
  const rotateAngle = getShapeAttrNumber(shape, 'rotate', 0)
  const pp = rotateAngle === 0 ? p : rotatePoint(p, rectCenter(bbox), -rotateAngle)

  switch (kind) {
    case 'rect':
      return rectContains(bbox, pp)
    case 'ellipse':
      return pointInEllipse(pp, bbox)
    case 'line': {
      const { a, b } = getLineEndpoints(shape)
      return pointToSegmentDist(pp, a, b) <= HIT_TOLERANCE
    }
    case 'polyline':
    case 'path':
    case 'freehand': {
      const points = parsePoints(getShapeAttrString(shape, 'points', ''))
      if (points.length === 0) return false
      for (let i = 0; i + 1 < points.length; i++) {
        if (pointToSegmentDist(pp, points[i]!, points[i + 1]!) <= HIT_TOLERANCE) return true
      }
      return false
    }
    case 'polygon': {
      const points = parsePoints(getShapeAttrString(shape, 'points', ''))
      return points.length >= 3 ? pointInPolygon(pp, points) : false
    }
    case 'image':
      return rectContains(bbox, pp)
    default:
      return false
  }
}

// Hit-test a drawing tree against a point. Walks layers + groups top-down,
// returning the topmost hit.
export function hitTestDrawing(drawing: Node, p: Vec2): HitResult {
  // Iterate layers from last to first (top of z-stack first)
  for (let li = drawing.content.length - 1; li >= 0; li--) {
    const layer = drawing.content[li]
    if (!isNode(layer) || layer.tag !== 'layer') continue
    if (getShapeAttrString(layer, 'visible', 'true') === 'false') continue
    const r = hitTestContainer(layer, [li], p)
    if (r !== null) return r
  }
  return { kind: 'empty', path: [], shape_id: null }
}

function hitTestContainer(container: Node, path: SourcePath, p: Vec2): HitResult | null {
  // Iterate children in reverse z-order (last child = topmost)
  for (let i = container.content.length - 1; i >= 0; i--) {
    const c = container.content[i] as Child
    if (!isNode(c)) continue
    const childPath: SourcePath = [...path, i]
    if (c.tag === 'shape') {
      if (hitTestShape(c, p)) {
        return { kind: 'shape', path: childPath, shape_id: getShapeAttrString(c, 'id', '') || null }
      }
    } else if (c.tag === 'group') {
      const r = hitTestContainer(c, childPath, p)
      if (r !== null) return r
    } else if (c.tag === 'connector') {
      // Connectors are tested via route polyline; the router computes the
      // path. Simpler test for now: if the connector's bbox contains p we
      // consider it a hit. (Improved routing-aware hit-test lives in router.)
      // Skipped here — see hitTestConnector below.
      void c
    } else if (c.tag === 'text-frame') {
      const bbox = textFrameBbox(c)
      if (bbox !== null && rectContains(bbox, p)) {
        return { kind: 'shape', path: childPath, shape_id: getShapeAttrString(c, 'id', '') || null }
      }
    }
  }
  return null
}

function textFrameBbox(tf: Node): Rect | null {
  return getShapeBbox(tf)
}
