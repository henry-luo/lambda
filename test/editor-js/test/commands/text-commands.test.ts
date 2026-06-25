import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import {
  cmdDeleteBackward,
  cmdDeleteForward,
  cmdFormatBold,
  cmdInsertLineBreak,
  cmdInsertParagraph,
  cmdInsertText,
  cmdSelectAll,
  cmdSetBlockType
} from '../../src/commands/text-commands.js'
import type { EditorState } from '../../src/commands/types.js'
import type { Selection } from '../../src/model/types.js'
import { marksAt, tagAt, textAt } from '../helpers/narrow.js'

function state(docTag: string, blocks: any[], sel: Selection): EditorState {
  return {
    doc: node(docTag, blocks),
    schema: html5SubsetSchema,
    selection: sel,
    stored_marks: null
  }
}

function caret(path: number[], offset: number) {
  return textSelection(pos(path, offset), pos(path, offset))
}

describe('commands/cmdInsertText', () => {
  it('inserts text at the caret in a single leaf', () => {
    const s = state('doc', [node('p', [text('helo')])], caret([0, 0], 2))
    const tx = cmdInsertText(s, 'l')!
    expect(textAt(tx.doc_after, [0, 0])).toBe('hello')
    expect(tx.sel_after).toEqual(caret([0, 0], 3))
  })

  it('replaces a non-collapsed selection in the same leaf', () => {
    const s = state('doc', [node('p', [text('helXXo')])], textSelection(pos([0, 0], 3), pos([0, 0], 5)))
    const tx = cmdInsertText(s, 'l')!
    expect(textAt(tx.doc_after, [0, 0])).toBe('hello')
  })

  it('returns null when no selection', () => {
    const s = state('doc', [node('p', [text('x')])], null as any)
    expect(cmdInsertText(s, 'y')).toBeNull()
  })
})

describe('commands/cmdInsertParagraph (split_block)', () => {
  it('splits a paragraph at the caret', () => {
    const s = state('doc', [node('p', [text('hello world')])], caret([0, 0], 6))
    const tx = cmdInsertParagraph(s)!
    expect(tx.doc_after.content.length).toBe(2)
    expect(textAt(tx.doc_after, [0, 0])).toBe('hello ')
    expect(textAt(tx.doc_after, [1, 0])).toBe('world')
    expect(tagAt(tx.doc_after, [0])).toBe('p')
    expect(tagAt(tx.doc_after, [1])).toBe('p')
    expect(tx.sel_after).toEqual(caret([1, 0], 0))
  })

  it('splits at start → empty first block', () => {
    const s = state('doc', [node('p', [text('hello')])], caret([0, 0], 0))
    const tx = cmdInsertParagraph(s)!
    expect(tx.doc_after.content.length).toBe(2)
    expect(tagAt(tx.doc_after, [0])).toBe('p')
    expect((tx.doc_after.content[0] as any).content).toEqual([])
  })

  it('splits at end → empty second block; default tag is p', () => {
    const s = state('doc', [node('h1', [text('Title')])], caret([0, 0], 5))
    const tx = cmdInsertParagraph(s)!
    expect(tx.doc_after.content.length).toBe(2)
    expect(tagAt(tx.doc_after, [0])).toBe('h1')
    expect(tagAt(tx.doc_after, [1])).toBe('p')
  })
})

describe('commands/cmdInsertLineBreak', () => {
  it('inserts a <br> in the middle of a leaf', () => {
    const s = state('doc', [node('p', [text('hello')])], caret([0, 0], 3))
    const tx = cmdInsertLineBreak(s)!
    // expect: [text("hel"), <br>, text("lo")]
    const p = tx.doc_after.content[0] as any
    expect(p.content.length).toBe(3)
    expect(p.content[0]).toEqual({ kind: 'text', text: 'hel', marks: {} })
    expect(p.content[1].tag).toBe('br')
    expect(p.content[2]).toEqual({ kind: 'text', text: 'lo', marks: {} })
  })
})

