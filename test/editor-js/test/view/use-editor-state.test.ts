import { describe, it, expect } from 'vitest'
import { editorReducer, initialEditorState } from '../../src/view/use-editor-state.js'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { cmdInsertText } from '../../src/commands/text-commands.js'
import { textAt } from '../helpers/narrow.js'

const start = () =>
  initialEditorState({
    doc: node('doc', [node('p', [text('hello')])]),
    schema: html5SubsetSchema,
    selection: textSelection(pos([0, 0], 5), pos([0, 0], 5))
  })

describe('view/use-editor-state — reducer', () => {
  it('apply commits the doc + selection + history', () => {
    const s0 = start()
    const tx = cmdInsertText(
      { doc: s0.doc, schema: s0.schema, selection: s0.selection, stored_marks: null },
      '!'
    )!
    const s1 = editorReducer(s0, { type: 'apply', tx })
    expect(textAt(s1.doc, [0, 0])).toBe('hello!')
    expect(s1.history.undo.length).toBe(1)
  })

  it('undo restores prior doc + selection', () => {
    const s0 = start()
    const tx = cmdInsertText(
      { doc: s0.doc, schema: s0.schema, selection: s0.selection, stored_marks: null },
      '!'
    )!
    const s1 = editorReducer(s0, { type: 'apply', tx })
    const s2 = editorReducer(s1, { type: 'undo' })
    expect(textAt(s2.doc, [0, 0])).toBe('hello')
    expect(s2.history.redo.length).toBe(1)
  })

  it('redo re-applies', () => {
    const s0 = start()
    const tx = cmdInsertText(
      { doc: s0.doc, schema: s0.schema, selection: s0.selection, stored_marks: null },
      '!'
    )!
    let s = editorReducer(s0, { type: 'apply', tx })
    s = editorReducer(s, { type: 'undo' })
    s = editorReducer(s, { type: 'redo' })
    expect(textAt(s.doc, [0, 0])).toBe('hello!')
  })

  it('set_selection moves the caret without changing the doc', () => {
    const s0 = start()
    const newSel = textSelection(pos([0, 0], 0), pos([0, 0], 0))
    const s1 = editorReducer(s0, { type: 'set_selection', sel: newSel })
    expect(s1.selection).toEqual(newSel)
    expect(s1.doc).toBe(s0.doc)
  })

  it('storedMarks meta flows from transaction to state', () => {
    const s0 = start()
    // tx with storedMarks meta + addToHistory=false (the cmdToggleStoredMark shape)
    const tx = {
      doc_before: s0.doc,
      doc_after: s0.doc,
      steps: [],
      sel_before: s0.selection,
      sel_after: s0.selection,
      meta: [
        { name: 'storedMarks', value: ['strong'] as any },
        { name: 'addToHistory', value: false as any }
      ]
    }
    const s1 = editorReducer(s0, { type: 'apply', tx })
    expect(s1.stored_marks).toEqual(['strong'])
    expect(s1.history.undo.length).toBe(0)  // not pushed
  })
})
