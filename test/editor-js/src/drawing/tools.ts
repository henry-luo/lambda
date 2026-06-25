// Tool state machine for canvas-mode editing.
//
// A Tool is a pure-function trait: pointer/keyboard handlers take the current
// editor state + tool session state and return a Transaction (or null) plus
// the next session state. The React component layer (DrawingView) owns the
// session-state lifecycle and dispatches transactions.
//
// Built-ins: select, rect, ellipse, line. More tools (polyline, freehand,
// connector, eraser, text) ship later as fixtures demand them.

import type { EditorState } from '../commands/types.js'
import type { Node, SourcePath, Transaction } from '../model/types.js'
import {
  cmdInsertShape,
  cmdMoveShapes,
  makeEllipseShape,
  makeLineShape,
  makeRectShape
} from './commands.js'
import { rectFromPoints, v2, type Vec2, type Rect } from './geom.js'
import { hitTestDrawing } from './hit-test.js'
import { nodeAt } from '../model/doc.js'
import { isNode } from '../model/doc.js'

// ---------------------------------------------------------------------------
// Tool interface
// ---------------------------------------------------------------------------

export interface ToolPointerEvent {
  /** Drawing-local coordinates. */
  canvas: Vec2
  buttons: number
  mods: { shift?: boolean; alt?: boolean; ctrl?: boolean; meta?: boolean }
}

export interface ToolContext {
  state: EditorState
  drawing_path: SourcePath
  drawing: Node
}

export interface ToolResult {
  tx?: Transaction | null
  session?: ToolSession
}

export type ToolSession = unknown

export interface Tool {
  name: string
  cursor: string
  on_pointer_down(ctx: ToolContext, ev: ToolPointerEvent, session: ToolSession | null): ToolResult
  on_pointer_move(ctx: ToolContext, ev: ToolPointerEvent, session: ToolSession | null): ToolResult
  on_pointer_up  (ctx: ToolContext, ev: ToolPointerEvent, session: ToolSession | null): ToolResult
}

// ---------------------------------------------------------------------------
// Select tool — pointer-down picks a shape; drag moves it
// ---------------------------------------------------------------------------

interface SelectSession {
  kind: 'drag_move' | 'lasso'
  picked_path: SourcePath | null
  start: Vec2
  last: Vec2
}

const SELECT: Tool = {
  name: 'select',
  cursor: 'default',
  on_pointer_down(ctx, ev) {
    const hit = hitTestDrawing(ctx.drawing, ev.canvas)
    if (hit.kind === 'empty') {
      return { session: { kind: 'lasso', picked_path: null, start: ev.canvas, last: ev.canvas } satisfies SelectSession }
    }
    const fullPath: SourcePath = [...ctx.drawing_path, ...hit.path]
    return {
      session: { kind: 'drag_move', picked_path: fullPath, start: ev.canvas, last: ev.canvas } satisfies SelectSession
    }
  },
  on_pointer_move(ctx, ev, session) {
    if (session === null) return {}
    const s = session as SelectSession
    if (s.kind !== 'drag_move' || s.picked_path === null) {
      return { session: { ...s, last: ev.canvas } }
    }
    const dx = ev.canvas.x - s.last.x
    const dy = ev.canvas.y - s.last.y
    if (dx === 0 && dy === 0) return {}
    const tx = cmdMoveShapes(ctx.state, { shape_paths: [s.picked_path], dx, dy })
    return { tx, session: { ...s, last: ev.canvas } }
  },
  on_pointer_up(_ctx, _ev, _session) {
    return { session: null }
  }
}

// ---------------------------------------------------------------------------
// Rect tool — drag to create a rectangle
// ---------------------------------------------------------------------------

interface DragCreateSession {
  start: Vec2
  pending_id: string | null
  pending_path: SourcePath | null
}

function makeDragCreateTool(opts: {
  name: string
  cursor: string
  build: (r: Rect) => Node
}): Tool {
  return {
    name: opts.name,
    cursor: opts.cursor,
    on_pointer_down(ctx, ev) {
      const shape = opts.build({ x: ev.canvas.x, y: ev.canvas.y, width: 0, height: 0 })
      // Insert at end of first layer; record its path so on_pointer_move can mutate it
      const tx = cmdInsertShape(ctx.state, { drawing_path: ctx.drawing_path, layer_index: 0, shape })
      if (tx === null) return {}
      const layerPath: SourcePath = [...ctx.drawing_path, 0]
      const layer = nodeAt(tx.doc_after, layerPath)
      if (layer === null || !isNode(layer)) return { tx }
      const insertedIdx = layer.content.length - 1
      return {
        tx,
        session: {
          start: ev.canvas,
          pending_id: shape.attrs.find(a => a.name === 'id')?.value as string ?? null,
          pending_path: [...layerPath, insertedIdx]
        } satisfies DragCreateSession
      }
    },
    on_pointer_move(_ctx, ev, session) {
      if (session === null) return {}
      const s = session as DragCreateSession
      if (s.pending_path === null) return {}
      const r = rectFromPoints(s.start, ev.canvas)
      // Update via cmdResizeShape-like steps — directly assemble a tx
      // We import cmdResizeShape lazily to avoid a circular reference at module-init.
      // (Module-level import is fine; this comment is just a reminder.)
      const tx = resizeIfChanged(_ctx, s.pending_path, r)
      return { tx, session: s }
    },
    on_pointer_up(_ctx, _ev, session) {
      return { session: null === session ? null : null }
    }
  }
}

function resizeIfChanged(ctx: ToolContext, p: SourcePath, r: Rect): Transaction | null {
  // Avoid an unnecessary tx if the shape is already at this geometry.
  const n = nodeAt(ctx.state.doc, p)
  if (n === null || !isNode(n)) return null
  return importedResize(ctx.state, p, r)
}

// Imported lazily to dodge circular module-init issues with commands.ts
import { cmdResizeShape } from './commands.js'
function importedResize(state: EditorState, p: SourcePath, r: Rect): Transaction | null {
  return cmdResizeShape(state, { shape_path: p, x: r.x, y: r.y, width: r.width, height: r.height })
}

const RECT_TOOL    = makeDragCreateTool({ name: 'rect',    cursor: 'crosshair', build: r => makeRectShape(r) })
const ELLIPSE_TOOL = makeDragCreateTool({ name: 'ellipse', cursor: 'crosshair', build: r => makeEllipseShape(r) })
const LINE_TOOL    = makeDragCreateTool({ name: 'line',    cursor: 'crosshair', build: r => makeLineShape(r) })

// ---------------------------------------------------------------------------
// Pan tool — adjusts a viewport offset (held outside the editor state for now)
// ---------------------------------------------------------------------------

interface PanSession { start: Vec2; last: Vec2 }

const PAN_TOOL: Tool = {
  name: 'pan',
  cursor: 'grab',
  on_pointer_down(_ctx, ev) {
    return { session: { start: ev.canvas, last: ev.canvas } satisfies PanSession }
  },
  on_pointer_move(_ctx, _ev, session) {
    if (session === null) return {}
    return {}  // viewport tracking lives in the React component
  },
  on_pointer_up(_ctx, _ev, _session) {
    return { session: null }
  }
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

export const TOOLS: Record<string, Tool> = {
  select: SELECT,
  rect:    RECT_TOOL,
  ellipse: ELLIPSE_TOOL,
  line:    LINE_TOOL,
  pan:     PAN_TOOL
}

export function getTool(name: string): Tool | null {
  return TOOLS[name] ?? null
}

// Re-export Vec2 helper for callers
export { v2 }
