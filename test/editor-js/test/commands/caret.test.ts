import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { cmdMoveCaret } from '../../src/commands/caret.js'
import type { EditorState } from '../../src/commands/types.js'
import type { Selection } from '../../src/model/types.js'

function state(blocks: any[], sel: Selection): EditorState {
  return { doc: node('doc', blocks), schema: html5SubsetSchema, selection: sel, stored_marks: null }
}
function caret(path: number[], offset: number) {
  return textSelection(pos(path, offset), pos(path, offset))
}

describe('commands/cmdMoveCaret — character', () => {
  it('moves the caret forward one character within a leaf', () => {
    const s = state([node('p', [text('hello')])], caret([0, 0], 1))
    const tx = cmdMoveCaret(s, 'move', 'forward', 'character')!
    expect(tx.sel_after).toEqual(caret([0, 0], 2))
  })

  it('moves backward one character', () => {
    const s = state([node('p', [text('hello')])], caret([0, 0], 3))
    const tx = cmdMoveCaret(s, 'move', 'backward', 'character')!
    expect(tx.sel_after).toEqual(caret([0, 0], 2))
  })

  it("'right'/'left' alias forward/backward", () => {
    const s = state([node('p', [text('hello')])], caret([0, 0], 2))
    expect(cmdMoveCaret(s, 'move', 'right', 'character')!.sel_after).toEqual(caret([0, 0], 3))
    expect(cmdMoveCaret(s, 'move', 'left', 'character')!.sel_after).toEqual(caret([0, 0], 1))
  })

  it('crosses a block boundary when moving past a leaf end', () => {
    const s = state([node('p', [text('ab')]), node('p', [text('cd')])], caret([0, 0], 2))
    const tx = cmdMoveCaret(s, 'move', 'forward', 'character')!
    // next stop after (p0,2) is the first stop of p1
    expect(tx.sel_after).toEqual(caret([1, 0], 0))
  })

  it('clamps at the document end', () => {
    const s = state([node('p', [text('ab')])], caret([0, 0], 2))
    const tx = cmdMoveCaret(s, 'move', 'forward', 'character')!
    expect(tx.sel_after).toEqual(caret([0, 0], 2))
  })

  it('does not mutate the document and is excluded from history', () => {
    const s = state([node('p', [text('hello')])], caret([0, 0], 1))
    const tx = cmdMoveCaret(s, 'move', 'forward', 'character')!
    expect(tx.steps.length).toBe(0)
    expect(tx.doc_after).toEqual(s.doc)
  })
})

describe('commands/cmdMoveCaret — extend', () => {
  it("'extend' keeps the anchor and moves the head", () => {
    const s = state([node('p', [text('hello')])], caret([0, 0], 1))
    const tx = cmdMoveCaret(s, 'extend', 'forward', 'character')!
    expect(tx.sel_after).toEqual(textSelection(pos([0, 0], 1), pos([0, 0], 2)))
  })
})

describe('commands/cmdMoveCaret — word', () => {
  it('moves to the next word boundary', () => {
    const s = state([node('p', [text('hello world here')])], caret([0, 0], 0))
    const tx = cmdMoveCaret(s, 'move', 'forward', 'word')!
    // lands at the end of "hello" (offset 5)
    expect(tx.sel_after).toEqual(caret([0, 0], 5))
  })

  it('moves to the previous word boundary', () => {
    const s = state([node('p', [text('hello world here')])], caret([0, 0], 16))
    const tx = cmdMoveCaret(s, 'move', 'backward', 'word')!
    // lands at the start of "here" (offset 12)
    expect(tx.sel_after).toEqual(caret([0, 0], 12))
  })
})

describe('commands/cmdMoveCaret — documentboundary', () => {
  it('jumps to the document end / start', () => {
    const s = state([node('p', [text('ab')]), node('p', [text('cd')])], caret([0, 0], 1))
    expect(cmdMoveCaret(s, 'move', 'forward', 'documentboundary')!.sel_after).toEqual(caret([1, 0], 2))
    expect(cmdMoveCaret(s, 'move', 'backward', 'documentboundary')!.sel_after).toEqual(caret([0, 0], 0))
  })
})

describe('commands/cmdMoveCaret — guards', () => {
  it('returns null without a text selection', () => {
    const s = state([node('p', [text('x')])], null as any)
    expect(cmdMoveCaret(s, 'move', 'forward', 'character')).toBeNull()
  })
})
