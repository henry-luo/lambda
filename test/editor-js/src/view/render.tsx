// React rendering of a Doc.
//
// `renderDoc(doc)` returns a single React element tree. Each rendered DOM
// element carries `data-source-path` for the DOM↔source bridge. Text leaves
// are wrapped in <span data-source-path> so their boundaries are unambiguous;
// mark wrappers (strong/em/...) nest around the span.

import { isText } from '../model/doc.js'
import type { Attr, Child, Doc, MarkDict, Node, SourcePath } from '../model/types.js'
import { stringifyPath } from './dom-bridge.js'
import { DrawingView } from './drawing/DrawingView.js'
import * as React from 'react'

interface RenderProps {
  doc: Doc
  child: Child
  path: SourcePath
}

export function renderDoc(doc: Doc): React.ReactElement {
  return <RenderNode doc={doc} child={doc} path={[]} />
}

function RenderNode({ doc, child, path }: RenderProps): React.ReactElement {
  if (isText(child)) {
    return <RenderTextLeaf leaf={child} path={path} />
  }
  if (child.tag === 'drawing') {
    return <DrawingView doc={doc} node={child} path={path} />
  }
  return <RenderElement doc={doc} node={child} path={path} />
}

// ---------------------------------------------------------------------------
// Elements
// ---------------------------------------------------------------------------

interface RenderElementProps {
  doc: Doc
  node: Node
  path: SourcePath
}

// Void / atomic tags that must not receive a placeholder <br>.
const VOID_TAGS = new Set(['img', 'hr', 'br'])

function RenderElement({ doc, node, path }: RenderElementProps): React.ReactElement {
  const tag = node.tag === 'doc' ? 'div' : node.tag
  const props: Record<string, unknown> = {
    'data-source-path': stringifyPath(path),
    'data-tag': node.tag,
    ...attrsToProps(node.attrs)
  }
  if (node.tag === 'doc') {
    props['className'] = 'rdt-editor'
  }
  // List-item indent level → left margin (flat indent model). Drop the raw
  // `indent` attribute so it doesn't render as an invalid DOM attribute.
  if (node.tag === 'li' || node.tag === 'list_item') {
    const indent = typeof props['indent'] === 'number' ? (props['indent'] as number) : 0
    delete props['indent']
    if (indent > 0) props['style'] = { marginInlineStart: `${indent * 1.75}em` }
  }
  let children: React.ReactNode
  if (node.content.length === 0) {
    // Empty container blocks render a <br> so contentEditable gives them a
    // line box and the caret can land inside (a truly-empty <p> collapses to
    // zero height). Void elements (img/hr/br) stay childless.
    children = VOID_TAGS.has(node.tag) || node.tag === 'doc'
      ? null
      : <br data-placeholder="true" />
  } else {
    children = node.content.map((c, i) => (
      <RenderNode key={i} doc={doc} child={c} path={[...path, i]} />
    ))
  }
  return React.createElement(tag, props, children)
}

function attrsToProps(attrs: Attr[]): Record<string, unknown> {
  const out: Record<string, unknown> = {}
  for (const a of attrs) {
    // React expects camelCase for built-in attributes (className, htmlFor),
    // but for arbitrary HTML attributes (like data-*, custom), the kebab-case
    // attribute names work via setAttribute. We pass them through verbatim
    // for now — sufficient for our schema-known attributes.
    out[a.name] = a.value
  }
  return out
}

// ---------------------------------------------------------------------------
// Text leaves (with mark wrapping)
// ---------------------------------------------------------------------------

interface TextLeafProps {
  leaf: { kind: 'text'; text: string; marks: MarkDict }
  path: SourcePath
}

// Renderer — per Inline_Formatting design §7. Walks the mark dict and emits
// a SINGLE wrapping element (no nesting). Semantic marks (link, code) win
// over <span>; style marks contribute a `style` attribute as a React style
// object (which React serializes to kebab-case CSS in HTML output).
function RenderTextLeaf({ leaf, path }: TextLeafProps): React.ReactElement {
  const tag = pickWrapperTag(leaf.marks)
  const props: Record<string, unknown> = {
    'data-source-path': stringifyPath(path),
    'data-tag': 'text'
  }
  const style = computeStyleObject(leaf.marks)
  if (Object.keys(style).length > 0) props['style'] = style
  if (tag === 'a' && typeof leaf.marks['link'] === 'string') props['href'] = leaf.marks['link']
  const child = leaf.text.length === 0 ? '​' : leaf.text
  return React.createElement(tag, props, child)
}

function pickWrapperTag(marks: MarkDict): string {
  if ('link' in marks) return 'a'
  if ('code' in marks) return 'code'
  return 'span'
}

function computeStyleObject(marks: MarkDict): Record<string, string> {
  const out: Record<string, string> = {}
  if (marks['bold']           === true) out['fontWeight'] = 'bold'
  if (marks['italic']         === true) out['fontStyle']  = 'italic'
  if (marks['underline']      === true && marks['strikethrough'] === true) out['textDecoration'] = 'underline line-through'
  else if (marks['underline'] === true) out['textDecoration'] = 'underline'
  else if (marks['strikethrough'] === true) out['textDecoration'] = 'line-through'
  if (typeof marks['color']        === 'string') out['color']           = marks['color']
  if (typeof marks['background']   === 'string') out['backgroundColor'] = marks['background']
  if (typeof marks['fontFamily']   === 'string') out['fontFamily']      = marks['fontFamily']
  if (typeof marks['fontSize']     === 'string') out['fontSize']        = marks['fontSize']
  return out
}
