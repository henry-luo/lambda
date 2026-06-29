// Covers the Tab / Shift+Tab key → indent/outdent wiring in the editor view
// (the keybinding in demo/full-editor.tsx's handleKeyDown). The underlying
// commands are unit-tested separately; this exercises the key→command path and
// that the indent level reaches the rendered DOM (margin-inline-start).
import { afterEach, describe, expect, it } from 'vitest'
import { act, fireEvent, render, cleanup } from '@testing-library/react'
import { FullEditor } from '../../demo/full-editor.js'
import { docSchema } from '../../src/schemas/doc.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'

afterEach(() => cleanup())

function mount(html: string) {
  const { doc, selection } = parseHtmlToDoc(html, docSchema)
  return render(<FullEditor doc={doc} schema={docSchema} initialSelection={selection} />)
}

function surface(c: HTMLElement): HTMLElement {
  return c.querySelector('.rdt-surface') as HTMLElement
}

function pressTab(c: HTMLElement, shift = false): void {
  act(() => { fireEvent.keyDown(surface(c), { key: 'Tab', shiftKey: shift }) })
}

function liMargin(c: HTMLElement, i: number): string {
  return (c.querySelectorAll('li')[i] as HTMLElement).style.marginInlineStart
}

describe('view — Tab/Shift-Tab list indent keybinding', () => {
  it('Tab indents the current list item (margin applied)', () => {
    const { container } = mount('<doc><ul><li>a</li><li>b<cursor></cursor></li></ul></doc>')
    expect(liMargin(container, 1)).toBe('')          // no indent yet
    pressTab(container)
    expect(liMargin(container, 1)).toBe('1.75em')    // level 1
    expect(liMargin(container, 0)).toBe('')          // sibling untouched
  })

  it('Shift+Tab outdents the current list item (margin removed)', () => {
    const { container } = mount('<doc><ul><li>a</li><li>b<cursor></cursor></li></ul></doc>')
    pressTab(container)
    expect(liMargin(container, 1)).toBe('1.75em')
    pressTab(container, true)
    expect(liMargin(container, 1)).toBe('')          // back to level 0
  })

  it('Tab works on the FIRST item and repeats (Word/Docs style)', () => {
    const { container } = mount('<doc><ul><li>a<cursor></cursor></li><li>b</li></ul></doc>')
    pressTab(container)
    expect(liMargin(container, 0)).toBe('1.75em')    // first item indents
    pressTab(container)
    expect(liMargin(container, 0)).toBe('3.5em')     // repeats → level 2
  })

  it('Tab is a no-op outside a list (and does not insert a tab char)', () => {
    const { container } = mount('<doc><p>hi<cursor></cursor></p></doc>')
    pressTab(container)
    expect(surface(container).textContent).toBe('hi')   // editor content unchanged
    expect(container.querySelectorAll('li').length).toBe(0)
  })
})
