// Event driver — translates a fixture's events.json entries into editor
// transactions. Each entry has a `type` discriminator:
//
//   { type: 'intent',  intent: InputIntent }
//   { type: 'command', name: string, args?: any }
//   { type: 'step',    step: Step }
//   { type: 'set_selection', selection: Selection | null }
//
// Returns Transaction | null. The fixture-runner applies the tx to state.

import { dispatchIntent, type InputIntent } from '../../src/input/intent.js'
import {
  cmdDeleteBackward,
  cmdDeleteForward,
  cmdFormatBold,
  cmdFormatItalic,
  cmdDeleteNode,
  cmdFormatUnderline,
  cmdInsertLineBreak,
  cmdInsertParagraph,
  cmdInsertText,
  cmdSelectAll,
  cmdSetBlockType,
  cmdToggleMark
} from '../../src/commands/text-commands.js'
import { cmdMoveCaret } from '../../src/commands/caret.js'
import type { Direction, Granularity } from '../../src/commands/caret.js'
import {
  cmdAddTableColumn,
  cmdAddTableRow,
  cmdDeleteTableColumn,
  cmdDeleteTableRow,
  cmdIndentListItem,
  cmdInsertImage,
  cmdInsertTable,
  cmdOutdentListItem,
  cmdWrapInList
} from '../../src/commands/structural-commands.js'
import {
  cmdBringToFront,
  cmdDeleteShapes,
  cmdInsertShape,
  cmdMoveShapes,
  cmdResizeShape,
  cmdRotateShape,
  cmdSendToBack,
  cmdSetShapeAttr,
  makeConnector,
  makeEllipseShape,
  makeLineShape,
  makeRectShape
} from '../../src/drawing/commands.js'
import { stepApply } from '../../src/model/step.js'
import { txBegin, txSetSelection, txStep } from '../../src/model/transaction.js'
import type { EditorState } from '../../src/commands/types.js'
import type { AttrValue, MarkDict, Selection, SourcePath, Step, Transaction } from '../../src/model/types.js'

// ---------------------------------------------------------------------------
// Event union
// ---------------------------------------------------------------------------

export type FixtureEvent =
  | { type: 'intent';        intent: InputIntent }
  | { type: 'command';       name: string;        args?: Record<string, unknown> }
  | { type: 'step';          step: Step }
  | { type: 'set_selection'; selection: Selection | null }
  | { type: 'set_stored_marks'; marks: MarkDict | null }

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

export function driveEvents(state: EditorState, ev: FixtureEvent): Transaction | null {
  switch (ev.type) {
    case 'intent':
      return dispatchIntent(state, ev.intent)
    case 'command':
      return runCommand(state, ev.name, ev.args ?? {})
    case 'step':
      return applyStepAsTx(state, ev.step)
    case 'set_selection':
      return setSelectionTx(state, ev.selection)
    case 'set_stored_marks':
      // Not a doc-mutating event — handled by the runner directly.
      // We synthesize a no-op tx that carries storedMarks meta.
      return setStoredMarksTx(state, ev.marks)
  }
}

// ---------------------------------------------------------------------------
// Command registry
// ---------------------------------------------------------------------------

type CommandHandler = (state: EditorState, args: Record<string, unknown>) => Transaction | null

const COMMANDS: Record<string, CommandHandler> = {
  // text commands
  insertText:            (s, a) => cmdInsertText(s, asStr(a, 'text')),
  insertParagraph:       (s)    => cmdInsertParagraph(s),
  insertLineBreak:       (s)    => cmdInsertLineBreak(s),
  deleteContentBackward: (s)    => cmdDeleteBackward(s),
  deleteContentForward:  (s)    => cmdDeleteForward(s),
  formatBold:            (s)    => cmdFormatBold(s),
  formatItalic:          (s)    => cmdFormatItalic(s),
  formatUnderline:       (s)    => cmdFormatUnderline(s),
  toggleMark:            (s, a) => {
    const value: AttrValue = 'value' in a ? (a['value'] as AttrValue) : true
    return cmdToggleMark(s, asStr(a, 'mark'), value)
  },
  setBlockType:          (s, a) => cmdSetBlockType(s, asStr(a, 'tag')),
  selectAll:             (s)    => cmdSelectAll(s),
  deleteNode:            (s)    => cmdDeleteNode(s),
  // caret navigation (selection.modify equivalent)
  moveCaret:             (s, a) => cmdMoveCaret(
                                     s,
                                     a['alter'] === 'extend' ? 'extend' : 'move',
                                     (a['direction'] as Direction) ?? 'forward',
                                     (a['granularity'] as Granularity) ?? 'character'),

  // structural commands — lists / tables / images
  wrapInList:        (s, a) => cmdWrapInList(s, (a['kind'] === 'ol' ? 'ol' : 'ul')),
  indentListItem:    (s)    => cmdIndentListItem(s),
  outdentListItem:   (s)    => cmdOutdentListItem(s),
  insertImage:       (s, a) => cmdInsertImage(s, asStr(a, 'src'), typeof a['alt'] === 'string' ? a['alt'] : ''),
  insertTable:       (s, a) => cmdInsertTable(s, asNumOpt(a, 'rows') ?? 2, asNumOpt(a, 'cols') ?? 2, a['header'] !== false),
  addTableRow:       (s)    => cmdAddTableRow(s),
  deleteTableRow:    (s)    => cmdDeleteTableRow(s),
  addTableColumn:    (s)    => cmdAddTableColumn(s),
  deleteTableColumn: (s)    => cmdDeleteTableColumn(s),

  // drawing commands
  insertShape: (s, a) => {
    const drawing_path = asPath(a, 'drawing_path')
    const layer_index  = asNumOpt(a, 'layer_index') ?? 0
    const shape = buildShapeFromArgs(a)
    if (shape === null) return null
    return cmdInsertShape(s, { drawing_path, layer_index, shape })
  },
  moveShapes: (s, a) => cmdMoveShapes(s, {
    shape_paths: asPathArray(a, 'shape_paths'),
    dx: asNum(a, 'dx', 0),
    dy: asNum(a, 'dy', 0)
  }),
  resizeShape: (s, a) => {
    const opts: Parameters<typeof cmdResizeShape>[1] = { shape_path: asPath(a, 'shape_path') }
    const x = asNumOpt(a, 'x'); if (x !== undefined) opts.x = x
    const y = asNumOpt(a, 'y'); if (y !== undefined) opts.y = y
    const w = asNumOpt(a, 'width');  if (w !== undefined) opts.width  = w
    const h = asNumOpt(a, 'height'); if (h !== undefined) opts.height = h
    return cmdResizeShape(s, opts)
  },
  rotateShape:   (s, a) => cmdRotateShape(s, asPath(a, 'shape_path'), asNum(a, 'angle', 0)),
  setShapeAttr:  (s, a) => cmdSetShapeAttr(s, asPath(a, 'shape_path'), asStr(a, 'name'), a['value'] as never),
  deleteShapes:  (s, a) => cmdDeleteShapes(s, asPathArray(a, 'shape_paths')),
  bringToFront:  (s, a) => cmdBringToFront(s, asPath(a, 'shape_path')),
  sendToBack:    (s, a) => cmdSendToBack(s, asPath(a, 'shape_path'))
}

