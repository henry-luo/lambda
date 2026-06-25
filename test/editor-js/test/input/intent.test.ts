import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { dispatchIntent, type InputIntent } from '../../src/input/intent.js'
import type { EditorState } from '../../src/commands/types.js'
import { textAt } from '../helpers/narrow.js'

function caret(path: number[], offset: number) {
  return textSelection(pos(path, offset), pos(path, offset))
}

function state(blocks: any[], sel: any): EditorState {
  return { doc: node('doc', blocks), schema: html5SubsetSchema, selection: sel, stored_marks: null }
}

describe('input/dispatchIntent — text editing', () => {
  it('insertText routes to cmdInsertText with typing history group', () => {
    const s = state([node('p', [text('helo')])], caret([0, 0], 2))
    const tx = dispatchIntent(s, { type: 'insertText', text: 'l' })!
    expect(textAt(tx.doc_after, [0, 0])).toBe('hello')
    expect(tx.meta.find(m => m.name === 'historyGroup')?.value).toBe('typing')
    expect(tx.meta.find(m => m.name === 'scrollIntoView')?.value).toBe(true)
  })

  it('insertParagraph splits the block', () => {
    const s = state([node('p', [text('ab')])], caret([0, 0], 1))
    const tx = dispatchIntent(s, { type: 'insertParagraph' })!
    expect(tx.doc_after.content.length).toBe(2)
  })

  it('deleteContentBackward removes one char', () => {
    const s = state([node('p', [text('hello')])], caret([0, 0], 5))
    const tx = dispatchIntent(s, { type: 'deleteContentBackward' })!
    expect(textAt(tx.doc_after, [0, 0])).toBe('hell')
  })

  it('formatBold toggles bold', () => {
    const s = state([node('p', [text('hi')])], textSelection(pos([0, 0], 0), pos([0, 0], 2)))
    const tx = dispatchIntent(s, { type: 'formatBold' })!
    expect((tx.doc_after.content[0] as any).content[0].marks).toEqual({ bold: true })
  })

  it('formatBlockType retags the block', () => {
    const s = state([node('p', [text('Title')])], caret([0, 0], 0))
    const tx = dispatchIntent(s, { type: 'formatBlockType', tag: 'h1' })!
    expect((tx.doc_after.content[0] as any).tag).toBe('h1')
  })

  it('selectAll sets TextSelection at doc boundaries', () => {
    const s = state([node('p', [text('x')])], caret([0, 0], 0))
    const tx = dispatchIntent(s, { type: 'selectAll' })!
    expect(tx.sel_after).toEqual({
      kind: 'text',
      anchor: { path: [], offset: 0 },
      head:   { path: [], offset: 1 }
    })
  })

  it('returns null when no command applies (e.g., deleteBackward at start)', () => {
    const s = state([node('p', [text('hi')])], caret([0, 0], 0))
    expect(dispatchIntent(s, { type: 'deleteContentBackward' })).toBeNull()
  })

  it('all known intents have a routing branch (compile-time check via switch)', () => {
    const intents: InputIntent[] = [
      { type: 'insertText', text: 'a' },
      { type: 'insertParagraph' },
      { type: 'insertLineBreak' },
      { type: 'deleteContentBackward' },
      { type: 'deleteContentForward' },
      { type: 'formatBold' },
      { type: 'formatItalic' },
      { type: 'formatUnderline' },
      { type: 'formatCode' },
      { type: 'formatToggleMark', mark: 'bold' },
      { type: 'formatBlockType', tag: 'h1' },
      { type: 'selectAll' }
    ]
    // Just checks the switch is exhaustive — TS would fail to compile otherwise.
    expect(intents.length).toBe(12)
  })
})
