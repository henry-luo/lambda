import { describe, it, expect } from 'vitest'
import { docSchema } from '../../src/schemas/doc.js'
import { validateDoc, type Schema } from '../../src/model/schema.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'

describe('drawing/schema — validation', () => {
  it('a valid drawing with shapes passes', () => {
    const { doc } = parseHtmlToDoc(`
      <doc>
        <drawing id="D1" width="400" height="300">
          <layer id="L1">
            <shape id="S1" kind="rect" x="10" y="10" width="100" height="80"></shape>
            <shape id="S2" kind="ellipse" x="200" y="50" width="80" height="80"></shape>
          </layer>
        </drawing>
      </doc>`, docSchema)
    const r = validateDoc(doc, docSchema as Schema)
    expect(r.valid).toBe(true)
  })

  it('drawing with a connector passes', () => {
    const { doc } = parseHtmlToDoc(`
      <doc>
        <drawing id="D1" width="400" height="300">
          <layer id="L1">
            <shape id="S1" kind="rect" x="10" y="10" width="100" height="80"></shape>
            <shape id="S2" kind="rect" x="200" y="100" width="100" height="80"></shape>
            <connector id="C1" from-shape="S1" to-shape="S2" routing="orthogonal"></connector>
          </layer>
        </drawing>
      </doc>`, docSchema)
    expect(validateDoc(doc, docSchema as Schema).valid).toBe(true)
  })

  it('drawing requires id (catches missing required attr)', () => {
    const { doc } = parseHtmlToDoc(`
      <doc>
        <drawing width="400" height="300">
          <layer id="L1"></layer>
        </drawing>
      </doc>`, docSchema)
    const r = validateDoc(doc, docSchema as Schema)
    expect(r.valid).toBe(false)
    expect(r.errors.some(e => e.message.includes('id'))).toBe(true)
  })

  it('shape requires kind', () => {
    const { doc } = parseHtmlToDoc(`
      <doc>
        <drawing id="D1">
          <layer id="L1">
            <shape id="S1" x="10" y="10" width="100" height="80"></shape>
          </layer>
        </drawing>
      </doc>`, docSchema)
    const r = validateDoc(doc, docSchema as Schema)
    expect(r.valid).toBe(false)
    expect(r.errors.some(e => e.message.includes('kind'))).toBe(true)
  })

  it('mixed flow + drawing in one doc', () => {
    const { doc } = parseHtmlToDoc(`
      <doc>
        <p>Before</p>
        <drawing id="D1" width="100" height="100">
          <layer id="L1">
            <shape id="S1" kind="rect" x="0" y="0" width="50" height="50"></shape>
          </layer>
        </drawing>
        <p>After</p>
      </doc>`, docSchema)
    expect(validateDoc(doc, docSchema as Schema).valid).toBe(true)
  })
})
