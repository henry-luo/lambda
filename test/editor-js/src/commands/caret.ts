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
import { pos } from '../model/source-pos.js'
import type { Child, Doc, SourcePath, SourcePos, TextSelection } from '../model/types.js'
import type { EditorState } from './types.js'
import { txBegin, txSetMeta, txSetSelection } from '../model/transaction.js'
import type { Transaction } from '../model/types.js'

export type Granularity = 'character' | 'word' | 'documentboundary'
export type Direction = 'forward' | 'backward' | 'left' | 'right'
export type Alter = 'move' | 'extend'

interface Stop { path: SourcePath; offset: number }

// Enumerate caret stops in document order, with the concatenated text and a
// per-stop global char index (for word segmentation).
function enumerate(doc: Doc): { stops: Stop[]; text: string; globalOf: number[] } {
  const stops: Stop[] = []
  const globalOf: number[] = []
  let text = ''
  const walk = (n: Child, path: SourcePath): void => {
    if (isText(n)) {
      for (let o = 0; o <= n.text.length; o++) {
        stops.push({ path, offset: o })
        globalOf.push(text.length + o)
      }
      text += n.text
      return
    }
    if (isNode(n)) {
      if (n.content.length === 0 && path.length > 0) {
        // empty block — a single stop
        stops.push({ path, offset: 0 })
        globalOf.push(text.length)
        return
      }
      for (let i = 0; i < n.content.length; i++) walk(n.content[i] as Child, [...path, i])
    }
  }
  walk(doc, [])
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
  if (sel === null || sel.kind !== 'text') return null
  const { stops, text, globalOf } = enumerate(state.doc)
  if (stops.length === 0) return null

  const headIdx = indexOf(stops, sel.head)
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
  const headPos = pos(target.path, target.offset)
  const anchorPos = alter === 'extend' ? sel.anchor : headPos
  const next: TextSelection = { kind: 'text', anchor: anchorPos, head: headPos }

  // Movement does not mutate the doc — emit a selection-only, non-history tx.
  let tx = txBegin(state.doc, sel)
  tx = txSetSelection(tx, next)
  tx = txSetMeta(tx, 'addToHistory', false)
  return tx
}
