// DOM ↔ source position bridge.
//
// Every rendered element carries `data-source-path="0,1,2"` so we can map
// from a DOM boundary (Node + offset) back to a SourcePos against our model
// doc. Text leaves are wrapped in a single <span data-source-path> so their
// DOM boundaries are unambiguous.
//
// In a future Lambda port (Radiant), the equivalent role is played by
// render_map's reverse lookup; the React-only `data-source-path` annotation
// is dropped from production HTML output.

import type { SourcePath } from '../model/types.js'

export const SOURCE_PATH_ATTR = 'data-source-path'

export function stringifyPath(p: SourcePath): string {
  return p.join(',')
}

export function parsePath(s: string): SourcePath {
  if (s === '') return []
  return s.split(',').map(n => parseInt(n, 10))
}

// Find the rendered element for a given source path, scoped to a root.
export function findElementByPath(root: Element, p: SourcePath): Element | null {
  const sel = `[${SOURCE_PATH_ATTR}="${stringifyPath(p)}"]`
  if (root.getAttribute(SOURCE_PATH_ATTR) === stringifyPath(p)) return root
  return root.querySelector(sel)
}

// Walk up from a node to the nearest ancestor carrying data-source-path.
export function nearestPathOwner(n: Node | null): Element | null {
  let cur: Node | null = n
  while (cur !== null) {
    if (cur.nodeType === 1) {
      const el = cur as Element
      if (el.hasAttribute(SOURCE_PATH_ATTR)) return el
    }
    cur = cur.parentNode
  }
  return null
}

export function pathOf(el: Element): SourcePath {
  return parsePath(el.getAttribute(SOURCE_PATH_ATTR) ?? '')
}

// ---------------------------------------------------------------------------
// DOM Boundary → SourcePos
//
// The boundary is (containerNode, offset). Two cases:
//  1. containerNode is a text node inside a <span data-source-path>:
//        → SourcePos { path: span.path, offset: DOM offset within the text }
//  2. containerNode is an element with data-source-path (or its inline child):
//        → SourcePos { path: element.path, offset: child index }
// ---------------------------------------------------------------------------

export interface DomBoundary {
  node: Node
  offset: number
}

export function domBoundaryToSourcePos(
  b: DomBoundary
): { path: SourcePath; offset: number } | null {
  // text node → owning element holds the path (the wrapping span)
  if (b.node.nodeType === 3) {
    const owner = nearestPathOwner(b.node)
    if (owner === null) return null
    return { path: pathOf(owner), offset: b.offset }
  }
  if (b.node.nodeType === 1) {
    const el = b.node as Element
    if (el.hasAttribute(SOURCE_PATH_ATTR)) {
      return { path: pathOf(el), offset: b.offset }
    }
    const owner = nearestPathOwner(el)
    if (owner !== null) return { path: pathOf(owner), offset: b.offset }
  }
  return null
}

// ---------------------------------------------------------------------------
// SourcePos → DOM Boundary
// ---------------------------------------------------------------------------

export function sourcePosToDomBoundary(
  root: Element,
  pos: { path: SourcePath; offset: number }
): DomBoundary | null {
  const el = findElementByPath(root, pos.path)
  if (el === null) return null
  // If the element is a text-leaf wrapper (single text child), boundary points
  // into the text node.
  const first = el.firstChild
  if (first !== null && first.nodeType === 3 && el.childNodes.length === 1) {
    const text = first as Text
    const clamped = Math.min(Math.max(0, pos.offset), text.data.length)
    return { node: first, offset: clamped }
  }
  // Otherwise the offset is a child index in the element's children.
  const clamped = Math.min(Math.max(0, pos.offset), el.childNodes.length)
  return { node: el, offset: clamped }
}

// ---------------------------------------------------------------------------
// Selection sync (limited to TextSelection for now)
// ---------------------------------------------------------------------------

import type { Selection, SourcePos } from '../model/types.js'

export function setDomSelectionFromSource(
  root: Element,
  sel: Selection | null,
  windowRef: Window = window
): void {
  if (sel === null) return
  if (sel.kind !== 'text') return
  const domSel = windowRef.getSelection()
  if (domSel === null) return

  const anchor = sourcePosToDomBoundary(root, sel.anchor)
  const head   = sourcePosToDomBoundary(root, sel.head)
  if (anchor === null || head === null) return

  const range = (windowRef.document).createRange()
  range.setStart(anchor.node, anchor.offset)
  range.setEnd(head.node, head.offset)
  domSel.removeAllRanges()
  domSel.addRange(range)
}

export function getSourceSelectionFromDom(
  windowRef: Window = window
): { anchor: SourcePos; head: SourcePos } | null {
  const sel = windowRef.getSelection()
  if (sel === null || sel.rangeCount === 0) return null
  const range = sel.getRangeAt(0)
  const a = domBoundaryToSourcePos({ node: range.startContainer, offset: range.startOffset })
  const h = domBoundaryToSourcePos({ node: range.endContainer,   offset: range.endOffset })
  if (a === null || h === null) return null
  return { anchor: { path: a.path, offset: a.offset }, head: { path: h.path, offset: h.offset } }
}
