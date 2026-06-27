import { describe, it, expect } from 'vitest'
import { node, nodeAt, text } from '../../src/model/doc.js'
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

describe('commands/cmdInsertText — empty block (bug 4)', () => {
  it('types into an empty paragraph (caret at node position)', () => {
    // empty <p>, caret at {path:[0], offset:0}
    const s = state('doc', [node('p', [])], textSelection(pos([0], 0), pos([0], 0)))
    const tx = cmdInsertText(s, 'hi')!
    expect(textAt(tx.doc_after, [0, 0])).toBe('hi')
    expect(tx.sel_after).toEqual(caret([0, 0], 2))
  })

  it('types into an empty list item', () => {
    const s = state('doc', [node('ul', [node('li', [])])], textSelection(pos([0, 0], 0), pos([0, 0], 0)))
    const tx = cmdInsertText(s, 'x')!
    expect(textAt(tx.doc_after, [0, 0, 0])).toBe('x')
  })

  it('clamps an over-large node offset (DOM placeholder <br>)', () => {
    const s = state('doc', [node('p', [])], textSelection(pos([0], 1), pos([0], 1)))
    const tx = cmdInsertText(s, 'z')!
    expect(textAt(tx.doc_after, [0, 0])).toBe('z')
  })
})

describe('commands/cmdInsertParagraph — empty block (bug 2)', () => {
  it('repeated Enter in an empty paragraph adds blank paragraphs', () => {
    let s = state('doc', [node('p', [])], textSelection(pos([0], 0), pos([0], 0)))
    // first Enter
    let tx = cmdInsertParagraph(s)!
    expect(tx.doc_after.content.length).toBe(2)
    // second Enter from the resulting caret
    s = { ...s, doc: tx.doc_after, selection: tx.sel_after }
    tx = cmdInsertParagraph(s)!
    expect(tx.doc_after.content.length).toBe(3)
    // third
    s = { ...s, doc: tx.doc_after, selection: tx.sel_after }
    tx = cmdInsertParagraph(s)!
    expect(tx.doc_after.content.length).toBe(4)
    expect(tx.doc_after.content.every((b: any) => b.tag === 'p')).toBe(true)
  })

  it('Enter in an empty list item adds another empty <li>', () => {
    const s = state('doc', [node('ul', [node('li', [])])], textSelection(pos([0, 0], 0), pos([0, 0], 0)))
    const tx = cmdInsertParagraph(s)!
    const ul = nodeAt(tx.doc_after, [0]) as any
    expect(ul.content.length).toBe(2)
    expect(ul.content.every((li: any) => li.tag === 'li')).toBe(true)
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
  it('at the end of a leaf inserts a <br> with no trailing empty leaf', () => {
    const s = state('doc', [node('p', [text('hello')])], caret([0, 0], 5))
    const tx = cmdInsertLineBreak(s)!
    const p = tx.doc_after.content[0] as any
    // expect: [text("hello"), <br>] — NOT a trailing empty text leaf
    expect(p.content.length).toBe(2)
    expect(p.content[0]).toEqual({ kind: 'text', text: 'hello', marks: {} })
    expect(p.content[1].tag).toBe('br')
  })

  it('at the start of a leaf inserts a <br> with no leading empty leaf', () => {
    const s = state('doc', [node('p', [text('hello')])], caret([0, 0], 0))
    const tx = cmdInsertLineBreak(s)!
    const p = tx.doc_after.content[0] as any
    expect(p.content.length).toBe(2)
    expect(p.content[0].tag).toBe('br')
    expect(p.content[1]).toEqual({ kind: 'text', text: 'hello', marks: {} })
  })

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

describe('commands/cmdDeleteBackward — join backward (divergence fix)', () => {
  it('merges a block into the previous block at block start', () => {
    const s = state('doc', [node('p', [text('a')]), node('p', [text('b')])], caret([1, 0], 0))
    const tx = cmdDeleteBackward(s)!
    expect(tx.doc_after.content).toEqual([node('p', [text('ab')])])
    expect(tx.sel_after).toEqual(caret([0, 0], 1))
  })

  it('merges a paragraph into a previous heading (keeps the heading tag)', () => {
    const s = state('doc', [node('h1', [text('Title')]), node('p', [text('body')])], caret([1, 0], 0))
    const tx = cmdDeleteBackward(s)!
    expect(tx.doc_after.content).toEqual([node('h1', [text('Titlebody')])])
    expect(tx.sel_after).toEqual(caret([0, 0], 5))
  })

  it('merges a paragraph into the last item of a previous list', () => {
    const s = state('doc', [node('ul', [node('li', [text('x')])]), node('p', [text('y')])], caret([1, 0], 0))
    const tx = cmdDeleteBackward(s)!
    expect(tx.doc_after.content).toEqual([node('ul', [node('li', [text('xy')])])])
    expect(tx.sel_after).toEqual(caret([0, 0, 0], 1))
  })

  it('returns null at the first block (nothing to merge into)', () => {
    const s = state('doc', [node('p', [text('a')]), node('p', [text('b')])], caret([0, 0], 0))
    expect(cmdDeleteBackward(s)).toBeNull()
  })

  it('merges an empty block into the previous block', () => {
    const s = state('doc', [node('p', [text('a')]), node('p', [])], caret([1], 0))
    const tx = cmdDeleteBackward(s)!
    expect(tx.doc_after.content).toEqual([node('p', [text('a')])])
  })
})

describe('commands/cmdDeleteBackward — multi-block range delete (select-all fix)', () => {
  it('empties the doc on a full-document range', () => {
    const s = state('doc', [node('p', [text('ab')]), node('p', [text('cd')])],
      textSelection(pos([0, 0], 0), pos([1, 0], 2)))
    const tx = cmdDeleteBackward(s)!
    expect(tx.doc_after.content).toEqual([node('p', [])])
    expect(tx.sel_after).toEqual(caret([0], 0))
  })

  it('deletes a partial multi-block range, joining the ends into the start block', () => {
    const s = state('doc', [node('p', [text('abc')]), node('p', [text('def')])],
      textSelection(pos([0, 0], 1), pos([1, 0], 2)))
    const tx = cmdDeleteBackward(s)!
    expect(tx.doc_after.content).toEqual([node('p', [text('af')])])
    expect(tx.sel_after).toEqual(caret([0, 0], 1))
  })

  it('select-all then delete leaves a single empty block', () => {
    const s = state('doc', [node('h1', [text('Title')]), node('p', [text('body')])],
      textSelection(pos([0, 0], 0), pos([1, 0], 4)))
    const tx = cmdDeleteBackward(s)!
    expect(tx.doc_after.content).toEqual([node('h1', [])])
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
