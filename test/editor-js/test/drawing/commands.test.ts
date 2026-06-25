import { describe, it, expect, beforeEach } from 'vitest'
import { docSchema } from '../../src/schemas/doc.js'
import { parseHtmlToDoc } from '../../src/view/html-parser.js'
import {
  _resetShapeIdCounter,
  cmdBringToFront,
  cmdDeleteShapes,
  cmdInsertShape,
  cmdMoveShapes,
  cmdResizeShape,
  cmdRotateShape,
  cmdSendToBack,
  cmdSetShapeAttr,
  makeRectShape
} from '../../src/drawing/commands.js'
import { stepApply } from '../../src/model/step.js'
import { txInvert } from '../../src/model/transaction.js'
import { tagAt } from '../helpers/narrow.js'
import { getShapeAttrNumber, getShapeAttrString, getShapeBbox } from '../../src/drawing/shape-utils.js'
import { isNode, nodeAt } from '../../src/model/doc.js'
import type { EditorState } from '../../src/commands/types.js'

beforeEach(() => _resetShapeIdCounter())

function buildState(html: string): EditorState {
  const { doc } = parseHtmlToDoc(html, docSchema)
  return { doc, schema: docSchema, selection: null, stored_marks: null }
}

const TWO_SHAPES = `
  <doc>
    <drawing id="D1" width="400" height="300">
      <layer id="L1">
        <shape id="S1" kind="rect" x="50" y="50" width="100" height="80"></shape>
        <shape id="S2" kind="rect" x="200" y="100" width="120" height="80"></shape>
      </layer>
    </drawing>
  </doc>`

describe('drawing/commands — insert', () => {
  it('cmdInsertShape appends to the layer', () => {
    const s = buildState(TWO_SHAPES)
    const shape = makeRectShape({ x: 300, y: 200, width: 40, height: 40 })
    const tx = cmdInsertShape(s, { drawing_path: [0], layer_index: 0, shape })!
    const layer = nodeAt(tx.doc_after, [0, 0])!
    expect(isNode(layer)).toBe(true)
    if (!isNode(layer)) throw new Error('layer not a node')
    expect(layer.content.length).toBe(3)
    expect(getShapeAttrNumber(layer.content[2] as any, 'x', 0)).toBe(300)
  })

  it('returns null when target is not a drawing', () => {
    const s = buildState('<doc><p>x</p></doc>')
    const shape = makeRectShape({ x: 0, y: 0, width: 10, height: 10 })
    expect(cmdInsertShape(s, { drawing_path: [0], layer_index: 0, shape })).toBeNull()
  })
})

describe('drawing/commands — move', () => {
  it('cmdMoveShapes translates by delta', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdMoveShapes(s, { shape_paths: [[0, 0, 0]], dx: 30, dy: 20 })!
    const s1 = nodeAt(tx.doc_after, [0, 0, 0])!
    expect(getShapeAttrNumber(s1 as any, 'x', 0)).toBe(80)
    expect(getShapeAttrNumber(s1 as any, 'y', 0)).toBe(70)
  })

  it('no-op when delta is zero', () => {
    const s = buildState(TWO_SHAPES)
    expect(cmdMoveShapes(s, { shape_paths: [[0, 0, 0]], dx: 0, dy: 0 })).toBeNull()
  })

  it('moves multiple shapes in one transaction', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdMoveShapes(s, { shape_paths: [[0, 0, 0], [0, 0, 1]], dx: 10, dy: 0 })!
    const after0 = nodeAt(tx.doc_after, [0, 0, 0])!
    const after1 = nodeAt(tx.doc_after, [0, 0, 1])!
    expect(getShapeAttrNumber(after0 as any, 'x', 0)).toBe(60)
    expect(getShapeAttrNumber(after1 as any, 'x', 0)).toBe(210)
  })

  it('undo restores original positions', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdMoveShapes(s, { shape_paths: [[0, 0, 0]], dx: 30, dy: 20 })!
    const invTx = txInvert(tx)
    let cur = tx.doc_after
    for (const step of invTx.steps) cur = stepApply(step, cur)
    expect(getShapeAttrNumber(nodeAt(cur, [0, 0, 0]) as any, 'x', 0)).toBe(50)
  })
})

describe('drawing/commands — resize / rotate / setAttr', () => {
  it('cmdResizeShape changes geometry', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdResizeShape(s, { shape_path: [0, 0, 0], x: 60, y: 60, width: 120, height: 100 })!
    const bbox = getShapeBbox(nodeAt(tx.doc_after, [0, 0, 0]) as any)!
    expect(bbox).toEqual({ x: 60, y: 60, width: 120, height: 100 })
  })

  it('cmdRotateShape sets the rotate attr', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdRotateShape(s, [0, 0, 0], 45)!
    expect(getShapeAttrNumber(nodeAt(tx.doc_after, [0, 0, 0]) as any, 'rotate', 0)).toBe(45)
  })

  it('cmdSetShapeAttr updates any attr (e.g., fill)', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdSetShapeAttr(s, [0, 0, 0], 'fill', '#ff0')!
    expect(getShapeAttrString(nodeAt(tx.doc_after, [0, 0, 0]) as any, 'fill', '')).toBe('#ff0')
  })
})

describe('drawing/commands — delete', () => {
  it('deletes one shape', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdDeleteShapes(s, [[0, 0, 0]])!
    const layer = nodeAt(tx.doc_after, [0, 0])!
    if (!isNode(layer)) throw new Error('not a node')
    expect(layer.content.length).toBe(1)
    expect(getShapeAttrString(layer.content[0] as any, 'id', '')).toBe('S2')
  })

  it('deletes multiple shapes from same parent (sorted descending — no index shift bug)', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdDeleteShapes(s, [[0, 0, 0], [0, 0, 1]])!
    const layer = nodeAt(tx.doc_after, [0, 0])!
    if (!isNode(layer)) throw new Error('not a node')
    expect(layer.content.length).toBe(0)
  })
})

describe('drawing/commands — z-order', () => {
  it('bring to front moves to last position', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdBringToFront(s, [0, 0, 0])!
    const layer = nodeAt(tx.doc_after, [0, 0])!
    if (!isNode(layer)) throw new Error('not a node')
    expect(getShapeAttrString(layer.content[0] as any, 'id', '')).toBe('S2')
    expect(getShapeAttrString(layer.content[1] as any, 'id', '')).toBe('S1')
  })

  it('send to back moves to first position', () => {
    const s = buildState(TWO_SHAPES)
    const tx = cmdSendToBack(s, [0, 0, 1])!
    const layer = nodeAt(tx.doc_after, [0, 0])!
    if (!isNode(layer)) throw new Error('not a node')
    expect(getShapeAttrString(layer.content[0] as any, 'id', '')).toBe('S2')
    expect(getShapeAttrString(layer.content[1] as any, 'id', '')).toBe('S1')
  })

  it('no-op when already at front', () => {
    const s = buildState(TWO_SHAPES)
    expect(cmdBringToFront(s, [0, 0, 1])).toBeNull()
  })

  it('tagAt sanity check', () => {
    const s = buildState(TWO_SHAPES)
    expect(tagAt(s.doc, [0])).toBe('drawing')
    expect(tagAt(s.doc, [0, 0])).toBe('layer')
    expect(tagAt(s.doc, [0, 0, 0])).toBe('shape')
  })
})
