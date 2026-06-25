import { describe, it, expect } from 'vitest'
import { renderToStaticMarkup } from 'react-dom/server'
import { renderDoc } from '../../src/view/render.js'
import { node, nodeAttrs, text, textMarked } from '../../src/model/doc.js'

describe('view/render — Doc → React → HTML', () => {
  it('renders an empty doc as a div container', () => {
    const html = renderToStaticMarkup(renderDoc(node('doc', [])))
    expect(html).toContain('class="rdt-editor"')
    expect(html).toContain('data-source-path=""')
  })

  it('renders a single paragraph', () => {
    const html = renderToStaticMarkup(renderDoc(node('doc', [node('p', [text('hello')])])))
    expect(html).toContain('<p')
    expect(html).toContain('data-source-path="0"')
    expect(html).toContain('data-source-path="0,0"')
    expect(html).toContain('hello')
  })

  it('renders text with marks as a single non-nested <span style>', () => {
    const html = renderToStaticMarkup(
      renderDoc(node('doc', [node('p', [textMarked('bold', { bold: true })])]))
    )
    expect(html).toContain('font-weight:bold')
    expect(html).toContain('data-source-path="0,0"')
    expect(html).not.toContain('<strong>')
    expect(html).not.toContain('<em>')
  })

  it('renders a link mark as <a href>', () => {
    const html = renderToStaticMarkup(
      renderDoc(node('doc', [node('p', [textMarked('click', { link: 'https://example.com' })])]))
    )
    expect(html).toContain('<a')
    expect(html).toContain('href="https://example.com"')
  })

  it('renders multiple style marks combined in one style attr', () => {
    const html = renderToStaticMarkup(
      renderDoc(node('doc', [node('p', [textMarked('x', { bold: true, italic: true, color: '#f00' })])]))
    )
    // single <span> wrapper, multiple style decls
    const spans = html.match(/<span/g) ?? []
    expect(spans.length).toBe(1)
    expect(html).toContain('font-weight:bold')
    expect(html).toContain('font-style:italic')
    expect(html).toContain('color:#f00')
  })

  it('renders attributes', () => {
    const html = renderToStaticMarkup(
      renderDoc(node('doc', [
        nodeAttrs('img', [{ name: 'src', value: 'x.png' }, { name: 'alt', value: 'X' }], [])
      ]))
    )
    expect(html).toContain('src="x.png"')
    expect(html).toContain('alt="X"')
  })

  it('renders lists', () => {
    const html = renderToStaticMarkup(
      renderDoc(node('doc', [
        node('ul', [
          node('li', [text('a')]),
          node('li', [text('b')])
        ])
      ]))
    )
    expect(html).toContain('<ul')
    expect(html).toContain('<li')
    expect(html).toContain('data-source-path="0,0,0"')
  })
})
