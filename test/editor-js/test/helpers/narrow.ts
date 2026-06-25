// Small narrowing helpers used in unit tests. Each throws a descriptive error
// if the runtime value doesn't match the expected shape — turning a TS
// "possibly null" / discriminated-union error into a one-liner at the use site.

import { isNode, isText, nodeAt } from '../../src/model/doc.js'
import type {
  Attr,
  Child,
  Doc,
  MarkDict,
  Node,
  ResolvedPos,
  SourcePath,
  TextLeaf
} from '../../src/model/types.js'

export function asText(n: Child | null): TextLeaf {
  if (n === null) throw new Error('asText: got null')
  if (!isText(n)) throw new Error(`asText: expected text leaf, got node <${(n as Node).tag}>`)
  return n
}

export function asNode(n: Child | null): Node {
  if (n === null) throw new Error('asNode: got null')
  if (!isNode(n)) throw new Error(`asNode: expected node, got text "${(n as TextLeaf).text}"`)
  return n
}

// Convenience accessors that combine nodeAt + narrowing.
export function textAt(doc: Doc, path: SourcePath): string {
  return asText(nodeAt(doc, path)).text
}

export function tagAt(doc: Doc, path: SourcePath): string {
  return asNode(nodeAt(doc, path)).tag
}

export function attrsAt(doc: Doc, path: SourcePath): Attr[] {
  return asNode(nodeAt(doc, path)).attrs
}

export function marksAt(doc: Doc, path: SourcePath): MarkDict {
  return asText(nodeAt(doc, path)).marks
}

export function resolvedParentTag(r: ResolvedPos): string {
  return asNode(r.parent).tag
}
