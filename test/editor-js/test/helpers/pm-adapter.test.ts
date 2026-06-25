import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { pmPosToSourcePos, sourcePosToPmPos } from './pm-adapter.js'

const DOC = node('doc', [
  node('p', [text('hi')]),     // p has size 2 (open=1 → text(2) → close=1) = 4 in this scheme? No, see below.
  node('p', [text('world')])
])
// PM positions for this doc:
//   0  : before first <p>
//   1  : inside first <p>, before 'h'
//   2  : after 'h'
//   3  : after 'i'  (= before </p>)
//   4  : after first </p>, before second <p>
//   5  : inside second <p>, before 'w'
//   6  : after 'w'
//   ...
//  10  : after 'd' (end of second <p>'s text)
//  11  : after second </p> (end of doc)

describe('pm-adapter — pmPosToSourcePos', () => {
  it('pos 0 → root boundary', () => {
    expect(pmPosToSourcePos(DOC, 0)).toEqual({ path: [], offset: 0 })
  })

  it('inside first text leaf', () => {
    expect(pmPosToSourcePos(DOC, 1)).toEqual({ path: [0, 0], offset: 0 })
    expect(pmPosToSourcePos(DOC, 2)).toEqual({ path: [0, 0], offset: 1 })
    expect(pmPosToSourcePos(DOC, 3)).toEqual({ path: [0, 0], offset: 2 })
  })

  it('boundary between block siblings', () => {
    expect(pmPosToSourcePos(DOC, 4)).toEqual({ path: [], offset: 1 })
  })

  it('inside second text leaf', () => {
    expect(pmPosToSourcePos(DOC, 5)).toEqual({ path: [1, 0], offset: 0 })
    expect(pmPosToSourcePos(DOC, 7)).toEqual({ path: [1, 0], offset: 2 })
  })

  it('end-of-doc boundary', () => {
    expect(pmPosToSourcePos(DOC, 11)).toEqual({ path: [], offset: 2 })
  })
})

describe('pm-adapter — round-trip', () => {
  it('every pos round-trips through src→pm→src', () => {
    for (let p = 0; p <= 11; p++) {
      const src = pmPosToSourcePos(DOC, p)
      if (src === null) continue  // boundary cases mid-token may be ambiguous
      const back = sourcePosToPmPos(DOC, src)
      expect(back).toBe(p)
    }
  })
})
