// Drawing commands. Compile down to existing `mod_step` steps (set_attr,
// replace, replace_around) — no new step kinds per Stage-4 §5.
//
// All commands return Transaction | null; null = "command does not apply".

import { isNode, lastIndex, nodeAt, parentPath } from '../model/doc.js'
import {
  stepReplace,
  stepSetAttr
} from '../model/step.js'
import { txBegin, txStep } from '../model/transaction.js'
import { pathCompare } from '../model/source-pos.js'
import type { EditorState } from '../commands/types.js'
import type {
  Attr,
  AttrValue,
  Node,
  SourcePath,
  Transaction
} from '../model/types.js'
import {
  getShapeAttrNumber,
  getShapeAttrString
} from './shape-utils.js'

// ---------------------------------------------------------------------------
// ID generator — monotonic counter scoped per process
// ---------------------------------------------------------------------------

let _idCounter = 0
export function nextShapeId(prefix = 's'): string {
  _idCounter += 1
  return `${prefix}_${_idCounter.toString(36)}`
}
export function _resetShapeIdCounter(): void { _idCounter = 0 }

// ---------------------------------------------------------------------------
// cmdInsertShape — append a shape to the named layer
// ---------------------------------------------------------------------------

export interface InsertShapeArgs {
  drawing_path: SourcePath
  layer_index?: number   // default 0 (first layer)
  shape: Node            // a constructed <shape> Node (or <connector>, <group>, <text-frame>)
}

export function cmdInsertShape(state: EditorState, args: InsertShapeArgs): Transaction | null {
  const drawing = nodeAt(state.doc, args.drawing_path)
  if (drawing === null || !isNode(drawing) || drawing.tag !== 'drawing') return null
  const layerIdx = args.layer_index ?? 0
  if (layerIdx < 0 || layerIdx >= drawing.content.length) return null
  const layer = drawing.content[layerIdx]
  if (!isNode(layer) || layer.tag !== 'layer') return null
  const layerPath: SourcePath = [...args.drawing_path, layerIdx]
  const insertAt = layer.content.length
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepReplace(layerPath, insertAt, insertAt, [args.shape]))
  return tx
}

// ---------------------------------------------------------------------------
// cmdMoveShapes — delta-translate one or many shapes
// ---------------------------------------------------------------------------

export interface MoveShapesArgs {
  shape_paths: SourcePath[]
  dx: number
  dy: number
}

export function cmdMoveShapes(state: EditorState, args: MoveShapesArgs): Transaction | null {
  if (args.shape_paths.length === 0) return null
  if (args.dx === 0 && args.dy === 0) return null
  let tx = txBegin(state.doc, state.selection)
  for (const p of args.shape_paths) {
    const n = nodeAt(tx.doc_after, p)
    if (n === null || !isNode(n)) continue
    if (args.dx !== 0) {
      const cur = getShapeAttrNumber(n, 'x', 0)
      tx = txStep(tx, stepSetAttr(p, 'x', cur + args.dx))
    }
    if (args.dy !== 0) {
      const after = nodeAt(tx.doc_after, p)
      if (after !== null && isNode(after)) {
        const cur = getShapeAttrNumber(after, 'y', 0)
        tx = txStep(tx, stepSetAttr(p, 'y', cur + args.dy))
      }
    }
  }
  return tx.steps.length === 0 ? null : tx
}

// ---------------------------------------------------------------------------
// cmdResizeShape — set new geometry on one shape
// ---------------------------------------------------------------------------

export interface ResizeShapeArgs {
  shape_path: SourcePath
  x?: number
  y?: number
  width?: number
  height?: number
}

export function cmdResizeShape(state: EditorState, args: ResizeShapeArgs): Transaction | null {
  const n = nodeAt(state.doc, args.shape_path)
  if (n === null || !isNode(n)) return null
  let tx = txBegin(state.doc, state.selection)
  if (args.x      !== undefined && args.x      !== getShapeAttrNumber(n, 'x', 0))      tx = txStep(tx, stepSetAttr(args.shape_path, 'x', args.x))
  if (args.y      !== undefined && args.y      !== getShapeAttrNumber(n, 'y', 0))      tx = txStep(tx, stepSetAttr(args.shape_path, 'y', args.y))
  if (args.width  !== undefined && args.width  !== getShapeAttrNumber(n, 'width', 0))  tx = txStep(tx, stepSetAttr(args.shape_path, 'width', args.width))
  if (args.height !== undefined && args.height !== getShapeAttrNumber(n, 'height', 0)) tx = txStep(tx, stepSetAttr(args.shape_path, 'height', args.height))
  return tx.steps.length === 0 ? null : tx
}

// ---------------------------------------------------------------------------
// cmdRotateShape — set rotate attribute
// ---------------------------------------------------------------------------

export function cmdRotateShape(state: EditorState, shape_path: SourcePath, angleDeg: number): Transaction | null {
  const n = nodeAt(state.doc, shape_path)
  if (n === null || !isNode(n)) return null
  if (getShapeAttrNumber(n, 'rotate', 0) === angleDeg) return null
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepSetAttr(shape_path, 'rotate', angleDeg))
  return tx
}

// ---------------------------------------------------------------------------
// cmdSetShapeAttr — generic attribute setter (style + custom)
// ---------------------------------------------------------------------------

