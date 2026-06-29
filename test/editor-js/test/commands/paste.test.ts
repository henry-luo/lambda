import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { cmdPasteSlice } from '../../src/commands/paste.js'
import type { EditorState } from '../../src/commands/types.js'
import type { Child, Selection } from '../../src/model/types.js'

function state(blocks: any[], sel: Selection): EditorState {
  return { doc: node('doc', blocks), schema: html5SubsetSchema, selection: sel, stored_marks: null }
}
function caret(path: number[], offset: number) {
  return textSelection(pos(path, offset), pos(path, offset))
}
const bold = (t: string): Child => ({ kind: 'text', text: t, marks: { bold: true } })

describe('commands/cmdPasteSlice — inline', () => {
  it('splices and merges plain inline text at the caret', () => {
    const s = state([node('p', [text('ab')])], caret([0, 0], 1))
    const tx = cmdPasteSlice(s, [text('XY')])!
    expect(tx.doc_after.content[0]).toEqual(node('p', [text('aXYb')]))
    expect(tx.sel_after).toEqual(caret([0, 0], 3))
  })

  it('keeps a distinct-mark inline run separate', () => {
    const s = state([node('p', [text('ab')])], caret([0, 0], 1))
    const tx = cmdPasteSlice(s, [bold('XY')])!
    const p = tx.doc_after.content[0] as any
    expect(p.content).toEqual([text('a'), bold('XY'), text('b')])
    // caret at the end of the pasted bold run
    expect(tx.sel_after).toEqual(caret([0, 1], 2))
  })

  it('pastes into an empty block', () => {
    const s = state([node('p', [])], caret([0], 0))
    const tx = cmdPasteSlice(s, [text('hi')])!
    expect(tx.doc_after.content[0]).toEqual(node('p', [text('hi')]))
    expect(tx.sel_after).toEqual(caret([0, 0], 2))
  })

  it('replaces a range within one leaf, then inserts', () => {
    const s = state([node('p', [text('abcd')])], textSelection(pos([0, 0], 1), pos([0, 0], 3)))
    const tx = cmdPasteSlice(s, [text('X')])!
    expect(tx.doc_after.content[0]).toEqual(node('p', [text('aXd')]))
    expect(tx.sel_after).toEqual(caret([0, 0], 2))
  })
})

describe('commands/cmdPasteSlice — block', () => {
  it('merges a single pasted block inline (no split)', () => {
    const s = state([node('p', [text('abcd')])], caret([0, 0], 2))
    const tx = cmdPasteSlice(s, [node('p', [text('XY')])])!
    expect(tx.doc_after.content).toEqual([node('p', [text('abXYcd')])])
    expect(tx.sel_after).toEqual(caret([0, 0], 4))
  })

  it('splits the current block for a multi-block paste', () => {
    const s = state([node('p', [text('abcd')])], caret([0, 0], 2))
    const tx = cmdPasteSlice(s, [node('p', [text('P')]), node('p', [text('Q')])])!
    expect(tx.doc_after.content).toEqual([
      node('p', [text('abP')]),
      node('p', [text('Qcd')])
    ])
    // caret at the boundary between pasted "Q" and the trailing "cd"
    expect(tx.sel_after).toEqual(caret([1, 0], 1))
  })

  it('inserts middle blocks verbatim and keeps the last block tag', () => {
    const s = state([node('p', [text('abcd')])], caret([0, 0], 2))
    const tx = cmdPasteSlice(s, [
      node('p', [text('one')]),
      node('h2', [text('two')]),
      node('p', [text('three')])
    ])!
    expect(tx.doc_after.content).toEqual([
      node('p', [text('abone')]),
      node('h2', [text('two')]),
      node('p', [text('threecd')])
    ])
  })
})

describe('commands/cmdPasteSlice — guards', () => {
  it('returns null without a selection', () => {
    const s = state([node('p', [text('x')])], null as any)
    expect(cmdPasteSlice(s, [text('y')])).toBeNull()
  })
  it('returns null for an empty slice', () => {
    const s = state([node('p', [text('x')])], caret([0, 0], 0))
    expect(cmdPasteSlice(s, [])).toBeNull()
  })
  it('deletes a multi-leaf (cross-block) range, then pastes at the caret', () => {
    const s = state([node('p', [text('ab')]), node('p', [text('cd')])],
      textSelection(pos([0, 0], 1), pos([1, 0], 1)))
    const tx = cmdPasteSlice(s, [text('y')])!
    // range "b…c" deleted (blocks merge to "ad"), then "y" pasted at the caret
    expect(tx.doc_after.content.length).toBe(1)
    expect(tx.doc_after.content[0]).toEqual(node('p', [text('ayd')]))
  })
})