function runCommand(state: EditorState, name: string, args: Record<string, unknown>): Transaction | null {
  const fn = COMMANDS[name]
  if (fn === undefined) throw new Error(`unknown command in fixture: ${name}`)
  return fn(state, args)
}

// ---------------------------------------------------------------------------
// Helpers — arg coercion
// ---------------------------------------------------------------------------

function asStr(a: Record<string, unknown>, key: string): string {
  const v = a[key]
  if (typeof v !== 'string') throw new Error(`expected string for arg "${key}", got ${typeof v}`)
  return v
}
function asNum(a: Record<string, unknown>, key: string, def = 0): number {
  const v = a[key]
  return typeof v === 'number' ? v : def
}
function asNumOpt(a: Record<string, unknown>, key: string): number | undefined {
  const v = a[key]
  return typeof v === 'number' ? v : undefined
}
function asPath(a: Record<string, unknown>, key: string): SourcePath {
  const v = a[key]
  if (!Array.isArray(v)) throw new Error(`expected number[] for arg "${key}"`)
  return v as number[]
}
function asPathArray(a: Record<string, unknown>, key: string): SourcePath[] {
  const v = a[key]
  if (!Array.isArray(v)) throw new Error(`expected number[][] for arg "${key}"`)
  return v as number[][]
}

// ---------------------------------------------------------------------------
// Shape construction from JSON args
// ---------------------------------------------------------------------------

function buildShapeFromArgs(a: Record<string, unknown>) {
  const kind = a['kind']
  if (kind === 'rect')    return makeRectShape({
    x: asNum(a, 'x'), y: asNum(a, 'y'), width: asNum(a, 'width'), height: asNum(a, 'height')
  })
  if (kind === 'ellipse') return makeEllipseShape({
    x: asNum(a, 'x'), y: asNum(a, 'y'), width: asNum(a, 'width'), height: asNum(a, 'height')
  })
  if (kind === 'line')    return makeLineShape({
    x: asNum(a, 'x'), y: asNum(a, 'y'), width: asNum(a, 'width'), height: asNum(a, 'height')
  })
  if (kind === 'connector') {
    const o: Parameters<typeof makeConnector>[0] = {}
    const fx = asNumOpt(a, 'from_x'); if (fx !== undefined) o.from_x = fx
    const fy = asNumOpt(a, 'from_y'); if (fy !== undefined) o.from_y = fy
    const tx = asNumOpt(a, 'to_x');   if (tx !== undefined) o.to_x   = tx
    const ty = asNumOpt(a, 'to_y');   if (ty !== undefined) o.to_y   = ty
    if (typeof a['from_shape'] === 'string') o.from_shape = a['from_shape']
    if (typeof a['to_shape']   === 'string') o.to_shape   = a['to_shape']
    const r = a['routing']
    if (r === 'straight' || r === 'orthogonal' || r === 'curved') o.routing = r
    return makeConnector(o)
  }
  return null
}

// ---------------------------------------------------------------------------
// Direct-step / meta event handlers
// ---------------------------------------------------------------------------

function applyStepAsTx(state: EditorState, step: Step): Transaction | null {
  try {
    const newDoc = stepApply(step, state.doc)
    let tx = txBegin(state.doc, state.selection)
    tx = txStep(tx, step)
    void newDoc
    return tx
  } catch (e) {
    void e
    return null
  }
}

function setSelectionTx(state: EditorState, sel: Selection | null): Transaction | null {
  let tx = txBegin(state.doc, state.selection)
  tx = txSetSelection(tx, sel)
  return tx
}

function setStoredMarksTx(state: EditorState, marks: MarkDict | null): Transaction | null {
  let tx = txBegin(state.doc, state.selection)
  tx = txSetSelection(tx, state.selection)
  tx.meta.push({ name: 'storedMarks', value: marks as never })
  tx.meta.push({ name: 'addToHistory', value: false })
  return tx
}