export function cmdSetShapeAttr(state: EditorState, shape_path: SourcePath, name: string, value: AttrValue): Transaction | null {
  const n = nodeAt(state.doc, shape_path)
  if (n === null || !isNode(n)) return null
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepSetAttr(shape_path, name, value))
  return tx
}

// ---------------------------------------------------------------------------
// cmdDeleteShapes — delete one or many shapes
// Multi-delete handles cross-parent and same-parent correctly by sorting in
// descending document order so later deletes don't shift earlier indices.
// ---------------------------------------------------------------------------

export function cmdDeleteShapes(state: EditorState, shape_paths: SourcePath[]): Transaction | null {
  if (shape_paths.length === 0) return null
  // Filter unique + sort descending
  const sorted = uniquePaths(shape_paths).sort((a, b) => pathCompare(b, a))
  let tx = txBegin(state.doc, state.selection)
  for (const p of sorted) {
    const n = nodeAt(tx.doc_after, p)
    if (n === null) continue
    const parent = parentPath(p)
    const idx = lastIndex(p)
    if (idx < 0) continue
    tx = txStep(tx, stepReplace(parent, idx, idx + 1, []))
  }
  return tx.steps.length === 0 ? null : tx
}

function uniquePaths(ps: SourcePath[]): SourcePath[] {
  const seen = new Set<string>()
  const out: SourcePath[] = []
  for (const p of ps) {
    const key = p.join(',')
    if (!seen.has(key)) { seen.add(key); out.push(p) }
  }
  return out
}

// ---------------------------------------------------------------------------
// cmdBringToFront / cmdSendToBack — reorder z within parent
// ---------------------------------------------------------------------------

export function cmdBringToFront(state: EditorState, shape_path: SourcePath): Transaction | null {
  const n = nodeAt(state.doc, shape_path)
  if (n === null || !isNode(n)) return null
  const parent = parentPath(shape_path)
  const parentNode = nodeAt(state.doc, parent)
  if (parentNode === null || !isNode(parentNode)) return null
  const idx = lastIndex(shape_path)
  if (idx < 0 || idx === parentNode.content.length - 1) return null
  let tx = txBegin(state.doc, state.selection)
  // Remove from old position, append to end
  tx = txStep(tx, stepReplace(parent, idx, idx + 1, []))
  tx = txStep(tx, stepReplace(parent, parentNode.content.length - 1, parentNode.content.length - 1, [n]))
  return tx
}

export function cmdSendToBack(state: EditorState, shape_path: SourcePath): Transaction | null {
  const n = nodeAt(state.doc, shape_path)
  if (n === null || !isNode(n)) return null
  const parent = parentPath(shape_path)
  const parentNode = nodeAt(state.doc, parent)
  if (parentNode === null || !isNode(parentNode)) return null
  const idx = lastIndex(shape_path)
  if (idx < 0 || idx === 0) return null
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepReplace(parent, idx, idx + 1, []))
  tx = txStep(tx, stepReplace(parent, 0, 0, [n]))
  return tx
}

// ---------------------------------------------------------------------------
// Shape constructors (convenience for tools and tests)
// ---------------------------------------------------------------------------

export function makeRectShape(geom: { x: number; y: number; width: number; height: number },
                              style: Partial<{ fill: string; stroke: string; 'stroke-width': number }> = {}): Node {
  return {
    kind: 'node',
    tag: 'shape',
    attrs: shapeAttrs({ id: nextShapeId('r'), kind: 'rect', ...geom, ...style })
  , content: []
  }
}

export function makeEllipseShape(geom: { x: number; y: number; width: number; height: number },
                                  style: Partial<{ fill: string; stroke: string }> = {}): Node {
  return {
    kind: 'node',
    tag: 'shape',
    attrs: shapeAttrs({ id: nextShapeId('e'), kind: 'ellipse', ...geom, ...style }),
    content: []
  }
}

export function makeLineShape(geom: { x: number; y: number; width: number; height: number },
                               style: Partial<{ stroke: string; 'stroke-width': number }> = {}): Node {
  return {
    kind: 'node',
    tag: 'shape',
    attrs: shapeAttrs({ id: nextShapeId('l'), kind: 'line', ...geom, ...style }),
    content: []
  }
}

export function makeConnector(args: {
  from_shape?: string
  from_x?: number
  from_y?: number
  to_shape?: string
  to_x?: number
  to_y?: number
  routing?: 'straight' | 'orthogonal' | 'curved'
}): Node {
  const a: Record<string, AttrValue> = { id: nextShapeId('c'), routing: args.routing ?? 'orthogonal' }
  if (args.from_shape !== undefined) a['from-shape'] = args.from_shape
  if (args.to_shape   !== undefined) a['to-shape']   = args.to_shape
  if (args.from_x     !== undefined) a['from-x']     = args.from_x
  if (args.from_y     !== undefined) a['from-y']     = args.from_y
  if (args.to_x       !== undefined) a['to-x']       = args.to_x
  if (args.to_y       !== undefined) a['to-y']       = args.to_y
  return { kind: 'node', tag: 'connector', attrs: shapeAttrs(a), content: [] }
}

function shapeAttrs(obj: Record<string, AttrValue>): Attr[] {
  const out: Attr[] = []
  for (const [k, v] of Object.entries(obj)) out.push({ name: k, value: v })
  return out
}

// Force-cast used by tests so they can read attributes back without a narrow.
export function shapeAttrString(shape: Node, name: string): string {
  return getShapeAttrString(shape, name, '')
}
