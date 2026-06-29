import { afterEach, describe, expect, it } from 'vitest'
import { act, render, cleanup } from '@testing-library/react'
import { FullEditor } from '../../demo/full-editor.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'

afterEach(() => cleanup())

function toolbarBtn(container: HTMLElement, title: string): HTMLElement {
  return Array.from(container.querySelectorAll('.rdt-toolbar button'))
    .find(b => (b as HTMLElement).title === title) as HTMLElement
}

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

describe('demo/FullEditor — image drag / select', () => {
  // Regression: mousedown must NOT preventDefault on an image, or the native
  // drag (mousedown → dragstart) can never start and the image can't be dragged.
  it('mousedown on an image does not preventDefault (drag stays possible)', () => {
    const { container } = mount('<doc><p>x</p><figure><img src="a.png" alt="p"></img></figure></doc>')
    const img = container.querySelector('img') as HTMLElement
    const md = new MouseEvent('mousedown', { bubbles: true, cancelable: true })
    act(() => { img.dispatchEvent(md) })
    expect(md.defaultPrevented).toBe(false)
  })

  it('click on an image selects it as a node (resize overlay appears)', () => {
    const { container } = mount('<doc><p>x</p><figure><img src="a.png" alt="p"></img></figure></doc>')
    const img = container.querySelector('img') as HTMLElement
    act(() => { img.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true })) })
    expect(container.querySelector('.rdt-img-overlay')).not.toBeNull()
  })
})

describe('demo/FullEditor — highlight active state', () => {
  const caret = textSelection(pos([0, 0], 1), pos([0, 0], 1))

  it('highlight button lights up when the caret is in highlighted text', () => {
    const doc = node('doc', [node('p', [{ ...text('hi'), marks: { background: '#fef08a' } }])])
    const { container } = render(<FullEditor doc={doc} schema={html5SubsetSchema} initialSelection={caret} />)
    expect(toolbarBtn(container, 'Highlight').className).toContain('is-active')
  })

  it('highlight button is not active in plain text', () => {
    const doc = node('doc', [node('p', [text('hi')])])
    const { container } = render(<FullEditor doc={doc} schema={html5SubsetSchema} initialSelection={caret} />)
    expect(toolbarBtn(container, 'Highlight').className).not.toContain('is-active')
  })
})

describe('demo/FullEditor — emoji picker', () => {
  it('opening the picker and clicking an emoji inserts it at the caret', () => {
    const { container } = mount('<doc><p>hi<cursor></cursor></p></doc>')
    const openBtn = Array.from(container.querySelectorAll('.rdt-toolbar button'))
      .find(b => (b as HTMLElement).title === 'Insert emoji') as HTMLElement
    expect(openBtn).toBeTruthy()
    act(() => { openBtn.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true })) })
    const emojiBtn = container.querySelector('.rdt-emoji-palette .rdt-emoji') as HTMLElement
    expect(emojiBtn).toBeTruthy()
    const emoji = emojiBtn.textContent as string
    act(() => { emojiBtn.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true })) })
    expect(container.textContent).toContain(emoji)
  })
})
