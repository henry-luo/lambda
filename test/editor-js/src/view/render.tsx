// React rendering of a Doc.
//
// `renderDoc(doc)` returns a single React element tree. Each rendered DOM
// element carries `data-source-path` for the DOM↔source bridge. Text leaves
// are wrapped in <span data-source-path> so their boundaries are unambiguous;
// mark wrappers (strong/em/...) nest around the span.

import { isText } from '../model/doc.js'
import type { Attr, Child, Doc, Mark, Node, SourcePath } from '../model/types.js'
import { stringifyPath } from './dom-bridge.js'
import * as React from 'react'

interface RenderProps {
  child: Child
  path: SourcePath
}

export function renderDoc(doc: Doc): React.ReactElement {
  return <RenderNode child={doc} path={[]} />
}

function RenderNode({ child, path }: RenderProps): React.ReactElement {
  if (isText(child)) {
    return <RenderTextLeaf leaf={child} path={path} />
  }
  return <RenderElement node={child} path={path} />
}

// ---------------------------------------------------------------------------
// Elements
// ---------------------------------------------------------------------------

interface RenderElementProps {
  node: Node
  path: SourcePath
}

function RenderElement({ node, path }: RenderElementProps): React.ReactElement {
  const tag = node.tag === 'doc' ? 'div' : node.tag
  const props: Record<string, unknown> = {
    'data-source-path': stringifyPath(path),
    'data-tag': node.tag,
    ...attrsToProps(node.attrs)
  }
  if (node.tag === 'doc') {
    props['className'] = 'rdt-editor'
  }
  const children = node.content.length === 0
    ? null
    : node.content.map((c, i) => (
        <RenderNode key={i} child={c} path={[...path, i]} />
      ))
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
  leaf: { kind: 'text'; text: string; marks: Mark[] }
  path: SourcePath
}

function RenderTextLeaf({ leaf, path }: TextLeafProps): React.ReactElement {
  // The data-source-path goes on the innermost span; mark wrappers are
  // purely visual. This keeps DOM boundary → SourcePos mapping unambiguous.
  let inner: React.ReactElement = (
    <span data-source-path={stringifyPath(path)} data-tag="text">
      {leaf.text.length === 0 ? '​' : leaf.text}
    </span>
  )
  for (const m of leaf.marks) {
    inner = React.createElement(m, {}, inner)
  }
  return inner
}
