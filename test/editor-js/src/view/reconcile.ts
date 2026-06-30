// Keyed, selection-preserving DOM reconciler (Stage 4B).
//
// The one genuinely new component of the plain-DOM port: it commits a VNode
// tree (render-vnode.ts) to real DOM, patching in place and keyed by
// `data-source-path` so reorder/insert/delete keep node identity — which keeps
// the native caret and focus intact across edits. It is the JS analogue of the
// Lambda `render_map` reconciler (also keyed by path / shape-id).

import type { VEl, VNode } from './vnode.js'

const SVG_NS = 'http://www.w3.org/2000/svg'
const SOURCE_PATH_ATTR = 'data-source-path'

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

// Patch `container`'s children to match `vChildren`. The container element
// itself (e.g. the contenteditable root) is owned by the caller and untouched.
export function reconcile(container: Element, vChildren: VNode[]): void {
  patchChildren(container, vChildren)
}

// Convenience: reconcile a single root VNode (the rendered <doc>) as the sole
// child of `container`.
export function reconcileDoc(container: Element, docVNode: VNode): void {
  patchChildren(container, [docVNode])
}

// ---------------------------------------------------------------------------
// Node creation
// ---------------------------------------------------------------------------

function createDom(v: VNode): Node {
  if (v.kind === 'text') return document.createTextNode(v.text)
  const elem = v.ns
    ? document.createElementNS(SVG_NS, v.tag)
    : document.createElement(v.tag)
  applyAttrs(elem, v.attrs)
  for (const child of v.children) elem.appendChild(createDom(child))
  return elem
}

// ---------------------------------------------------------------------------
// Patch
// ---------------------------------------------------------------------------

function sameType(dom: Node, v: VNode): boolean {
  if (v.kind === 'text') return dom.nodeType === 3
  return dom.nodeType === 1 && (dom as Element).localName === v.tag
}

// patch `dom` to match `v` (precondition: sameType(dom, v)). Returns `dom`.
function patchNode(dom: Node, v: VNode): Node {
  if (v.kind === 'text') {
    if ((dom as Text).data !== v.text) (dom as Text).data = v.text
    return dom
  }
  const elem = dom as Element
  applyAttrs(elem, v.attrs)
  patchChildren(elem, v.children)
  return dom
}

function applyAttrs(elem: Element, attrs: VEl['attrs']): void {
  const desired = new Set<string>()
  for (const name in attrs) {
    const val = attrs[name]
    if (val === undefined || val === null || val === false) continue
    desired.add(name)
    const next = val === true ? '' : String(val)
    if (elem.getAttribute(name) !== next) elem.setAttribute(name, next)
  }
  // remove attributes no longer desired (collect first — live during removal)
  const stale: string[] = []
  for (let i = 0; i < elem.attributes.length; i++) {
    const name = elem.attributes[i]!.name
    if (!desired.has(name)) stale.push(name)
  }
  for (const name of stale) elem.removeAttribute(name)
}

function keyOf(node: Node): string | null {
  if (node.nodeType !== 1) return null
  return (node as Element).getAttribute(SOURCE_PATH_ATTR)
}

function patchChildren(parent: Element, vChildren: VNode[]): void {
  const existing: ChildNode[] = Array.from(parent.childNodes)

  // index keyed children for O(1) reuse across reorders
  const keyMap = new Map<string, ChildNode>()
  for (const c of existing) {
    const k = keyOf(c)
    if (k !== null) keyMap.set(k, c)
  }

  const used = new Set<ChildNode>()
  const target: Node[] = []
  let unkeyedCursor = 0

  for (const v of vChildren) {
    let match: ChildNode | undefined
    const vkey = v.kind === 'el' ? v.key : null

    if (vkey !== null) {
      const cand = keyMap.get(vkey)
      if (cand !== undefined && !used.has(cand) && sameType(cand, v)) match = cand
    } else {
      // match the next reusable unkeyed existing child of the same type
      // (placeholder <br>, leaf text, zero-width text) by order
      while (unkeyedCursor < existing.length) {
        const cand = existing[unkeyedCursor++]!
        if (used.has(cand) || keyOf(cand) !== null) continue
        if (sameType(cand, v)) { match = cand; break }
      }
    }

    if (match !== undefined) {
      target.push(patchNode(match, v))
      used.add(match)
    } else {
      target.push(createDom(v))
    }
  }

  // drop existing children no longer present
  for (const c of existing) {
    if (!used.has(c) && c.parentNode === parent) parent.removeChild(c)
  }

  // place target nodes in order (insertBefore moves or inserts as needed)
  for (let i = 0; i < target.length; i++) {
    const want = target[i]!
    const have = parent.childNodes[i] ?? null
    if (have !== want) parent.insertBefore(want, have)
  }
}
