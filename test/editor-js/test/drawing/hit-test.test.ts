import { describe, it, expect } from 'vitest'
import { hitTestDrawing, hitTestShape } from '../../src/drawing/hit-test.js'
import { docSchema } from '../../src/schemas/doc.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'
import { v2 } from '../../src/drawing/geom.js'
import { isNode, nodeAt } from '../../src/model/doc.js'

describe('drawing/hit-test — per-shape', () => {
  it('rectangle hits inside, misses outside', () => {
    const { doc } = parseHtmlToDoc(`
      <doc><drawing id="D" width="200" height="100"><layer id="L">
        <shape id="R" kind="rect" x="10" y="10" width="80" height="40"></shape>
      </layer></drawing></doc>`, docSchema)
    const shape = nodeAt(doc, [0, 0, 0])
    if (!isNode(shape)) throw new Error()
    expect(hitTestShape(shape, v2(20, 20))).toBe(true)
    expect(hitTestShape(shape, v2(100, 100))).toBe(false)
  })

  it('ellipse hits inside, misses corner', () => {
    const { doc } = parseHtmlToDoc(`
      <doc><drawing id="D" width="200" height="100"><layer id="L">
        <shape id="E" kind="ellipse" x="0" y="0" width="100" height="50"></shape>
      </layer></drawing></doc>`, docSchema)
    const shape = nodeAt(doc, [0, 0, 0])
    if (!isNode(shape)) throw new Error()
    expect(hitTestShape(shape, v2(50, 25))).toBe(true)
    expect(hitTestShape(shape, v2(99, 49))).toBe(false)  // corner — outside the ellipse
  })

  it('line hits near segment, misses far', () => {
    const { doc } = parseHtmlToDoc(`
      <doc><drawing id="D" width="200" height="100"><layer id="L">
        <shape id="LN" kind="line" x="0" y="0" width="100" height="0"></shape>
      </layer></drawing></doc>`, docSchema)
    const shape = nodeAt(doc, [0, 0, 0])
    if (!isNode(shape)) throw new Error()
    expect(hitTestShape(shape, v2(50, 1))).toBe(true)   // 1px from segment
    expect(hitTestShape(shape, v2(50, 20))).toBe(false) // far
  })
})

describe('drawing/hit-test — drawing-level (z-order)', () => {
  it('returns the topmost (last) hit shape', () => {
    const { doc } = parseHtmlToDoc(`
      <doc><drawing id="D" width="200" height="200"><layer id="L">
        <shape id="BACK"  kind="rect" x="0" y="0" width="100" height="100"></shape>
        <shape id="FRONT" kind="rect" x="20" y="20" width="50"  height="50"></shape>
      </layer></drawing></doc>`, docSchema)
    const drawing = nodeAt(doc, [0])
    if (!isNode(drawing)) throw new Error()
    const r = hitTestDrawing(drawing, v2(30, 30))
    expect(r.kind).toBe('shape')
    expect(r.shape_id).toBe('FRONT')
  })

  it('falls through to back when click misses front', () => {
    const { doc } = parseHtmlToDoc(`
      <doc><drawing id="D" width="200" height="200"><layer id="L">
        <shape id="BACK"  kind="rect" x="0" y="0" width="100" height="100"></shape>
        <shape id="FRONT" kind="rect" x="20" y="20" width="20"  height="20"></shape>
      </layer></drawing></doc>`, docSchema)
    const drawing = nodeAt(doc, [0])
    if (!isNode(drawing)) throw new Error()
    const r = hitTestDrawing(drawing, v2(80, 80))
    expect(r.shape_id).toBe('BACK')
  })

  it('empty result on miss', () => {
    const { doc } = parseHtmlToDoc(`
      <doc><drawing id="D" width="200" height="200"><layer id="L">
        <shape id="R" kind="rect" x="0" y="0" width="10" height="10"></shape>
      </layer></drawing></doc>`, docSchema)
    const drawing = nodeAt(doc, [0])
    if (!isNode(drawing)) throw new Error()
    expect(hitTestDrawing(drawing, v2(100, 100)).kind).toBe('empty')
  })
})
