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

  it('renders text with marks as nested elements', () => {
    const html = renderToStaticMarkup(
      renderDoc(node('doc', [node('p', [textMarked('bold', ['strong'])])]))
    )
    expect(html).toContain('<strong>')
    expect(html).toContain('<span data-source-path="0,0"')
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
