import { describe, it, expect } from 'vitest'
import { node, nodeAttrs, text } from '../../src/model/doc.js'
import { attrsAt, marksAt, tagAt, textAt } from '../helpers/narrow.js'
import { pos } from '../../src/model/source-pos.js'
import {
  hasMark,
  stepAddMark,
  stepApply,
  stepInvert,
  stepMap,
  stepRemoveMark,
  stepReplace,
  stepReplaceAround,
  stepReplaceText,
  stepSetAttr,
  stepSetNodeType,
  withMark,
  withoutMark
} from '../../src/model/step.js'

describe('model/step — mark helpers', () => {
  it('hasMark / withMark / withoutMark', () => {
    expect(hasMark(['strong'], 'strong')).toBe(true)
    expect(hasMark(['em'], 'strong')).toBe(false)
    expect(withMark(['em'], 'strong')).toEqual(['em', 'strong'])
    expect(withMark(['em', 'strong'], 'strong')).toEqual(['em', 'strong'])  // idempotent
    expect(withoutMark(['em', 'strong'], 'strong')).toEqual(['em'])
    expect(withoutMark(['em'], 'strong')).toEqual(['em'])
  })
})

// One round-trip per step kind: apply ∘ invert ∘ apply == apply.
// (Lambda's mod_step uses the same invariant.)

describe('model/step — replace_text', () => {
  const doc = node('doc', [node('p', [text('hello world')])])

  it('apply replaces a slice', () => {
    const s = stepReplaceText([0, 0], 6, 11, 'lambda')
    const r = stepApply(s, doc)
    expect(textAt(r, [0, 0])).toBe('hello lambda')
  })

  it('invert round-trips', () => {
    const s = stepReplaceText([0, 0], 6, 11, 'lambda')
    const inv = stepInvert(s, doc)
    const r1 = stepApply(s, doc)
    const r2 = stepApply(inv, r1)
    expect(textAt(r2, [0, 0])).toBe('hello world')
  })

  it('map a position past the change', () => {
    const s = stepReplaceText([0, 0], 6, 11, 'lambda')  // +1 char overall
    const p = pos([0, 0], 11)
    expect(stepMap(s, p)).toEqual(pos([0, 0], 12))
  })
})

describe('model/step — replace (children splice)', () => {
  const doc = node('doc', [
    node('p', [text('a')]),
    node('p', [text('b')]),
    node('p', [text('c')])
  ])

  it('apply inserts and deletes', () => {
    // delete the middle paragraph, insert two new ones
    const s = stepReplace([], 1, 2, [node('p', [text('X')]), node('p', [text('Y')])])
    const r = stepApply(s, doc)
    expect(r.content.length).toBe(4)
    expect(textAt(r, [1, 0])).toBe('X')
    expect(textAt(r, [2, 0])).toBe('Y')
    expect(textAt(r, [3, 0])).toBe('c')
  })

  it('invert round-trips', () => {
    const s = stepReplace([], 1, 2, [node('p', [text('X')])])
    const inv = stepInvert(s, doc)
    const r2 = stepApply(inv, stepApply(s, doc))
    expect(r2.content.length).toBe(3)
    expect(textAt(r2, [1, 0])).toBe('b')
  })

  it('map shifts a sibling index', () => {
    const s = stepReplace([], 1, 1, [node('p', [text('inserted')])])
    expect(stepMap(s, pos([], 2))).toEqual(pos([], 3))
    expect(stepMap(s, pos([2, 0], 0))).toEqual(pos([3, 0], 0))
  })
})

describe('model/step — replace_around (sibling-level preserve)', () => {
  // replace_around replaces an outer range parent.content[from..to] while
  // preserving the inner subrange parent.content[gap_from..gap_to]. The gap
  // is re-inserted into `slice` at flat position `insert`. This is FLAT
  // sibling manipulation — wrapping inside a new parent uses `replace` instead.
  const doc = node('doc', [
    node('p', [text('A')]),
    node('p', [text('B')]),
    node('p', [text('C')]),
    node('p', [text('D')])
  ])

  it('preserves an inner subrange while swapping the surrounding siblings', () => {
    // Replace [A,B,C,D] with [X,B,C,Y] — preserve B and C in place
    const X = node('p', [text('X')])
    const Y = node('p', [text('Y')])
    const s = stepReplaceAround([], 0, 4, 1, 3, [X, Y], 1)
    const r = stepApply(s, doc)
    expect(r.content.length).toBe(4)
    expect(textAt(r, [0, 0])).toBe('X')
    expect(textAt(r, [1, 0])).toBe('B')
    expect(textAt(r, [2, 0])).toBe('C')
    expect(textAt(r, [3, 0])).toBe('Y')
  })

  it('invert round-trips', () => {
    const s = stepReplaceAround(
      [], 0, 4, 1, 3,
      [node('p', [text('X')]), node('p', [text('Y')])],
      1
    )
    const inv = stepInvert(s, doc)
    const r2 = stepApply(inv, stepApply(s, doc))
    expect(r2.content.length).toBe(4)
    expect(textAt(r2, [0, 0])).toBe('A')
    expect(textAt(r2, [1, 0])).toBe('B')
    expect(textAt(r2, [2, 0])).toBe('C')
    expect(textAt(r2, [3, 0])).toBe('D')
  })
})

describe('model/step — add_mark / remove_mark', () => {
  const doc = node('doc', [node('p', [text('hi')])])

  it('add then remove', () => {
    const add = stepAddMark([0, 0], 'strong')
    const r1 = stepApply(add, doc)
    expect(marksAt(r1, [0, 0])).toEqual(['strong'])

    const rem = stepRemoveMark([0, 0], 'strong')
    const r2 = stepApply(rem, r1)
    expect(marksAt(r2, [0, 0])).toEqual([])
  })

  it('invert(add_mark) is remove_mark', () => {
    const add = stepAddMark([0, 0], 'strong')
    expect(stepInvert(add, doc).kind).toBe('remove_mark')
  })
})

describe('model/step — set_attr', () => {
  const doc = node('doc', [nodeAttrs('heading', [{ name: 'level', value: 1 }], [text('Title')])])

  it('apply updates the attr', () => {
    const s = stepSetAttr([0], 'level', 3)
    const r = stepApply(s, doc)
    expect(attrsAt(r, [0])).toEqual([{ name: 'level', value: 3 }])
  })

  it('invert restores the previous value', () => {
    const s = stepSetAttr([0], 'level', 3)
    const inv = stepInvert(s, doc)
    expect(inv).toEqual(stepSetAttr([0], 'level', 1))
  })

  it('set_attr value=null removes the attr; invert re-adds it', () => {
    const s = stepSetAttr([0], 'level', null as any)  // delete
    const inv = stepInvert(s, doc)
    expect(inv).toEqual(stepSetAttr([0], 'level', 1))
    const r = stepApply(s, doc)
    expect(attrsAt(r, [0])).toEqual([])
  })
})

describe('model/step — set_node_type', () => {
  const doc = node('doc', [node('p', [text('hi')])])

  it('apply retags', () => {
    const r = stepApply(stepSetNodeType([0], 'heading'), doc)
    expect(tagAt(r, [0])).toBe('heading')
  })

  it('invert restores the original tag', () => {
    const s = stepSetNodeType([0], 'heading')
    const inv = stepInvert(s, doc)
    expect(inv).toEqual(stepSetNodeType([0], 'p'))
  })
})
