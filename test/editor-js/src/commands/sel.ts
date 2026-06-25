// Selection helpers used by commands. Mirror the inline helpers in
// lambda/package/editor/mod_commands.ls (sel_lo, sel_hi, sel_collapsed,
// sel_single_leaf, etc.).

import { pathEqual, pos, posMax, posMin } from '../model/source-pos.js'
import type { Selection, SourcePos, TextSelection } from '../model/types.js'

export function isText(sel: Selection): sel is TextSelection {
  return sel.kind === 'text'
}

export function selLo(sel: TextSelection): SourcePos {
  return posMin(sel.anchor, sel.head)
}

export function selHi(sel: TextSelection): SourcePos {
  return posMax(sel.anchor, sel.head)
}

export function selCollapsed(sel: TextSelection): boolean {
  return pathEqual(sel.anchor.path, sel.head.path) && sel.anchor.offset === sel.head.offset
}

export function selSingleLeaf(sel: TextSelection): boolean {
  return pathEqual(sel.anchor.path, sel.head.path)
}

export function caret(p: SourcePos): TextSelection {
  return { kind: 'text', anchor: p, head: p }
}

export function caretAt(path: number[], offset: number): TextSelection {
  return caret(pos(path, offset))
}
