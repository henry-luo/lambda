// Caret navigation — `selection.modify`-equivalent movement over the document
// model. Implements character and word granularity in both directions, for
// 'move' (collapse) and 'extend'. Line/lineboundary need layout and are NOT
// handled here (callers should skip them).
//
// Movement is defined over an enumeration of caret "stops": every text-leaf
// offset in document order, plus one stop per empty block. This is headless
// (no layout) and self-consistent — sufficient for replaying real editing
// sequences (type / move / type) through the editor.

import { isNode, isText } from '../model/doc.js'
import { gapSelection, pos } from '../model/source-pos.js'
import { schemaAllowsInline } from '../model/schema.js'
import { isBlockAtom } from './gap-cursor.js'
import type { Schema } from '../model/schema.js'
import type { Child, Doc, Selection, SourcePath, SourcePos } from '../model/types.js'
import type { EditorState } from './types.js'
import { txBegin, txSetMeta, txSetSelection } from '../model/transaction.js'
import type { Transaction } from '../model/types.js'

export type Granularity = 'character' | 'word' | 'documentboundary'
export type Direction = 'forward' | 'backward' | 'left' | 'right'
export type Alter = 'move' | 'extend'

// A caret stop: a text-leaf offset, an empty-block position, or (gap:true) a gap
// cursor at an insertion index — `path` is then [...containerPath, index].
interface Stop { path: SourcePath; offset: number; gap?: boolean }

function pathEq(a: SourcePath, b: SourcePath): boolean {
  return a.length === b.length && a.every((v, i) => v === b[i])
}

// Enumerate caret stops in document order, with the concatenated text and a
// per-stop global char index (for word segmentation).
function enumerate(doc: Doc, schema: Schema): { stops: Stop[]; text: string; globalOf: number[] } {
  const stops: Stop[] = []
  const globalOf: number[] = []
  let text = ''
  // `inInline` = this node sits in an inline context (its parent holds inline
  // content), so an atom here is an INLINE atom (e.g. an <img> in a paragraph),
  // which keeps a normal node stop — only BLOCK-context atoms become gaps.
  const walk = (n: Child, path: SourcePath, inInline: boolean): void => {
    if (isText(n)) {
      for (let o = 0; o <= n.text.length; o++) {
        stops.push({ path, offset: o })
        globalOf.push(text.length + o)
      }
      text += n.text
      return
    }
    if (isNode(n)) {
      // A block-context atom (hr/img/drawing) gets a gap stop BEFORE it instead
      // of a text stop — you cannot put a text caret inside it.
      if (path.length > 0 && isBlockAtom(n.tag) && !inInline) {
        stops.push({ path, offset: 0, gap: true })
        globalOf.push(text.length)
        return
      }
      if (n.content.length === 0 && path.length > 0) {
        // empty block, or an inline atom (image/br) — a single node stop
        stops.push({ path, offset: 0 })
        globalOf.push(text.length)
        return
      }
      const childInline = schemaAllowsInline(schema, n.tag)
      for (let i = 0; i < n.content.length; i++) {
        walk(n.content[i] as Child, [...path, i], childInline)
        // A trailing block atom also needs a gap AFTER it (e.g. doc ends with an <hr>).
        const child = n.content[i] as Child
        if (i === n.content.length - 1 && !childInline && child.kind === 'node' && isBlockAtom(child.tag)) {
          stops.push({ path: [...path, i + 1], offset: 0, gap: true })
          globalOf.push(text.length)
        }
      }
    }
  }
  walk(doc, [], false)
  return { stops, text, globalOf }
}

function sameStop(a: Stop, b: SourcePos): boolean {
  return a.offset === b.offset && a.path.length === b.path.length && a.path.every((v, i) => v === b.path[i])
}

// Index of the stop matching `p`, or the nearest stop at/after it in doc order.
function indexOf(stops: Stop[], p: SourcePos): number {
  for (let i = 0; i < stops.length; i++) if (sameStop(stops[i] as Stop, p)) return i
  return 0
}

function isForward(dir: Direction): boolean {
  return dir === 'forward' || dir === 'right'
}

// Word-boundary stop index in the given direction, using Intl.Segmenter.
function wordTarget(text: string, globalOf: number[], idx: number, forward: boolean): number {
  const seg = new Intl.Segmenter(undefined, { granularity: 'word' })
  // Collect word-start global indices.
  const starts: number[] = []
  for (const s of seg.segment(text)) {
    if ((s as { isWordLike?: boolean }).isWordLike) {
      starts.push(s.index)
      starts.push(s.index + s.segment.length)  // word end is also a stop browsers land on
    }
  }
  const cur = globalOf[idx] ?? 0
  if (forward) {
    const next = starts.find(g => g > cur)
    const target = next ?? text.length
    return nearestStopAtGlobal(globalOf, target, true)
  } else {
    const prev = [...starts].reverse().find(g => g < cur)
    const target = prev ?? 0
    return nearestStopAtGlobal(globalOf, target, false)
  }
}

function nearestStopAtGlobal(globalOf: number[], target: number, forward: boolean): number {
  // Find a stop index whose global offset equals target (prefer exact).
  let best = forward ? globalOf.length - 1 : 0
  let bestDist = Infinity
  for (let i = 0; i < globalOf.length; i++) {
    const g = globalOf[i] as number
    const d = Math.abs(g - target)
    if (d < bestDist) { bestDist = d; best = i }
  }
  return best
}

export function cmdMoveCaret(state: EditorState, alter: Alter, dir: Direction, gran: Granularity): Transaction | null {
  const sel = state.selection
  if (sel === null || (sel.kind !== 'text' && sel.kind !== 'gap')) return null
  const { stops, text, globalOf } = enumerate(state.doc, state.schema)
  if (stops.length === 0) return null

  // Locate the current head among the stops (a gap matches by path).
  let headIdx: number
  if (sel.kind === 'gap') {
    headIdx = stops.findIndex(s => s.gap === true && pathEq(s.path, sel.path))
    if (headIdx < 0) headIdx = 0
  } else {
    headIdx = indexOf(stops, sel.head)
  }
  const fwd = isForward(dir)
  let targetIdx: number
  if (gran === 'documentboundary') {
    targetIdx = fwd ? stops.length - 1 : 0
  } else if (gran === 'word') {
    targetIdx = wordTarget(text, globalOf, headIdx, fwd)
  } else {
    targetIdx = Math.max(0, Math.min(stops.length - 1, headIdx + (fwd ? 1 : -1)))
  }
  const target = stops[targetIdx] as Stop

  // A gap target produces a GapSelection; extend collapses onto it (a gap is not
  // a range endpoint).
  let next: Selection
  if (target.gap === true) {
    next = gapSelection(target.path)
  } else {
    const headPos = pos(target.path, target.offset)
    const anchorPos = alter === 'extend' && sel.kind === 'text' ? sel.anchor : headPos
    next = { kind: 'text', anchor: anchorPos, head: headPos }
  }

  // Movement does not mutate the doc — emit a selection-only, non-history tx.
  let tx = txBegin(state.doc, sel)
  tx = txSetSelection(tx, next)
  tx = txSetMeta(tx, 'addToHistory', false)
  return tx
}
