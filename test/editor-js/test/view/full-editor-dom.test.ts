// FullEditorDom parity (Stage 4B) — the framework-free twin of
// tab-keybinding.test.tsx + paste-integration.test.tsx. Drives the vanilla
// FullEditorDom with native events and asserts the same chrome behaviour
// (tab indent, paste/copy, image select, highlight active, emoji picker).
import { afterEach, describe, expect, it } from 'vitest'
import { FullEditorDom } from '../../demo/full-editor-dom.js'
import { docSchema } from '../../src/schemas/doc.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import type { Doc, Selection } from '../../src/model/types.js'
import type { Schema } from '../../src/model/schema.js'

const live: FullEditorDom[] = []
afterEach(() => {
  for (const ed of live.splice(0)) ed.destroy()
})

function mountDoc(doc: Doc, schema: Schema, selection: Selection | null) {
  const host = document.createElement('div')
  document.body.appendChild(host)
  const ed = new FullEditorDom(host, { doc, schema, initialSelection: selection })
  live.push(ed)
  return { host, ed, surface: ed.surface }
}

function mount(html: string, schema: Schema = html5SubsetSchema) {
  const { doc, selection } = parseHtmlToDoc(html, schema)
  return mountDoc(doc, schema, selection)
}

function fireKeyDown(el: HTMLElement, init: KeyboardEventInit): void {
  el.dispatchEvent(new KeyboardEvent('keydown', { ...init, bubbles: true, cancelable: true }))
}

function fireMouse(el: HTMLElement, type: string, init: MouseEventInit = {}): MouseEvent {
  const ev = new MouseEvent(type, { bubbles: true, cancelable: true, ...init })
  el.dispatchEvent(ev)
  return ev
}

function firePaste(el: HTMLElement, html: string, plain = ''): void {
  const ev = new Event('paste', { bubbles: true, cancelable: true }) as Event & {
    clipboardData: { getData: (t: string) => string; items: unknown[] }
  }
  ev.clipboardData = {
    getData: (t: string) => (t === 'text/html' ? html : t === 'text/plain' ? plain : ''),
    items: []
  }
  el.dispatchEvent(ev)
}

function fireCopy(el: HTMLElement): Record<string, string> {
  const captured: Record<string, string> = {}
  const ev = new Event('copy', { bubbles: true, cancelable: true }) as Event & {
    clipboardData: { setData: (t: string, v: string) => void }
  }
  ev.clipboardData = { setData: (t, v) => { captured[t] = v } }
  el.dispatchEvent(ev)
  return captured
}

function toolbarBtn(host: HTMLElement, title: string): HTMLElement {
  return Array.from(host.querySelectorAll('.rdt-toolbar button'))
    .find(b => (b as HTMLElement).title === title) as HTMLElement
}

function liMargin(host: HTMLElement, i: number): string {
  return (host.querySelectorAll('li')[i] as HTMLElement).style.marginInlineStart
}

// ---------------------------------------------------------------------------
// Tab / Shift-Tab list indent (← tab-keybinding.test.tsx)
// ---------------------------------------------------------------------------

describe('FullEditorDom — Tab/Shift-Tab list indent', () => {
  it('Tab indents the current list item (margin applied)', () => {
    const { host, surface } = mount('<doc><ul><li>a</li><li>b<cursor></cursor></li></ul></doc>', docSchema)
    expect(liMargin(host, 1)).toBe('')
    fireKeyDown(surface, { key: 'Tab' })
    expect(liMargin(host, 1)).toBe('1.75em')
    expect(liMargin(host, 0)).toBe('')
  })

  it('Shift+Tab outdents the current list item', () => {
    const { host, surface } = mount('<doc><ul><li>a</li><li>b<cursor></cursor></li></ul></doc>', docSchema)
    fireKeyDown(surface, { key: 'Tab' })
    expect(liMargin(host, 1)).toBe('1.75em')
    fireKeyDown(surface, { key: 'Tab', shiftKey: true })
    expect(liMargin(host, 1)).toBe('')
  })

  it('Tab works on the FIRST item and repeats (Word/Docs style)', () => {
    const { host, surface } = mount('<doc><ul><li>a<cursor></cursor></li><li>b</li></ul></doc>', docSchema)
    fireKeyDown(surface, { key: 'Tab' })
    expect(liMargin(host, 0)).toBe('1.75em')
    fireKeyDown(surface, { key: 'Tab' })
    expect(liMargin(host, 0)).toBe('3.5em')
  })

  it('Tab is a no-op outside a list (and inserts no tab char)', () => {
    const { host, surface } = mount('<doc><p>hi<cursor></cursor></p></doc>', docSchema)
    fireKeyDown(surface, { key: 'Tab' })
    expect(surface.textContent).toBe('hi')
    expect(host.querySelectorAll('li').length).toBe(0)
  })
})

