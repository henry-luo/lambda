// Plain-DOM controller parity (Stage 4B) — the framework-free twin of
// editor-view.test.tsx. Drives EditorViewDom with native events and asserts the
// same beforeinput → InputIntent → command → Transaction pipeline.
import { afterEach, describe, expect, it, vi } from 'vitest'
import { EditorViewDom } from '../../src/view/editor-view-dom.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'
import type { EditorViewState } from '../../src/view/editor-state.js'
import { textAt } from '../helpers/narrow.js'

const mounted: HTMLElement[] = []
afterEach(() => {
  for (const el of mounted.splice(0)) el.remove()
})

function mount(html: string) {
  const { doc, selection } = parseHtmlToDoc(html, html5SubsetSchema)
  const root = document.createElement('div')
  document.body.appendChild(root)
  mounted.push(root)
  const seenStates: EditorViewState[] = []
  const view = new EditorViewDom(root, {
    doc, schema: html5SubsetSchema, initialSelection: selection,
    onChange: s => seenStates.push(s)
  })
  return { root, view, seenStates }
}

function fireBeforeInput(el: HTMLElement, init: { inputType: string; data?: string | null }) {
  el.dispatchEvent(new InputEvent('beforeinput', {
    inputType: init.inputType,
    data: init.data ?? null,
    bubbles: true,
    cancelable: true
  }))
}

function fireKeyDown(el: HTMLElement, init: KeyboardEventInit) {
  el.dispatchEvent(new KeyboardEvent('keydown', { ...init, bubbles: true, cancelable: true }))
}

describe('view/EditorViewDom — rendering', () => {
  it('renders the doc text', () => {
    const { root } = mount('<doc><p>hello</p></doc>')
    expect(root.textContent).toBe('hello')
  })

  it('marks editable and exposes data-source-path on rendered nodes', () => {
    const { root } = mount('<doc><p>hi</p></doc>')
    expect(root.getAttribute('contenteditable')).toBe('true')
    expect(root.querySelector('[data-source-path="0,0"]')).not.toBeNull()
  })
})

describe('view/EditorViewDom — typing pipeline', () => {
  it('insertText routes through beforeinput → InputIntent → command', () => {
    const { root, seenStates } = mount('<doc><p>helo<cursor></cursor></p></doc>')
    fireBeforeInput(root, { inputType: 'insertText', data: '!' })
    expect(textAt(seenStates[seenStates.length - 1]!.doc, [0, 0])).toBe('helo!')
  })

  it('deleteContentBackward removes the previous char', () => {
    const { root, seenStates } = mount('<doc><p>hello<cursor></cursor></p></doc>')
    fireBeforeInput(root, { inputType: 'deleteContentBackward' })
    expect(textAt(seenStates[seenStates.length - 1]!.doc, [0, 0])).toBe('hell')
  })

  it('insertParagraph splits the block', () => {
    const { root, seenStates } = mount('<doc><p>ab<cursor></cursor>cd</p></doc>')
    fireBeforeInput(root, { inputType: 'insertParagraph' })
    const last = seenStates[seenStates.length - 1]!
    expect(last.doc.content.length).toBe(2)
    expect(textAt(last.doc, [0, 0])).toBe('ab')
    expect(textAt(last.doc, [1, 0])).toBe('cd')
  })

  it('the rendered DOM reflects the edit (reconciled in place)', () => {
    const { root } = mount('<doc><p>helo<cursor></cursor></p></doc>')
    fireBeforeInput(root, { inputType: 'insertText', data: '!' })
    expect(root.textContent).toBe('helo!')
  })

  it('undo / redo via Cmd+Z reverts and reapplies the last typing', () => {
    const { root, seenStates } = mount('<doc><p>helo<cursor></cursor></p></doc>')
    fireBeforeInput(root, { inputType: 'insertText', data: '!' })
    expect(textAt(seenStates[seenStates.length - 1]!.doc, [0, 0])).toBe('helo!')
    fireKeyDown(root, { key: 'z', metaKey: true })
    expect(textAt(seenStates[seenStates.length - 1]!.doc, [0, 0])).toBe('helo')
    fireKeyDown(root, { key: 'z', metaKey: true, shiftKey: true })
    expect(textAt(seenStates[seenStates.length - 1]!.doc, [0, 0])).toBe('helo!')
  })

  it('unsupported inputType is left alone (no exception, no state change)', () => {
    const { root, seenStates } = mount('<doc><p>x<cursor></cursor></p></doc>')
    const before = seenStates.length
    fireBeforeInput(root, { inputType: 'insertReplacementText', data: 'y' })
    expect(seenStates.length).toBe(before)
  })

  it('onChange is called on every state mutation', () => {
    const onChange = vi.fn()
    const { doc, selection } = parseHtmlToDoc('<doc><p>x<cursor></cursor></p></doc>', html5SubsetSchema)
    const root = document.createElement('div')
    document.body.appendChild(root)
    mounted.push(root)
    new EditorViewDom(root, { doc, schema: html5SubsetSchema, initialSelection: selection, onChange })
    fireBeforeInput(root, { inputType: 'insertText', data: 'y' })
    expect(onChange).toHaveBeenCalled()
  })
})
