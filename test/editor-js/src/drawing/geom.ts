// 2D geometry primitives used by hit-test, routing, snap, and the tool state
// machine.
//
// All operations are pure; types are plain JSON-friendly maps.

export interface Vec2 { x: number; y: number }
export interface Rect { x: number; y: number; width: number; height: number }

// ---------------------------------------------------------------------------
// Constructors / accessors
// ---------------------------------------------------------------------------

export const ZERO: Readonly<Vec2> = Object.freeze({ x: 0, y: 0 })

export function v2(x: number, y: number): Vec2 { return { x, y } }
export function rect(x: number, y: number, width: number, height: number): Rect {
  return { x, y, width, height }
}

export function rectFromPoints(a: Vec2, b: Vec2): Rect {
  const x = Math.min(a.x, b.x)
  const y = Math.min(a.y, b.y)
  return { x, y, width: Math.abs(b.x - a.x), height: Math.abs(b.y - a.y) }
}

export function rectCenter(r: Rect): Vec2 {
  return { x: r.x + r.width / 2, y: r.y + r.height / 2 }
}

export function rectRight(r: Rect): number { return r.x + r.width }
export function rectBottom(r: Rect): number { return r.y + r.height }

// ---------------------------------------------------------------------------
// Vec2 arithmetic
// ---------------------------------------------------------------------------

export function v2Add(a: Vec2, b: Vec2): Vec2 { return { x: a.x + b.x, y: a.y + b.y } }
export function v2Sub(a: Vec2, b: Vec2): Vec2 { return { x: a.x - b.x, y: a.y - b.y } }
export function v2Scale(a: Vec2, k: number): Vec2 { return { x: a.x * k, y: a.y * k } }
export function v2Dot(a: Vec2, b: Vec2): number { return a.x * b.x + a.y * b.y }
export function v2Len(a: Vec2): number { return Math.hypot(a.x, a.y) }
export function v2Dist(a: Vec2, b: Vec2): number { return Math.hypot(a.x - b.x, a.y - b.y) }

// ---------------------------------------------------------------------------
// Rect predicates
// ---------------------------------------------------------------------------

export function rectContains(r: Rect, p: Vec2): boolean {
  return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height
}

export function rectIntersects(a: Rect, b: Rect): boolean {
  return !(a.x + a.width  < b.x || b.x + b.width  < a.x ||
           a.y + a.height < b.y || b.y + b.height < a.y)
}

export function rectUnion(a: Rect, b: Rect): Rect {
  const x = Math.min(a.x, b.x)
  const y = Math.min(a.y, b.y)
  const x2 = Math.max(a.x + a.width,  b.x + b.width)
  const y2 = Math.max(a.y + a.height, b.y + b.height)
  return { x, y, width: x2 - x, height: y2 - y }
}

// ---------------------------------------------------------------------------
// Rotation around a center
// ---------------------------------------------------------------------------

export function rotatePoint(p: Vec2, center: Vec2, angleDeg: number): Vec2 {
  if (angleDeg === 0) return p
  const rad = (angleDeg * Math.PI) / 180
  const c = Math.cos(rad), s = Math.sin(rad)
  const dx = p.x - center.x, dy = p.y - center.y
  return { x: center.x + dx * c - dy * s, y: center.y + dx * s + dy * c }
}

// ---------------------------------------------------------------------------
// Line / segment math
// ---------------------------------------------------------------------------

// Squared distance from point p to the line segment a→b.
export function pointToSegmentDistSq(p: Vec2, a: Vec2, b: Vec2): number {
  const dx = b.x - a.x, dy = b.y - a.y
  const lenSq = dx * dx + dy * dy
  if (lenSq === 0) {
    const ex = p.x - a.x, ey = p.y - a.y
    return ex * ex + ey * ey
  }
  let t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq
  t = Math.max(0, Math.min(1, t))
  const px = a.x + t * dx, py = a.y + t * dy
  const ex = p.x - px, ey = p.y - py
  return ex * ex + ey * ey
}

export function pointToSegmentDist(p: Vec2, a: Vec2, b: Vec2): number {
  return Math.sqrt(pointToSegmentDistSq(p, a, b))
}

// True iff p is within `tolerance` of the polyline.
export function pointNearPolyline(p: Vec2, poly: Vec2[], tolerance: number): boolean {
  const tolSq = tolerance * tolerance
  for (let i = 0; i + 1 < poly.length; i++) {
    if (pointToSegmentDistSq(p, poly[i] as Vec2, poly[i + 1] as Vec2) <= tolSq) return true
  }
  return false
}

// ---------------------------------------------------------------------------
// Polygon (closed): even-odd point-in-polygon
// ---------------------------------------------------------------------------

export function pointInPolygon(p: Vec2, poly: Vec2[]): boolean {
  let inside = false
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[i] as Vec2
    const b = poly[j] as Vec2
    const intersect =
      a.y > p.y !== b.y > p.y &&
      p.x < ((b.x - a.x) * (p.y - a.y)) / (b.y - a.y || 1e-9) + a.x
    if (intersect) inside = !inside
  }
  return inside
}

// ---------------------------------------------------------------------------
// Ellipse (axis-aligned) point-in test
// ---------------------------------------------------------------------------

export function pointInEllipse(p: Vec2, bbox: Rect): boolean {
  const c = rectCenter(bbox)
  const rx = bbox.width / 2, ry = bbox.height / 2
  if (rx === 0 || ry === 0) return false
  const dx = (p.x - c.x) / rx
  const dy = (p.y - c.y) / ry
  return dx * dx + dy * dy <= 1
}

// ---------------------------------------------------------------------------
// SVG path d-string from polyline
// ---------------------------------------------------------------------------

export function polylineToSvgD(points: Vec2[]): string {
  if (points.length === 0) return ''
  const head = points[0] as Vec2
  let out = `M ${head.x} ${head.y}`
  for (let i = 1; i < points.length; i++) {
    const p = points[i] as Vec2
    out += ` L ${p.x} ${p.y}`
  }
  return out
}
