import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { cmdSetMark, cmdRemoveMark, cmdToggleMark } from '../../src/commands/text-commands.js'
import type { EditorState } from '../../src/commands/types.js'
import type { Selection, Child } from '../../src/model/types.js'

function state(blocks: any[], sel: Selection): EditorState {
  return { doc: node('doc', blocks), schema: html5SubsetSchema, selection: sel, stored_marks: null }
}
const colored = (t: string, c: string): Child => ({ kind: 'text', text: t, marks: { color: c } })
const content = (tx: { doc_after: any }, i = 0): Child[] => tx.doc_after.content[i].content

// cmdSetMark applies a value-carrying mark unconditionally (set semantics), so a
// colour picker can re-pick to replace; cmdRemoveMark clears it. This is the gap
// cmdToggleMark cannot fill — it would remove the mark once any colour is present.
describe('commands/cmdSetMark + cmdRemoveMark', () => {
  it('sets a colour across a single-leaf selection', () => {
    const s = state([node('p', [text('hello')])], textSelection(pos([0, 0], 0), pos([0, 0], 5)))
    expect(content(cmdSetMark(s, 'color', '#ff0000')!)).toEqual([colored('hello', '#ff0000')])
  })

  it('REPLACES an existing colour (set semantics, not toggle)', () => {
    const s = state([node('p', [colored('hello', '#ff0000')])], textSelection(pos([0, 0], 0), pos([0, 0], 5)))
    expect(content(cmdSetMark(s, 'color', '#0000ff')!)).toEqual([colored('hello', '#0000ff')])
  })

  it('cmdRemoveMark clears the colour', () => {
    const s = state([node('p', [colored('hello', '#ff0000')])], textSelection(pos([0, 0], 0), pos([0, 0], 5)))
    expect(content(cmdRemoveMark(s, 'color')!)).toEqual([text('hello')])
  })

  it('contrast: cmdToggleMark would WRONGLY remove an existing colour (why set is needed)', () => {
    const s = state([node('p', [colored('hello', '#ff0000')])], textSelection(pos([0, 0], 0), pos([0, 0], 5)))
    expect(content(cmdToggleMark(s, 'color', '#0000ff')!)).toEqual([text('hello')])
  })

  it('sets a colour across a cross-block selection (both covered runs)', () => {
    const s = state([node('p', [text('foo')]), node('p', [text('bar')])],
      textSelection(pos([0, 0], 1), pos([1, 0], 2)))
    const tx = cmdSetMark(s, 'color', '#16a34a')!
    expect(tx.doc_after.content.length).toBe(2)
    expect(content(tx, 0)).toEqual([text('f'), colored('oo', '#16a34a')])
    expect(content(tx, 1)).toEqual([colored('ba', '#16a34a'), text('r')])
  })

  it('collapsed caret: set colour leaves the doc unchanged (stored for next typing)', () => {
    const s = state([node('p', [text('hi')])], textSelection(pos([0, 0], 2), pos([0, 0], 2)))
    const tx = cmdSetMark(s, 'color', '#ef4444')
    expect(tx).not.toBeNull()
    expect(tx!.doc_after).toEqual(s.doc)
  })
})
