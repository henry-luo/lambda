// Connector routing.
//
// `computeRoute(connector, doc)` returns the polyline (Vec2[]) the connector
// should draw. Three strategies:
//
//   'straight'   — [from, to]
//   'orthogonal' — right-angled path with a single bend (L-route). When the
//                   connector's endpoints would land inside an endpoint shape's
//                   bbox, a Z-route (two bends) is used to escape.
//   'curved'     — orthogonal route with quarter-arc fillets at each bend.
//
// User waypoints (in connector.attrs `waypoints`) are pinned through-points.
// Between consecutive waypoints the routing strategy applies independently.
//
// This is a focused port of the parts of maxGraph's `mxEdgeStyle` needed for
// the Stage-4 MVP; full obstacle avoidance + port-side selection live in a
// future expansion.

import { attrsGet, isNode } from '../model/doc.js'
import type { AttrValue, Doc, Node } from '../model/types.js'
import { polylineToSvgD, rectContains, type Vec2, v2 } from './geom.js'
import {
  findShapeById,
  getShapeAttrNumber,
  getShapeAttrString,
  getShapeBbox
} from './shape-utils.js'

export type Routing = 'straight' | 'orthogonal' | 'curved'

export interface EndpointAnchor {
  point: Vec2
  /** Bounding box of the anchored shape, if any (used to avoid routing through it). */
  bbox: import('./geom.js').Rect | null
  /** Which side of the bbox the anchor sits on: 'top' | 'bottom' | 'left' | 'right' | null */
  side: 'top' | 'bottom' | 'left' | 'right' | null
}

// ---------------------------------------------------------------------------
// Endpoint resolution
// ---------------------------------------------------------------------------

export function resolveEndpoint(
  doc: Doc,
  connector: Node,
  end: 'from' | 'to'
): EndpointAnchor {
  const shapeId = getShapeAttrString(connector, `${end}-shape`, '')
  if (shapeId !== '') {
    const found = findShapeById(doc, shapeId)
    if (found !== null) {
      const shape = found.shape
      // Port anchor: explicit port id on the shape
      const portId = getShapeAttrString(connector, `${end}-port`, '')
      const port = portId !== '' ? findShapePort(shape, portId) : null
      if (port !== null) {
        return { point: port, bbox: getShapeBbox(shape), side: null }
      }
      // Closest-edge anchor (fallback) — use bbox center for now.
      const bbox = getShapeBbox(shape)
      if (bbox !== null) {
        const center = { x: bbox.x + bbox.width / 2, y: bbox.y + bbox.height / 2 }
        return { point: center, bbox, side: null }
      }
    }
  }
  // Free anchor
  const x = getShapeAttrNumber(connector, `${end}-x`, 0)
  const y = getShapeAttrNumber(connector, `${end}-y`, 0)
  return { point: v2(x, y), bbox: null, side: null }
}

function findShapePort(shape: Node, portId: string): Vec2 | null {
  const ports = attrsGet(shape.attrs, 'ports')
  if (!Array.isArray(ports)) return null
  for (const p of ports) {
    if (typeof p !== 'object' || p === null || Array.isArray(p)) continue
    const port = p as { [k: string]: AttrValue }
    if (port['id'] === portId) {
      const bbox = getShapeBbox(shape)
      if (bbox === null) return null
      const px = typeof port['x'] === 'number' ? port['x'] : 0
      const py = typeof port['y'] === 'number' ? port['y'] : 0
      // Port coords are normalized 0..1 in shape-local space
      return { x: bbox.x + px * bbox.width, y: bbox.y + py * bbox.height }
    }
  }
  return null
}

// ---------------------------------------------------------------------------
// Routing strategies
// ---------------------------------------------------------------------------

export function routeStraight(a: Vec2, b: Vec2): Vec2[] {
  return [a, b]
}

// Single-bend L-route. Picks horizontal-first if horizontal distance is
// larger, else vertical-first. Falls back to a Z-route (two bends) if the
// L-route would cross either endpoint's bbox.
export function routeOrthogonal(
  a: Vec2, b: Vec2,
  aBbox: import('./geom.js').Rect | null = null,
  bBbox: import('./geom.js').Rect | null = null
): Vec2[] {
  if (a.x === b.x || a.y === b.y) return [a, b]

  const horizFirst = Math.abs(b.x - a.x) >= Math.abs(b.y - a.y)
  const lCorner: Vec2 = horizFirst ? { x: b.x, y: a.y } : { x: a.x, y: b.y }

  const crosses =
    (aBbox !== null && rectContains(aBbox, lCorner)) ||
    (bBbox !== null && rectContains(bBbox, lCorner))

  if (!crosses) return [a, lCorner, b]

  // Z-route: bend halfway across the gap
  if (horizFirst) {
    const mx = (a.x + b.x) / 2
    return [a, { x: mx, y: a.y }, { x: mx, y: b.y }, b]
  } else {
    const my = (a.y + b.y) / 2
    return [a, { x: a.x, y: my }, { x: b.x, y: my }, b]
  }
}

