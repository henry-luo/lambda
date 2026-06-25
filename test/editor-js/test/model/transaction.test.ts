import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { textAt } from '../helpers/narrow.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { stepApply, stepReplace, stepReplaceText } from '../../src/model/step.js'
import {
  selMap,
  txBegin,
  txGetMeta,
  txInvert,
  txMapPos,
  txSetMeta,
  txSetSelection,
  txStep
} from '../../src/model/transaction.js'

const doc = node('doc', [node('p', [text('hello')])])

describe('model/transaction — begin / step / set selection', () => {
  it('begin captures doc and selection', () => {
    const sel = textSelection(pos([0, 0], 0), pos([0, 0], 0))
    const tx = txBegin(doc, sel)
    expect(tx.doc_before).toBe(doc)
    expect(tx.doc_after).toBe(doc)
    expect(tx.steps).toEqual([])
    expect(tx.sel_before).toEqual(sel)
    expect(tx.sel_after).toEqual(sel)
  })

  it('step accumulates doc + maps selection', () => {
    const sel = textSelection(pos([0, 0], 5), pos([0, 0], 5))
    let tx = txBegin(doc, sel)
    tx = txStep(tx, stepReplaceText([0, 0], 0, 0, 'XX'))  // prepend "XX"
    expect(textAt(tx.doc_after, [0, 0])).toBe('XXhello')
    expect(tx.sel_after).toEqual(textSelection(pos([0, 0], 7), pos([0, 0], 7)))
    expect(tx.steps).toHaveLength(1)
  })

  it('txSetSelection overrides sel_after', () => {
    const tx0 = txBegin(doc, null)
    const tx1 = txSetSelection(tx0, textSelection(pos([0, 0], 2), pos([0, 0], 4)))
    expect(tx1.sel_after).toEqual(textSelection(pos([0, 0], 2), pos([0, 0], 4)))
  })

  it('meta set/get', () => {
    let tx = txBegin(doc, null)
    tx = txSetMeta(tx, 'historyGroup', 'typing')
    tx = txSetMeta(tx, 'scrollIntoView', true)
    expect(txGetMeta(tx, 'historyGroup')).toBe('typing')
    expect(txGetMeta(tx, 'scrollIntoView')).toBe(true)
    expect(txGetMeta(tx, 'missing')).toBeNull()
  })

  it('meta set replaces existing key', () => {
    let tx = txBegin(doc, null)
    tx = txSetMeta(tx, 'k', 'v1')
    tx = txSetMeta(tx, 'k', 'v2')
    expect(txGetMeta(tx, 'k')).toBe('v2')
    expect(tx.meta).toHaveLength(1)
  })
})

describe('model/transaction — invert', () => {
  it('invert produces a transaction that undoes the original', () => {
    let tx = txBegin(doc, null)
    tx = txStep(tx, stepReplaceText([0, 0], 0, 5, 'goodbye'))
    expect(textAt(tx.doc_after, [0, 0])).toBe('goodbye')

    const inv = txInvert(tx)
    expect(inv.doc_before).toBe(tx.doc_after)
    expect(inv.doc_after).toBe(tx.doc_before)
    expect(inv.steps).toHaveLength(1)

    let cur = tx.doc_after
    for (const s of inv.steps) cur = stepApply(s, cur)
    expect(textAt(cur, [0, 0])).toBe('hello')
  })

  it('invert of a multi-step transaction reverses cleanly', () => {
    let tx = txBegin(doc, null)
    tx = txStep(tx, stepReplaceText([0, 0], 0, 5, 'A'))    // "hello" → "A"
    tx = txStep(tx, stepReplaceText([0, 0], 1, 1, 'BC'))   // "A" → "ABC"
    expect(textAt(tx.doc_after, [0, 0])).toBe('ABC')

    const inv = txInvert(tx)
    expect(inv.steps).toHaveLength(2)

    let cur = tx.doc_after
    for (const s of inv.steps) cur = stepApply(s, cur)
    expect(textAt(cur, [0, 0])).toBe('hello')
  })
})

describe('model/transaction — selMap + txMapPos', () => {
  it('selMap on AllSelection is identity', () => {
    const step = stepReplaceText([0, 0], 0, 0, 'X')
    expect(selMap(step, { kind: 'all' })).toEqual({ kind: 'all' })
  })

  it('txMapPos chains stepMaps', () => {
    let tx = txBegin(doc, null)
    tx = txStep(tx, stepReplaceText([0, 0], 0, 0, 'A'))   // shift offsets +1
    tx = txStep(tx, stepReplaceText([0, 0], 0, 0, 'B'))   // shift +1 again
    expect(txMapPos(tx, pos([0, 0], 0))).toEqual(pos([0, 0], 2))
  })

  it('selMap multi-node drops deleted paths', () => {
    const doc2 = node('doc', [
      node('p', [text('a')]),
      node('p', [text('b')]),
      node('p', [text('c')])
    ])
    // delete the middle paragraph
    const step = stepReplace([], 1, 2, [])
    const sel = { kind: 'multi-node' as const, paths: [[0], [1], [2]] }
    const mapped = selMap(step, sel)
    expect(mapped.kind).toBe('multi-node')
    expect((mapped as any).paths).toEqual([[0], [1]])
    void doc2
  })
})
