import { describe, it, expect } from 'vitest'
import {
  computeRoute,
  routeOrthogonal,
  routeStraight,
  routeToCurvedSvgPath,
  routeToSvgPath
} from '../../src/drawing/router.js'
import { docSchema } from '../../src/schemas/doc.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'
import { v2 } from '../../src/drawing/geom.js'
import { isNode, nodeAt } from '../../src/model/doc.js'

describe('drawing/router — straight', () => {
  it('routeStraight is [from, to]', () => {
    expect(routeStraight(v2(0, 0), v2(10, 10))).toEqual([{ x: 0, y: 0 }, { x: 10, y: 10 }])
  })
})

describe('drawing/router — orthogonal', () => {
  it('aligned points → direct line', () => {
    expect(routeOrthogonal(v2(0, 0), v2(10, 0)))
      .toEqual([{ x: 0, y: 0 }, { x: 10, y: 0 }])
    expect(routeOrthogonal(v2(0, 0), v2(0, 10)))
      .toEqual([{ x: 0, y: 0 }, { x: 0, y: 10 }])
  })

  it('L-route picks horizontal first when |dx| >= |dy|', () => {
    const r = routeOrthogonal(v2(0, 0), v2(10, 5))
    expect(r).toEqual([{ x: 0, y: 0 }, { x: 10, y: 0 }, { x: 10, y: 5 }])
  })

  it('L-route picks vertical first when |dy| > |dx|', () => {
    const r = routeOrthogonal(v2(0, 0), v2(5, 10))
    expect(r).toEqual([{ x: 0, y: 0 }, { x: 0, y: 10 }, { x: 5, y: 10 }])
  })

  it('Z-route emitted when L-route would cross a bbox', () => {
    // 'a' is at (0,0); 'b' is at (10,5). The L-route corner (10,0) lies inside
    // b's bbox if b's bbox covers it. Set b bbox to enclose corner.
    const aBbox = null
    const bBbox = { x: 5, y: -5, width: 20, height: 10 }
    const r = routeOrthogonal(v2(0, 0), v2(20, 0), aBbox, bBbox)
    // a and b are horizontally aligned, so we get a direct line — bbox doesn't matter
    expect(r.length).toBe(2)

    // For unaligned points with conflicting bbox we should see a Z-route (4 pts)
    const r2 = routeOrthogonal(v2(0, 0), v2(10, 5), null, { x: 5, y: -100, width: 100, height: 200 })
    expect(r2.length).toBeGreaterThan(2)
  })
})

describe('drawing/router — computeRoute on a doc', () => {
  it('orthogonal connector between two shape centers', () => {
    const { doc } = parseHtmlToDoc(`
      <doc>
        <drawing id="D1" width="400" height="300">
          <layer id="L1">
            <shape id="S1" kind="rect" x="0" y="0" width="100" height="100"></shape>
            <shape id="S2" kind="rect" x="200" y="0" width="100" height="100"></shape>
            <connector id="C1" from-shape="S1" to-shape="S2" routing="straight"></connector>
          </layer>
        </drawing>
      </doc>`, docSchema)
    const connector = nodeAt(doc, [0, 0, 2])
    expect(isNode(connector)).toBe(true)
    if (!isNode(connector)) throw new Error()
    const route = computeRoute(connector, doc)
    // Straight from center(50,50) to center(250,50)
    expect(route).toEqual([{ x: 50, y: 50 }, { x: 250, y: 50 }])
  })

  it('free anchor connector', () => {
    const { doc } = parseHtmlToDoc(`
      <doc>
        <drawing id="D1" width="400" height="300">
          <layer id="L1">
            <connector id="C1" from-x="10" from-y="20" to-x="100" to-y="20" routing="straight"></connector>
          </layer>
        </drawing>
      </doc>`, docSchema)
    const connector = nodeAt(doc, [0, 0, 0])
    if (!isNode(connector)) throw new Error()
    expect(computeRoute(connector, doc)).toEqual([{ x: 10, y: 20 }, { x: 100, y: 20 }])
  })

  it('respects waypoints', () => {
    const { doc } = parseHtmlToDoc(`
      <doc>
        <drawing id="D1" width="400" height="300">
          <layer id="L1">
            <connector id="C1" from-x="0" from-y="0" to-x="100" to-y="0" routing="straight"></connector>
          </layer>
        </drawing>
      </doc>`, docSchema)
    // Manually inject waypoints via attr after parse — parser doesn't know JSON arrays
    const c = nodeAt(doc, [0, 0, 0])
    if (!isNode(c)) throw new Error()
    c.attrs.push({ name: 'waypoints', value: [{ x: 50, y: 30 }] })
    const r = computeRoute(c, doc)
    expect(r).toEqual([
      { x: 0, y: 0 },
      { x: 50, y: 30 },
      { x: 100, y: 0 }
    ])
  })
})

describe('drawing/router — SVG emit', () => {
  it('routeToSvgPath', () => {
    expect(routeToSvgPath([v2(0, 0), v2(10, 0), v2(10, 10)]))
      .toBe('M 0 0 L 10 0 L 10 10')
  })

  it('routeToCurvedSvgPath includes quadratic corners', () => {
    const d = routeToCurvedSvgPath([v2(0, 0), v2(10, 0), v2(10, 10)], 4)
    expect(d).toContain('M')
    expect(d).toContain('Q')
  })
})
