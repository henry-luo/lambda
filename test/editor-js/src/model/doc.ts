// Port of lambda/package/editor/mod_doc.ls
//
// Document shape (immutable, JSON-native):
//   TextLeaf = { kind: 'text', text: string, marks: Mark[] }
//   Node     = { kind: 'node', tag: string, attrs: Attr[], content: Child[] }
//
// SourcePath = number[]
// SourcePos.offset on a text leaf  -> UTF-16 unit offset in `text`
// SourcePos.offset on a non-leaf   -> child index in [0, content.length]
//
// Note on offset units: Lambda's mod_doc.ls says "UTF-8 byte offset" for text
// leaves. JS strings are UTF-16 so this reference implementation uses UTF-16
// unit offsets natively. A future Lambda port will need a UTF-16↔UTF-8 adapter
// at the boundary; the algorithm is identical.

import type { Attr, AttrValue, Child, Doc, Mark, Node, SourcePath, TextLeaf } from './types.js'

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

export function text(s: string): TextLeaf {
  return { kind: 'text', text: s, marks: [] }
}

export function textMarked(s: string, marks: Mark[]): TextLeaf {
  return { kind: 'text', text: s, marks }
}

export function node(tag: string, content: Child[]): Node {
  return { kind: 'node', tag, attrs: [], content }
}

export function nodeAttrs(tag: string, attrs: Attr[], content: Child[]): Node {
  return { kind: 'node', tag, attrs, content }
}

// ---------------------------------------------------------------------------
// Predicates
// ---------------------------------------------------------------------------

export function isText(n: unknown): n is TextLeaf {
  return typeof n === 'object' && n !== null && (n as Child).kind === 'text'
}

export function isNode(n: unknown): n is Node {
  return typeof n === 'object' && n !== null && (n as Child).kind === 'node'
}

// ---------------------------------------------------------------------------
// Pure list ops (immutable)
// ---------------------------------------------------------------------------

export function listTake<T>(arr: T[], k: number): T[] {
  return arr.slice(0, k)
}

export function listDrop<T>(arr: T[], k: number): T[] {
  return arr.slice(k)
}

export function listConcat<T>(a: T[], b: T[]): T[] {
  return [...a, ...b]
}

export function listSet<T>(arr: T[], i: number, x: T): T[] {
  const r = arr.slice()
  r[i] = x
  return r
}

export function listSplice<T>(arr: T[], start: number, deleteCount: number, items: T[]): T[] {
  return [...arr.slice(0, start), ...items, ...arr.slice(start + deleteCount)]
}

// ---------------------------------------------------------------------------
// Tree navigation
// ---------------------------------------------------------------------------

export function nodeAt(doc: Doc, path: SourcePath): Child | null {
  let cursor: Child = doc
  for (let i = 0; i < path.length; i++) {
    if (!isNode(cursor)) return null
    const idx = path[i] as number
    if (idx >= cursor.content.length) return null
    cursor = cursor.content[idx] as Child
  }
  return cursor
}

export function parentPath(path: SourcePath): SourcePath {
  return path.length === 0 ? [] : path.slice(0, path.length - 1)
}

export function lastIndex(path: SourcePath): number {
  return path.length === 0 ? -1 : (path[path.length - 1] as number)
}

// ---------------------------------------------------------------------------
// Tree updates (immutable)
// ---------------------------------------------------------------------------

export function withContent(n: Node, newContent: Child[]): Node {
  return { kind: 'node', tag: n.tag, attrs: n.attrs, content: newContent }
}

export function replaceNodeAt(doc: Doc, path: SourcePath, newNode: Child): Doc {
  if (path.length === 0) {
    if (!isNode(newNode)) {
      throw new Error('replaceNodeAt: cannot replace root with a text leaf')
    }
    return newNode
  }
  return replaceAtStep(doc, path, 0, newNode) as Doc
}

function replaceAtStep(node_: Child, path: SourcePath, i: number, newNode: Child): Child {
  if (i >= path.length) return newNode
  if (!isNode(node_)) {
    throw new Error('replaceAtStep: path runs through a non-node')
  }
  const idx = path[i] as number
  const child = node_.content[idx] as Child
  const child2 = replaceAtStep(child, path, i + 1, newNode)
  return withContent(node_, listSet(node_.content, idx, child2))
}

export function spliceChildrenAt(
  doc: Doc,
  parentP: SourcePath,
  start: number,
  deleteCount: number,
  items: Child[]
): Doc {
  const parent = nodeAt(doc, parentP)
  if (!isNode(parent)) {
    throw new Error('spliceChildrenAt: parent is not a node')
  }
  const newContent = listSplice(parent.content, start, deleteCount, items)
  return replaceNodeAt(doc, parentP, withContent(parent, newContent))
}

// ---------------------------------------------------------------------------
// Attr helpers (used by step.ts)
// ---------------------------------------------------------------------------

export function attrsGet(attrs: Attr[], name: string): AttrValue | null {
  for (const a of attrs) if (a.name === name) return a.value
  return null
}

// Sets / removes an attribute by name. `value === null` removes the entry.
// If the attribute does not exist and `value !== null`, it is appended.
export function attrsSet(attrs: Attr[], name: string, value: AttrValue | null): Attr[] {
  const out: Attr[] = []
  let found = false
  for (const a of attrs) {
    if (a.name === name) {
      found = true
      if (value !== null) out.push({ name, value })
    } else {
      out.push(a)
    }
  }
  if (!found && value !== null) out.push({ name, value })
  return out
}

// ---------------------------------------------------------------------------
// Text content extraction
// ---------------------------------------------------------------------------

export function docText(n: Child): string {
  if (isText(n)) return n.text
  if (isNode(n)) {
    let out = ''
    for (const c of n.content) out += docText(c)
    return out
  }
  return ''
}
