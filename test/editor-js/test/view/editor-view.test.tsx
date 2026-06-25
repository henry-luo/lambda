import { afterEach, describe, expect, it, vi } from 'vitest'
import { act, fireEvent, render, cleanup } from '@testing-library/react'
import { EditorView } from '../../src/view/EditorView.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'
import type { EditorViewState } from '../../src/view/use-editor-state.js'
import { textAt } from '../helpers/narrow.js'

function fireBeforeInput(el: HTMLElement, init: { inputType: string; data?: string | null }) {
  act(() => {
    const ev = new InputEvent('beforeinput', {
      inputType: init.inputType,
      data: init.data ?? null,
      bubbles: true,
      cancelable: true
    })
    el.dispatchEvent(ev)
  })
}

afterEach(() => cleanup())

function mount(html: string) {
  const { doc, selection } = parseHtmlToDoc(html, html5SubsetSchema)
  const seenStates: EditorViewState[] = []
  const utils = render(
    <EditorView
      doc={doc}
      schema={html5SubsetSchema}
      initialSelection={selection}
      onChange={s => seenStates.push(s)}
    />
  )
  return { ...utils, seenStates }
}

describe('view/EditorView — rendering', () => {
  it('renders an empty doc', () => {
    const { container } = mount('<doc><p>hello</p></doc>')
    expect(container.textContent).toBe('hello')
  })

  it('marks editable and exposes data-source-path on rendered nodes', () => {
    const { container } = mount('<doc><p>hi</p></doc>')
    const root = container.firstElementChild as HTMLElement
    expect(root.getAttribute('contenteditable')).toBe('true')
    expect(container.querySelector('[data-source-path="0,0"]')).not.toBeNull()
  })
})

describe('view/EditorView — typing pipeline', () => {
  it('insertText routes through beforeinput → InputIntent → command', () => {
    const { container, seenStates } = mount('<doc><p>helo<cursor></cursor></p></doc>')
    const editable = container.querySelector('[contenteditable]') as HTMLElement
    fireBeforeInput(editable, { inputType: 'insertText', data: '!' })
    const last = seenStates[seenStates.length - 1]!
    expect(textAt(last.doc, [0, 0])).toBe('helo!')
  })

  it('deleteContentBackward removes the previous char', () => {
    const { container, seenStates } = mount('<doc><p>hello<cursor></cursor></p></doc>')
    const editable = container.querySelector('[contenteditable]') as HTMLElement
    fireBeforeInput(editable, { inputType: 'deleteContentBackward' })
    const last = seenStates[seenStates.length - 1]!
    expect(textAt(last.doc, [0, 0])).toBe('hell')
  })

  it('insertParagraph splits the block', () => {
    const { container, seenStates } = mount('<doc><p>ab<cursor></cursor>cd</p></doc>')
    const editable = container.querySelector('[contenteditable]') as HTMLElement
    fireBeforeInput(editable, { inputType: 'insertParagraph' })
    const last = seenStates[seenStates.length - 1]!
    expect(last.doc.content.length).toBe(2)
    expect(textAt(last.doc, [0, 0])).toBe('ab')
    expect(textAt(last.doc, [1, 0])).toBe('cd')
  })

  it('undo via Cmd+Z reverts the last typing', () => {
    const { container, seenStates } = mount('<doc><p>helo<cursor></cursor></p></doc>')
    const editable = container.querySelector('[contenteditable]') as HTMLElement
    fireBeforeInput(editable, { inputType: 'insertText', data: '!' })
    expect(textAt(seenStates[seenStates.length - 1]!.doc, [0, 0])).toBe('helo!')

    fireEvent.keyDown(editable, { key: 'z', metaKey: true })
    expect(textAt(seenStates[seenStates.length - 1]!.doc, [0, 0])).toBe('helo')

    fireEvent.keyDown(editable, { key: 'z', metaKey: true, shiftKey: true })
    expect(textAt(seenStates[seenStates.length - 1]!.doc, [0, 0])).toBe('helo!')
  })

  it('unsupported inputType is left alone (no exception, no state change)', () => {
    const { container, seenStates } = mount('<doc><p>x<cursor></cursor></p></doc>')
    const editable = container.querySelector('[contenteditable]') as HTMLElement
    const before = seenStates.length
    fireBeforeInput(editable, { inputType: 'insertReplacementText', data: 'y' })
    expect(seenStates.length).toBe(before)
  })

  it('onChange is called on every state mutation', () => {
    const onChange = vi.fn()
    const parsed = parseHtmlToDoc('<doc><p>x<cursor></cursor></p></doc>', html5SubsetSchema)
    const { container } = render(
      <EditorView
        doc={parsed.doc}
        schema={html5SubsetSchema}
        initialSelection={parsed.selection}
        onChange={onChange}
      />
    )
    const editable = container.querySelector('[contenteditable]') as HTMLElement
    fireBeforeInput(editable, { inputType: 'insertText', data: 'y' })
    expect(onChange).toHaveBeenCalled()
  })
})
