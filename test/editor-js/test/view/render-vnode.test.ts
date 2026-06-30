// Plain-DOM render parity (Stage 4B) — the framework-free twin of
// render.test.tsx. Asserts that `renderDoc` (VNode) committed via the keyed
// reconciler produces the same DOM contract React produced.
import { describe, it, expect } from 'vitest'
import { renderDoc } from '../../src/view/render-vnode.js'
import { reconcileDoc } from '../../src/view/reconcile.js'
import { node, nodeAttrs, text, textMarked } from '../../src/model/doc.js'
import type { Doc } from '../../src/model/types.js'

function mount(doc: Doc): HTMLElement {
  const container = document.createElement('div')
  reconcileDoc(container, renderDoc(doc))
  return container
}

describe('view/render-vnode — Doc → VNode → DOM', () => {
  it('renders an empty doc as a div container', () => {
    const c = mount(node('doc', []))
    const root = c.querySelector('.rdt-editor') as HTMLElement
    expect(root).not.toBeNull()
    expect(root.getAttribute('data-source-path')).toBe('')
    expect(root.getAttribute('data-tag')).toBe('doc')
  })

  it('renders a single paragraph', () => {
    const c = mount(node('doc', [node('p', [text('hello')])]))
    expect(c.querySelector('p')).not.toBeNull()
    expect(c.querySelector('[data-source-path="0"]')).not.toBeNull()
    expect(c.querySelector('[data-source-path="0,0"]')).not.toBeNull()
    expect(c.textContent).toBe('hello')
  })

  it('renders text with marks as a single non-nested span[style]', () => {
    const c = mount(node('doc', [node('p', [textMarked('bold', { bold: true })])]))
    const spans = c.querySelectorAll('span')
    expect(spans.length).toBe(1)
    expect(spans[0]!.getAttribute('style')).toContain('font-weight:bold')
    expect(spans[0]!.getAttribute('data-source-path')).toBe('0,0')
    expect(c.querySelector('strong')).toBeNull()
    expect(c.querySelector('em')).toBeNull()
  })

  it('renders a link mark as <a href>', () => {
    const c = mount(node('doc', [node('p', [textMarked('click', { link: 'https://example.com' })])]))
    const a = c.querySelector('a') as HTMLAnchorElement
    expect(a).not.toBeNull()
    expect(a.getAttribute('href')).toBe('https://example.com')
  })

  it('renders multiple style marks combined in one span', () => {
    const c = mount(node('doc', [node('p', [textMarked('x', { bold: true, italic: true, color: '#f00' })])]))
    const spans = c.querySelectorAll('span')
    expect(spans.length).toBe(1)
    const style = spans[0]!.getAttribute('style') ?? ''
    expect(style).toContain('font-weight:bold')
    expect(style).toContain('font-style:italic')
    expect(style).toContain('color:#f00')
  })

  it('renders attributes', () => {
    const c = mount(node('doc', [
      nodeAttrs('img', [{ name: 'src', value: 'x.png' }, { name: 'alt', value: 'X' }], [])
    ]))
    const img = c.querySelector('img') as HTMLImageElement
    expect(img.getAttribute('src')).toBe('x.png')
    expect(img.getAttribute('alt')).toBe('X')
  })

  it('renders lists', () => {
    const c = mount(node('doc', [
      node('ul', [node('li', [text('a')]), node('li', [text('b')])])
    ]))
    expect(c.querySelector('ul')).not.toBeNull()
    expect(c.querySelectorAll('li').length).toBe(2)
    expect(c.querySelector('[data-source-path="0,0,0"]')).not.toBeNull()
  })

  it('renders an empty container with a placeholder <br>', () => {
    const c = mount(node('doc', [node('p', [])]))
    const br = c.querySelector('p > br') as HTMLBRElement
    expect(br).not.toBeNull()
    expect(br.getAttribute('data-placeholder')).toBe('true')
  })

  it('renders list-item indent as a left margin', () => {
    const c = mount(node('doc', [
      node('ul', [nodeAttrs('li', [{ name: 'indent', value: 2 }], [text('a')])])
    ]))
    const li = c.querySelector('li') as HTMLElement
    expect(li.style.marginInlineStart).toBe('3.5em')
    expect(li.hasAttribute('indent')).toBe(false)
  })
})