// Curved route: same polyline as orthogonal but the SVG `d` uses quadratic
// bezier corners. For now, we return the polyline; the renderer can emit
// curve segments. The actual curved `d` is produced by routeToCurvedSvgD.
export function routeCurved(
  a: Vec2, b: Vec2,
  aBbox: import('./geom.js').Rect | null = null,
  bBbox: import('./geom.js').Rect | null = null
): Vec2[] {
  return routeOrthogonal(a, b, aBbox, bBbox)
}

// ---------------------------------------------------------------------------
// Top-level computeRoute
// ---------------------------------------------------------------------------

export function computeRoute(connector: Node, doc: Doc): Vec2[] {
  const from = resolveEndpoint(doc, connector, 'from')
  const to   = resolveEndpoint(doc, connector, 'to')
  const routing = getShapeAttrString(connector, 'routing', 'orthogonal') as Routing

  const waypoints = readWaypoints(connector)
  const points: Vec2[] = [from.point, ...waypoints, to.point]

  // Route each consecutive segment independently
  const out: Vec2[] = []
  for (let i = 0; i + 1 < points.length; i++) {
    const segA = points[i] as Vec2
    const segB = points[i + 1] as Vec2
    const aBox = i === 0 ? from.bbox : null
    const bBox = i === points.length - 2 ? to.bbox : null
    const seg =
      routing === 'straight'   ? routeStraight(segA, segB) :
      routing === 'curved'     ? routeCurved(segA, segB, aBox, bBox)
                               : routeOrthogonal(segA, segB, aBox, bBox)
    if (i === 0) out.push(...seg)
    else out.push(...seg.slice(1))   // skip duplicate join point
  }
  return out
}

function readWaypoints(connector: Node): Vec2[] {
  const ws = attrsGet(connector.attrs, 'waypoints')
  if (!Array.isArray(ws)) return []
  const out: Vec2[] = []
  for (const w of ws) {
    if (typeof w !== 'object' || w === null || Array.isArray(w)) continue
    const wp = w as { [k: string]: AttrValue }
    const x = typeof wp['x'] === 'number' ? wp['x'] : 0
    const y = typeof wp['y'] === 'number' ? wp['y'] : 0
    out.push({ x, y })
  }
  return out
}

// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------

export function routeToSvgPath(points: Vec2[]): string {
  return polylineToSvgD(points)
}

// Curved SVG `d` with quarter-arc corners at each bend.
export function routeToCurvedSvgPath(points: Vec2[], radius = 8): string {
  if (points.length < 2) return ''
  if (points.length === 2) return polylineToSvgD(points)
  const head = points[0] as Vec2
  let out = `M ${head.x} ${head.y}`
  for (let i = 1; i < points.length - 1; i++) {
    const prev = points[i - 1] as Vec2
    const cur  = points[i] as Vec2
    const next = points[i + 1] as Vec2
    const inDx = Math.sign(cur.x - prev.x)
    const inDy = Math.sign(cur.y - prev.y)
    const outDx = Math.sign(next.x - cur.x)
    const outDy = Math.sign(next.y - cur.y)
    const inLen = Math.min(radius, Math.hypot(cur.x - prev.x, cur.y - prev.y) / 2)
    const outLen = Math.min(radius, Math.hypot(next.x - cur.x, next.y - cur.y) / 2)
    const p1 = { x: cur.x - inDx * inLen, y: cur.y - inDy * inLen }
    const p2 = { x: cur.x + outDx * outLen, y: cur.y + outDy * outLen }
    out += ` L ${p1.x} ${p1.y} Q ${cur.x} ${cur.y} ${p2.x} ${p2.y}`
  }
  const last = points[points.length - 1] as Vec2
  out += ` L ${last.x} ${last.y}`
  return out
}

// Used by the connector hit-test: tolerance-aware point-on-route check.
export function isPointOnRoute(p: Vec2, route: Vec2[], tolerance: number): boolean {
  for (let i = 0; i + 1 < route.length; i++) {
    const a = route[i] as Vec2
    const b = route[i + 1] as Vec2
    // Reuse pointToSegmentDistSq via tolerance squared
    const dx = b.x - a.x, dy = b.y - a.y
    const lenSq = dx * dx + dy * dy
    if (lenSq === 0) {
      if (Math.hypot(p.x - a.x, p.y - a.y) <= tolerance) return true
      continue
    }
    let t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq
    t = Math.max(0, Math.min(1, t))
    const px = a.x + t * dx, py = a.y + t * dy
    if (Math.hypot(p.x - px, p.y - py) <= tolerance) return true
  }
  return false
}

// Re-export the kind helper for callers that need to filter on connector tag.
export function isConnector(n: unknown): n is Node {
  return typeof n === 'object' && n !== null && (n as Node).kind === 'node' && (n as Node).tag === 'connector'
}

void isNode  // ensure module imports stay alive after tree-shake
