import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { textAt } from '../helpers/narrow.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { stepReplaceText } from '../../src/model/step.js'
import {
  canRedo,
  canUndo,
  historyApplyStep,
  historyNew,
  historyPush,
  historyRedo,
  historyUndo
} from '../../src/model/history.js'
import { txBegin, txSetMeta, txStep } from '../../src/model/transaction.js'

const initial = node('doc', [node('p', [text('hello')])])

describe('model/history — basics', () => {
  it('empty history has nothing to undo/redo', () => {
    const h = historyNew()
    expect(canUndo(h)).toBe(false)
    expect(canRedo(h)).toBe(false)
    expect(historyUndo(h, initial).ok).toBe(false)
  })

  it('apply one step → undo restores → redo re-applies', () => {
    const r1 = historyApplyStep(
      historyNew(),
      initial,
      stepReplaceText([0, 0], 0, 5, 'world'),
      null,
      null
    )
    expect(textAt(r1.doc, [0, 0])).toBe('world')
    expect(canUndo(r1.hist)).toBe(true)

    const undone = historyUndo(r1.hist, r1.doc)
    expect(undone.ok).toBe(true)
    expect(textAt(undone.doc, [0, 0])).toBe('hello')
    expect(canRedo(undone.hist)).toBe(true)

    const redone = historyRedo(undone.hist, undone.doc)
    expect(redone.ok).toBe(true)
    expect(textAt(redone.doc, [0, 0])).toBe('world')
  })

  it('apply two steps → undo twice restores fully', () => {
    let r = historyApplyStep(historyNew(), initial, stepReplaceText([0, 0], 5, 5, '!'), null, null)
    r = historyApplyStep(r.hist, r.doc, stepReplaceText([0, 0], 0, 0, '>'), null, null)
    expect(textAt(r.doc, [0, 0])).toBe('>hello!')

    const u1 = historyUndo(r.hist, r.doc)
    expect(textAt(u1.doc, [0, 0])).toBe('hello!')
    const u2 = historyUndo(u1.hist, u1.doc)
    expect(textAt(u2.doc, [0, 0])).toBe('hello')
    expect(canUndo(u2.hist)).toBe(false)
  })

  it('selection is restored on undo (sel_before of the original tx)', () => {
    const selBefore = textSelection(pos([0, 0], 5), pos([0, 0], 5))
    const selAfter  = textSelection(pos([0, 0], 6), pos([0, 0], 6))
    const r = historyApplyStep(
      historyNew(),
      initial,
      stepReplaceText([0, 0], 5, 5, '!'),
      selBefore,
      selAfter
    )
    const undone = historyUndo(r.hist, r.doc)
    expect(undone.sel).toEqual(selBefore)
  })

  it('historyGroup compresses adjacent transactions', () => {
    let h = historyNew()
    let doc = initial

    // First "typing" tx
    {
      let tx = txBegin(doc, null)
      tx = txStep(tx, stepReplaceText([0, 0], 5, 5, 'A'))
      tx = txSetMeta(tx, 'historyGroup', 'typing')
      h = historyPush(h, tx)
      doc = tx.doc_after
    }
    // Second "typing" tx — same group, should merge
    {
      let tx = txBegin(doc, null)
      tx = txStep(tx, stepReplaceText([0, 0], 6, 6, 'B'))
      tx = txSetMeta(tx, 'historyGroup', 'typing')
      h = historyPush(h, tx)
      doc = tx.doc_after
    }
    expect(textAt(doc, [0, 0])).toBe('helloAB')
    expect(h.undo).toHaveLength(1)  // merged

    // One undo restores both characters
    const u = historyUndo(h, doc)
    expect(textAt(u.doc, [0, 0])).toBe('hello')
  })

  it('historyPush clears the redo stack', () => {
    let r = historyApplyStep(historyNew(), initial, stepReplaceText([0, 0], 0, 0, 'X'), null, null)
    r = { ...r, ...historyUndo(r.hist, r.doc) }
    expect(canRedo(r.hist)).toBe(true)
    r = historyApplyStep(r.hist, r.doc, stepReplaceText([0, 0], 0, 0, 'Y'), null, null)
    expect(canRedo(r.hist)).toBe(false)
  })
})