// ---------------------------------------------------------------------------
// Paste / copy / image / highlight / emoji (← paste-integration.test.tsx)
// ---------------------------------------------------------------------------

describe('FullEditorDom — paste wiring', () => {
  it('pastes inline HTML at the caret', () => {
    const { host, surface } = mount('<doc><p>ab<cursor></cursor>cd</p></doc>')
    firePaste(surface, 'X<b>Y</b>')
    expect(host.textContent).toContain('abXYcd')
  })

  it('falls back to text/plain when no HTML is present', () => {
    const { host, surface } = mount('<doc><p><cursor></cursor></p></doc>')
    firePaste(surface, '', 'hello world')
    expect(host.textContent).toContain('hello world')
  })

  it('splits the block for a multi-paragraph plain-text paste', () => {
    const { host, surface } = mount('<doc><p>start<cursor></cursor>end</p></doc>')
    firePaste(surface, '', 'one\n\ntwo')
    expect(host.textContent).toContain('startone')
    expect(host.textContent).toContain('twoend')
  })

  it('pastes an <img> from text/html at the caret', () => {
    const { host, surface } = mount('<doc><p>x<cursor></cursor></p></doc>')
    firePaste(surface, '<img src="data:pic" alt="p">')
    expect(host.querySelector('img')).not.toBeNull()
  })
})

describe('FullEditorDom — copy wiring', () => {
  it('copies a selected image node as HTML', () => {
    const { doc } = parseHtmlToDoc('<doc><p>x</p><figure><img src="data:img" alt="pic"></img></figure></doc>', html5SubsetSchema)
    const { surface } = mountDoc(doc, html5SubsetSchema, { kind: 'node', path: [1, 0] })
    const captured = fireCopy(surface)
    expect(captured['text/html']).toContain('<img')
    expect(captured['text/html']).toContain('data:img')
  })
})

describe('FullEditorDom — image drag / select', () => {
  it('mousedown on an image does not preventDefault (drag stays possible)', () => {
    const { host } = mount('<doc><p>x</p><figure><img src="a.png" alt="p"></img></figure></doc>')
    const img = host.querySelector('img') as HTMLElement
    const md = fireMouse(img, 'mousedown')
    expect(md.defaultPrevented).toBe(false)
  })

  it('click on an image selects it as a node (resize overlay appears)', () => {
    const { host } = mount('<doc><p>x</p><figure><img src="a.png" alt="p"></img></figure></doc>')
    const img = host.querySelector('img') as HTMLElement
    fireMouse(img, 'click')
    expect(host.querySelector('.rdt-img-overlay')).not.toBeNull()
  })
})

describe('FullEditorDom — highlight active state', () => {
  const caret = textSelection(pos([0, 0], 1), pos([0, 0], 1))

  it('highlight button lights up when the caret is in highlighted text', () => {
    const doc = node('doc', [node('p', [{ ...text('hi'), marks: { background: '#fef08a' } }])])
    const { host } = mountDoc(doc, html5SubsetSchema, caret)
    expect(toolbarBtn(host, 'Highlight').className).toContain('is-active')
  })

  it('highlight button is not active in plain text', () => {
    const doc = node('doc', [node('p', [text('hi')])])
    const { host } = mountDoc(doc, html5SubsetSchema, caret)
    expect(toolbarBtn(host, 'Highlight').className).not.toContain('is-active')
  })
})

describe('FullEditorDom — emoji picker', () => {
  it('opening the picker and clicking an emoji inserts it at the caret', () => {
    const { host } = mount('<doc><p>hi<cursor></cursor></p></doc>')
    const openBtn = toolbarBtn(host, 'Insert emoji')
    expect(openBtn).toBeTruthy()
    fireMouse(openBtn, 'click')
    const emojiBtn = host.querySelector('.rdt-emoji-palette .rdt-emoji') as HTMLElement
    expect(emojiBtn).toBeTruthy()
    const emoji = emojiBtn.textContent as string
    fireMouse(emojiBtn, 'click')
    expect(host.textContent).toContain(emoji)
  })
})
