import { describe, it, expect } from 'vitest'
import {
  pointInEllipse,
  pointInPolygon,
  pointNearPolyline,
  pointToSegmentDist,
  polylineToSvgD,
  rect,
  rectCenter,
  rectContains,
  rectFromPoints,
  rectIntersects,
  rectUnion,
  rotatePoint,
  v2,
  v2Add,
  v2Dist,
  v2Sub
} from '../../src/drawing/geom.js'

describe('drawing/geom — Vec2 arithmetic', () => {
  it('v2Add / v2Sub / v2Dist', () => {
    expect(v2Add(v2(1, 2), v2(3, 4))).toEqual({ x: 4, y: 6 })
    expect(v2Sub(v2(5, 5), v2(3, 2))).toEqual({ x: 2, y: 3 })
    expect(v2Dist(v2(0, 0), v2(3, 4))).toBe(5)
  })
})

describe('drawing/geom — Rect', () => {
  it('rectFromPoints normalizes corners', () => {
    expect(rectFromPoints(v2(10, 20), v2(5, 5))).toEqual({ x: 5, y: 5, width: 5, height: 15 })
  })

  it('rectContains', () => {
    const r = rect(0, 0, 100, 50)
    expect(rectContains(r, v2(50, 25))).toBe(true)
    expect(rectContains(r, v2(101, 25))).toBe(false)
    expect(rectContains(r, v2(0, 0))).toBe(true)
    expect(rectContains(r, v2(100, 50))).toBe(true)
  })

  it('rectIntersects', () => {
    const a = rect(0, 0, 50, 50)
    expect(rectIntersects(a, rect(25, 25, 50, 50))).toBe(true)
    expect(rectIntersects(a, rect(100, 100, 10, 10))).toBe(false)
  })

  it('rectUnion encloses both', () => {
    const u = rectUnion(rect(0, 0, 10, 10), rect(20, 30, 10, 10))
    expect(u).toEqual({ x: 0, y: 0, width: 30, height: 40 })
  })

  it('rectCenter', () => {
    expect(rectCenter(rect(10, 20, 30, 40))).toEqual({ x: 25, y: 40 })
  })
})

describe('drawing/geom — rotation', () => {
  it('rotates 90° around origin', () => {
    const r = rotatePoint(v2(1, 0), v2(0, 0), 90)
    expect(r.x).toBeCloseTo(0)
    expect(r.y).toBeCloseTo(1)
  })

  it('rotate 0° is identity', () => {
    expect(rotatePoint(v2(5, 7), v2(0, 0), 0)).toEqual({ x: 5, y: 7 })
  })
})

describe('drawing/geom — line/polyline math', () => {
  it('pointToSegmentDist on perpendicular', () => {
    expect(pointToSegmentDist(v2(5, 5), v2(0, 0), v2(10, 0))).toBe(5)
  })

  it('pointToSegmentDist clamps to endpoint', () => {
    expect(pointToSegmentDist(v2(20, 0), v2(0, 0), v2(10, 0))).toBe(10)
  })

  it('pointNearPolyline tolerance', () => {
    const poly = [v2(0, 0), v2(10, 0), v2(10, 10)]
    expect(pointNearPolyline(v2(5, 0.5), poly, 1)).toBe(true)
    expect(pointNearPolyline(v2(5, 5), poly, 1)).toBe(false)
  })
})

describe('drawing/geom — point-in-polygon and ellipse', () => {
  it('pointInPolygon (triangle)', () => {
    const tri = [v2(0, 0), v2(10, 0), v2(5, 10)]
    expect(pointInPolygon(v2(5, 5), tri)).toBe(true)
    expect(pointInPolygon(v2(15, 5), tri)).toBe(false)
  })

  it('pointInEllipse', () => {
    const e = rect(0, 0, 100, 50)  // center (50,25), rx=50, ry=25
    expect(pointInEllipse(v2(50, 25), e)).toBe(true)
    expect(pointInEllipse(v2(95, 25), e)).toBe(true)
    expect(pointInEllipse(v2(105, 25), e)).toBe(false)
  })
})

describe('drawing/geom — SVG path emit', () => {
  it('polylineToSvgD', () => {
    expect(polylineToSvgD([v2(10, 20), v2(30, 40), v2(50, 60)]))
      .toBe('M 10 20 L 30 40 L 50 60')
    expect(polylineToSvgD([])).toBe('')
  })
})
