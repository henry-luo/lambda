// Inline-content primitives shared by the editing commands (paste, delete,
// join). Pure functions over a block's Child[] content.

import { isText } from './doc.js'
import { marksEqual } from './step.js'
import { pos } from './source-pos.js'
import type { Child, SourcePath, SourcePos, TextLeaf } from './types.js'

// Merge adjacent text leaves that share marks (keeps results round-trippable).
export function mergeInlines(children: Child[]): Child[] {
  const out: Child[] = []
  for (const c of children) {
    const last = out[out.length - 1]
    if (last !== undefined && isText(last) && isText(c) && marksEqual(last.marks, c.marks)) {
      out[out.length - 1] = { kind: 'text', text: last.text + c.text, marks: last.marks }
    } else {
      out.push(c)
    }
  }
  return out
}

// Char-addressable width of inline content (text length; inline nodes are width 1).
export function charLen(children: Child[]): number {
  let n = 0
  for (const c of children) n += isText(c) ? c.text.length : 1
  return n
}

// SourcePos (relative to `basePath`) for the Nth char-addressable offset within
// `content`. A text offset resolves to a leaf position; a boundary between
// inline nodes resolves to a node position at `basePath`.
export function caretPosInContent(basePath: SourcePath, content: Child[], target: number): SourcePos {
  let acc = 0
  for (let i = 0; i < content.length; i++) {
    const c = content[i] as Child
    if (isText(c)) {
      if (target <= acc + c.text.length) return pos([...basePath, i], target - acc)
      acc += c.text.length
    } else {
      if (target <= acc) return pos(basePath, i)
      acc += 1
    }
  }
  const n = content.length
  if (n > 0 && isText(content[n - 1])) return pos([...basePath, n - 1], (content[n - 1] as TextLeaf).text.length)
  return pos(basePath, n)
}