describe('commands/cmdDeleteBackward', () => {
  it('deletes one char back in the same leaf', () => {
    const s = state('doc', [node('p', [text('hello')])], caret([0, 0], 3))
    const tx = cmdDeleteBackward(s)!
    expect(textAt(tx.doc_after, [0, 0])).toBe('helo')
    expect(tx.sel_after).toEqual(caret([0, 0], 2))
  })

  it('returns null at start of leaf (boundary)', () => {
    const s = state('doc', [node('p', [text('hello')])], caret([0, 0], 0))
    expect(cmdDeleteBackward(s)).toBeNull()
  })

  it('deletes the selection if non-collapsed', () => {
    const s = state('doc', [node('p', [text('helXXo')])], textSelection(pos([0, 0], 3), pos([0, 0], 5)))
    const tx = cmdDeleteBackward(s)!
    expect(textAt(tx.doc_after, [0, 0])).toBe('helo')
  })
})

describe('commands/cmdDeleteForward', () => {
  it('deletes one char forward', () => {
    const s = state('doc', [node('p', [text('hello')])], caret([0, 0], 2))
    const tx = cmdDeleteForward(s)!
    expect(textAt(tx.doc_after, [0, 0])).toBe('helo')
    expect(tx.sel_after).toEqual(caret([0, 0], 2))
  })
})

describe('commands/cmdFormatBold (toggleMark with leaf-split)', () => {
  it('splits a leaf and bolds the middle when range is partial', () => {
    // "hello", select offsets 1..4 → before "h", target "ell", after "o"
    const s = state('doc', [node('p', [text('hello')])], textSelection(pos([0, 0], 1), pos([0, 0], 4)))
    const tx = cmdFormatBold(s)!
    // 3 sub-leaves
    const p = tx.doc_after.content[0] as any
    expect(p.content.length).toBe(3)
    expect(p.content[0].text).toBe('h')
    expect(p.content[1].text).toBe('ell')
    expect(p.content[2].text).toBe('o')
    expect(p.content[1].marks).toEqual({ bold: true })
    expect(p.content[0].marks).toEqual({})
    expect(p.content[2].marks).toEqual({})
  })

  it('full-leaf range becomes a single bolded leaf (no split)', () => {
    const s = state('doc', [node('p', [text('hi')])], textSelection(pos([0, 0], 0), pos([0, 0], 2)))
    const tx = cmdFormatBold(s)!
    const p = tx.doc_after.content[0] as any
    expect(p.content.length).toBe(1)
    expect(p.content[0].marks).toEqual({ bold: true })
  })

  it('toggles off when already present', () => {
    const s = state(
      'doc',
      [node('p', [{ kind: 'text' as const, text: 'hi', marks: { bold: true } }])],
      textSelection(pos([0, 0], 0), pos([0, 0], 2))
    )
    const tx = cmdFormatBold(s)!
    expect(marksAt(tx.doc_after, [0, 0])).toEqual({})
  })

  it('collapsed selection → stored-mark toggle (no doc change)', () => {
    const s = state('doc', [node('p', [text('x')])], caret([0, 0], 1))
    const tx = cmdFormatBold(s)!
    expect(tx.steps).toEqual([])
    expect(tx.meta.find(m => m.name === 'storedMarks')?.value).toEqual({ bold: true })
  })
})

describe('commands/cmdSetBlockType', () => {
  it('retags the containing block', () => {
    const s = state('doc', [node('p', [text('Title')])], caret([0, 0], 0))
    const tx = cmdSetBlockType(s, 'h1')!
    expect(tagAt(tx.doc_after, [0])).toBe('h1')
    expect(textAt(tx.doc_after, [0, 0])).toBe('Title')
  })

  it('refuses non-block tags', () => {
    const s = state('doc', [node('p', [text('x')])], caret([0, 0], 0))
    expect(cmdSetBlockType(s, 'strong')).toBeNull()
  })
})

describe('commands/cmdSelectAll', () => {
  it('sets selection to TextSelection spanning the doc-root boundaries', () => {
    const s = state('doc', [node('p', [text('x')]), node('p', [text('y')])], caret([0, 0], 0))
    const tx = cmdSelectAll(s)!
    expect(tx.sel_after).toEqual({
      kind: 'text',
      anchor: { path: [], offset: 0 },
      head:   { path: [], offset: 2 }
    })
    expect(tx.steps).toEqual([])
    expect(tx.meta.find(m => m.name === 'addToHistory')?.value).toBe(false)
  })
})
