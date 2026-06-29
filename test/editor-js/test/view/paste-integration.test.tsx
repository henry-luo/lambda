import { afterEach, describe, expect, it } from 'vitest'
import { act, render, cleanup } from '@testing-library/react'
import { FullEditor } from '../../demo/full-editor.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'

afterEach(() => cleanup())

function firePaste(el: HTMLElement, html: string, text = '') {
  act(() => {
    const ev = new Event('paste', { bubbles: true, cancelable: true }) as Event & {
      clipboardData: { getData: (t: string) => string }
    }
    ev.clipboardData = { getData: (t: string) => (t === 'text/html' ? html : t === 'text/plain' ? text : '') }
    el.dispatchEvent(ev)
  })
}

function mount(html: string) {
  const { doc, selection } = parseHtmlToDoc(html, html5SubsetSchema)
  const utils = render(<FullEditor doc={doc} schema={html5SubsetSchema} initialSelection={selection} />)
  const root = utils.container.querySelector('[contenteditable]') as HTMLElement
  return { ...utils, root }
}

describe('demo/FullEditor — paste wiring', () => {
  it('pastes inline HTML at the caret', () => {
    const { container, root } = mount('<doc><p>ab<cursor></cursor>cd</p></doc>')
    firePaste(root, 'X<b>Y</b>')
    expect(container.textContent).toContain('abXYcd')
  })

  it('falls back to text/plain when no HTML is present', () => {
    const { container, root } = mount('<doc><p><cursor></cursor></p></doc>')
    firePaste(root, '', 'hello world')
    expect(container.textContent).toContain('hello world')
  })

  it('splits the block for a multi-paragraph plain-text paste', () => {
    const { container, root } = mount('<doc><p>start<cursor></cursor>end</p></doc>')
    firePaste(root, '', 'one\n\ntwo')
    // start+one, then two+end across two paragraphs
    expect(container.textContent).toContain('startone')
    expect(container.textContent).toContain('twoend')
  })

  it('pastes an <img> from text/html at the caret', () => {
    const { container, root } = mount('<doc><p>x<cursor></cursor></p></doc>')
    firePaste(root, '<img src="data:pic" alt="p">')
    expect(container.querySelector('img')).not.toBeNull()
  })
})

function fireCopy(el: HTMLElement): Record<string, string> {
  const captured: Record<string, string> = {}
  act(() => {
    const ev = new Event('copy', { bubbles: true, cancelable: true }) as Event & {
      clipboardData: { setData: (t: string, v: string) => void; getData: () => string }
    }
    ev.clipboardData = { setData: (t, v) => { captured[t] = v }, getData: () => '' }
    el.dispatchEvent(ev)
  })
  return captured
}

describe('demo/FullEditor — copy wiring', () => {
  it('copies a selected image node as HTML (so it can be pasted back)', () => {
    const { doc } = parseHtmlToDoc('<doc><p>x</p><figure><img src="data:img" alt="pic"></img></figure></doc>', html5SubsetSchema)
    const utils = render(<FullEditor doc={doc} schema={html5SubsetSchema} initialSelection={{ kind: 'node', path: [1, 0] }} />)
    const root = utils.container.querySelector('[contenteditable]') as HTMLElement
    const captured = fireCopy(root)
    expect(captured['text/html']).toContain('<img')
    expect(captured['text/html']).toContain('data:img')
  })
})
