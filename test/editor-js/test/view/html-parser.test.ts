import { describe, it, expect } from 'vitest'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { textAt } from '../helpers/narrow.js'

describe('view/html-parser — basic parsing', () => {
  it('parses a single paragraph', () => {
    const { doc, selection } = parseHtmlToDoc('<doc><p>hello</p></doc>', html5SubsetSchema)
    expect(doc.tag).toBe('doc')
    expect(doc.content.length).toBe(1)
    expect(textAt(doc, [0, 0])).toBe('hello')
    expect(selection).toBeNull()
  })

  it('strips ignorable indentation between block tags', () => {
    const { doc } = parseHtmlToDoc(
      `<doc>
         <p>a</p>
         <p>b</p>
       </doc>`,
      html5SubsetSchema
    )
    expect(doc.content.length).toBe(2)
    expect(textAt(doc, [0, 0])).toBe('a')
    expect(textAt(doc, [1, 0])).toBe('b')
  })

  it('flattens <strong> into a text leaf with marks={bold:true}', () => {
    const { doc } = parseHtmlToDoc(
      '<doc><p>hi <strong>bold</strong> end</p></doc>',
      html5SubsetSchema
    )
    const p = doc.content[0] as any
    // three FLAT text leaves, no nested element
    expect(p.content.length).toBe(3)
    expect(p.content[0]).toEqual({ kind: 'text', text: 'hi ',  marks: {} })
    expect(p.content[1]).toEqual({ kind: 'text', text: 'bold', marks: { bold: true } })
    expect(p.content[2]).toEqual({ kind: 'text', text: ' end', marks: {} })
  })

  it('flattens nested <em><strong> into one leaf with both marks', () => {
    const { doc } = parseHtmlToDoc(
      '<doc><p><em>italic <strong>bold</strong> end</em></p></doc>',
      html5SubsetSchema
    )
    const p = doc.content[0] as any
    expect(p.content.length).toBe(3)
    expect(p.content[0]).toEqual({ kind: 'text', text: 'italic ', marks: { italic: true } })
    expect(p.content[1]).toEqual({ kind: 'text', text: 'bold',    marks: { italic: true, bold: true } })
    expect(p.content[2]).toEqual({ kind: 'text', text: ' end',    marks: { italic: true } })
  })

  it('parses <span style="font-weight:bold"> into marks={bold:true}', () => {
    const { doc } = parseHtmlToDoc(
      '<doc><p><span style="font-weight: bold">bold</span></p></doc>',
      html5SubsetSchema
    )
    const p = doc.content[0] as any
    expect(p.content[0]).toEqual({ kind: 'text', text: 'bold', marks: { bold: true } })
  })

  it('parses multiple CSS declarations into combined marks', () => {
    const { doc } = parseHtmlToDoc(
      '<doc><p><span style="font-weight: bold; color: #ff0000">x</span></p></doc>',
      html5SubsetSchema
    )
    const p = doc.content[0] as any
    expect(p.content[0]).toEqual({ kind: 'text', text: 'x', marks: { bold: true, color: '#ff0000' } })
  })

  it('parses <a href> into marks={link: href}', () => {
    const { doc } = parseHtmlToDoc(
      '<doc><p><a href="https://example.com">click</a></p></doc>',
      html5SubsetSchema
    )
    const p = doc.content[0] as any
    expect(p.content[0]).toEqual({ kind: 'text', text: 'click', marks: { link: 'https://example.com' } })
  })

  it('parses cursor marker → collapsed TextSelection', () => {
    const { selection } = parseHtmlToDoc(
      '<doc><p>he<cursor></cursor>llo</p></doc>',
      html5SubsetSchema
    )
    expect(selection).toEqual({
      kind: 'text',
      anchor: { path: [0, 0], offset: 2 },
      head:   { path: [0, 0], offset: 2 }
    })
  })

  it('parses anchor + focus → range TextSelection', () => {
    const { selection } = parseHtmlToDoc(
      '<doc><p>h<anchor></anchor>el<focus></focus>lo</p></doc>',
      html5SubsetSchema
    )
    expect(selection).toEqual({
      kind: 'text',
      anchor: { path: [0, 0], offset: 1 },
      head:   { path: [0, 0], offset: 3 }
    })
  })

  it('parses lists', () => {
    const { doc } = parseHtmlToDoc(
      '<doc><ul><li>a</li><li>b</li></ul></doc>',
      html5SubsetSchema
    )
    const ul = doc.content[0] as any
    expect(ul.tag).toBe('ul')
    expect(ul.content.length).toBe(2)
    expect(ul.content[0].tag).toBe('li')
  })

  it('parses element attributes including types', () => {
    const { doc } = parseHtmlToDoc(
      '<doc><img src="x.png" alt="X"></img></doc>',
      html5SubsetSchema
    )
    const img = (doc.content[0] as any)
    expect(img.tag).toBe('img')
    expect(img.attrs).toContainEqual({ name: 'src', value: 'x.png' })
    expect(img.attrs).toContainEqual({ name: 'alt', value: 'X' })
  })

  it('falls back to synthesizing <doc> if absent', () => {
    const { doc } = parseHtmlToDoc('<p>orphan</p>', html5SubsetSchema)
    expect(doc.tag).toBe('doc')
    expect(textAt(doc, [0, 0])).toBe('orphan')
  })
})
