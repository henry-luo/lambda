// Keyed reconciler unit tests (Stage 4B). The reconciler is the one new
// component of the plain-DOM port; these pin its invariants: keyed reuse keeps
// node identity, in-place text patches preserve the text node (so the caret
// survives), placeholder <br> ↔ text transitions, and removal.
import { describe, it, expect } from 'vitest'
import { renderDoc } from '../../src/view/render-vnode.js'
import { reconcileDoc } from '../../src/view/reconcile.js'
import { node, nodeAttrs, text } from '../../src/model/doc.js'
import type { Doc } from '../../src/model/types.js'

function commit(container: HTMLElement, doc: Doc): void {
  reconcileDoc(container, renderDoc(doc))
}

function doc(...kids: Doc[]): Doc {
  return node('doc', kids)
}
function para(s: string): Doc {
  return node('p', [text(s)])
}

describe('view/reconcile — keyed identity & selection preservation', () => {
  it('reuses an unchanged sibling element (identity preserved)', () => {
    const c = document.createElement('div')
    commit(c, doc(para('a'), para('b')))
    const pA = c.querySelector('[data-source-path="0"]')!
    commit(c, doc(para('a'), para('bb')))   // only the 2nd paragraph changed
    expect(c.querySelector('[data-source-path="0"]')).toBe(pA)        // same element object
    expect(c.querySelector('[data-source-path="1"]')!.textContent).toBe('bb')
  })

  it('patches a text node in place (same Text object, updated data)', () => {
    const c = document.createElement('div')
    commit(c, doc(para('a')))
    const leaf = c.querySelector('[data-source-path="0,0"]')!
    const textNode = leaf.firstChild as Text
    expect(textNode.nodeType).toBe(3)
    commit(c, doc(para('ab')))
    expect(leaf.firstChild).toBe(textNode)   // same text node object
    expect(textNode.data).toBe('ab')         // data updated in place
  })

  it('a DOM selection survives a patch elsewhere', () => {
    const c = document.createElement('div')
    document.body.appendChild(c)
    commit(c, doc(para('hello'), para('world')))
    const leaf0 = c.querySelector('[data-source-path="0,0"]')!
    const t0 = leaf0.firstChild as Text
    const sel = window.getSelection()!
    const range = document.createRange()
    range.setStart(t0, 2)
    range.setEnd(t0, 2)
    sel.removeAllRanges()
    sel.addRange(range)

    commit(c, doc(para('hello'), para('WORLD')))   // change only the 2nd paragraph

    expect(sel.anchorNode).toBe(t0)                 // caret node unchanged
    expect(t0.isConnected).toBe(true)               // still in the tree
    expect(sel.anchorOffset).toBe(2)
    document.body.removeChild(c)
  })

  it('transitions empty-block <br> ↔ text', () => {
    const c = document.createElement('div')
    commit(c, doc(node('p', [])))
    expect(c.querySelector('p > br')).not.toBeNull()
    commit(c, doc(para('x')))
    expect(c.querySelector('p > br')).toBeNull()
    expect(c.querySelector('p')!.textContent).toBe('x')
    commit(c, doc(node('p', [])))
    expect(c.querySelector('p > br')).not.toBeNull()
  })

  it('removes deleted children', () => {
    const c = document.createElement('div')
    commit(c, doc(para('a'), para('b'), para('c')))
    expect(c.querySelectorAll('p').length).toBe(3)
    commit(c, doc(para('a')))
    expect(c.querySelectorAll('p').length).toBe(1)
    expect(c.textContent).toBe('a')
  })

  it('reconciles an SVG drawing subtree without crashing', () => {
    const c = document.createElement('div')
    const drawing = node('drawing', [
      node('layer', [
        nodeAttrs('shape', [
          { name: 'kind', value: 'rect' }, { name: 'x', value: 1 }, { name: 'y', value: 2 },
          { name: 'width', value: 10 }, { name: 'height', value: 20 }
        ], [])
      ])
    ])
    commit(c, doc(drawing))
    const svg = c.querySelector('svg')!
    expect(svg.namespaceURI).toBe('http://www.w3.org/2000/svg')
    const rect = c.querySelector('rect')!
    expect(rect.namespaceURI).toBe('http://www.w3.org/2000/svg')
    expect(rect.getAttribute('width')).toBe('10')
  })
})
