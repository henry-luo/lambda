// Pure `doc → VNode` renderer (Stage 4B) — the framework-free twin of
// render.tsx. Same output contract: every element carries `data-source-path`
// for the DOM↔source bridge; text leaves are a single wrapper element with one
// text child; mark wrappers are non-nested; empty containers get a placeholder
// <br>; list-item `indent` becomes a left margin. Drawings render as SVG.

import { isText, isNode } from '../model/doc.js'
import type { Attr, Child, Doc, MarkDict, Node, SourcePath } from '../model/types.js'
import { stringifyPath } from './dom-bridge.js'
import { computeRoute, routeToCurvedSvgPath, routeToSvgPath } from '../drawing/router.js'
import { getShapeAttrNumber, getShapeAttrString, getShapeKind } from '../drawing/shape-utils.js'
import { rectCenter, type Rect } from '../drawing/geom.js'
import { el, txt, type VAttrs, type VNode } from './vnode.js'

const VOID_TAGS = new Set(['img', 'hr', 'br'])
const ZERO_WIDTH = '​'

export function renderDoc(doc: Doc): VNode {
  return renderChild(doc, doc, [])
}

function renderChild(doc: Doc, child: Child, path: SourcePath): VNode {
  if (isText(child)) return renderTextLeaf(child, path)
  if (child.tag === 'drawing') return renderDrawing(doc, child, path)
  if (child.tag === 'mention') return renderMention(child, path)
  return renderElement(doc, child, path)
}

// ---------------------------------------------------------------------------
// Elements
// ---------------------------------------------------------------------------

function renderElement(doc: Doc, node: Node, path: SourcePath): VNode {
  const tag = node.tag === 'doc' ? 'div' : node.tag
  const attrs: VAttrs = {
    'data-source-path': stringifyPath(path),
    'data-tag': node.tag,
    ...attrsToVAttrs(node.attrs)
  }
  if (node.tag === 'doc') attrs['class'] = 'rdt-editor'

  // list-item indent level → left margin (flat indent model); drop the raw
  // `indent` attribute so it doesn't render as an invalid DOM attribute.
  if (node.tag === 'li' || node.tag === 'list_item') {
    const indent = typeof attrs['indent'] === 'number' ? (attrs['indent'] as number) : 0
    delete attrs['indent']
    if (indent > 0) attrs['style'] = `margin-inline-start:${indent * 1.75}em`
  }

  let children: VNode[]
  if (node.content.length === 0) {
    // empty container blocks render a <br> so contenteditable gives them a line
    // box; void elements and the doc root stay childless.
    children = VOID_TAGS.has(node.tag) || node.tag === 'doc'
      ? []
      : [el('br', { 'data-placeholder': 'true' })]
  } else {
    children = node.content.map((c, i) => renderChild(doc, c, [...path, i]))
  }
  return el(tag, attrs, children)
}

function renderMention(child: Node, path: SourcePath): VNode {
  const label = (child.attrs.find(a => a.name === 'label')?.value as string) ?? ''
  return el(
    'span',
    {
      'data-source-path': stringifyPath(path),
      'data-tag': 'mention',
      'class': 'rdt-mention',
      'contenteditable': 'false'
    },
    [txt('@' + label)]
  )
}

function attrsToVAttrs(attrs: Attr[]): VAttrs {
  const out: VAttrs = {}
  for (const a of attrs) out[a.name] = a.value as VAttrs[string]
  return out
}

// ---------------------------------------------------------------------------
// Text leaves (single non-nested wrapper, marks → style)
// ---------------------------------------------------------------------------

function renderTextLeaf(leaf: { kind: 'text'; text: string; marks: MarkDict }, path: SourcePath): VNode {
  const tag = pickWrapperTag(leaf.marks)
  const attrs: VAttrs = {
    'data-source-path': stringifyPath(path),
    'data-tag': 'text'
  }
  const style = computeStyleString(leaf.marks)
  if (style !== '') attrs['style'] = style
  if (tag === 'a' && typeof leaf.marks['link'] === 'string') attrs['href'] = leaf.marks['link']
  const child = leaf.text.length === 0 ? ZERO_WIDTH : leaf.text
  return el(tag, attrs, [txt(child)])
}

function pickWrapperTag(marks: MarkDict): string {
  if ('link' in marks) return 'a'
  if ('code' in marks) return 'code'
  return 'span'
}

function computeStyleString(marks: MarkDict): string {
  const decls: string[] = []
  if (marks['bold'] === true) decls.push('font-weight:bold')
  if (marks['italic'] === true) decls.push('font-style:italic')
  if (marks['underline'] === true && marks['strikethrough'] === true) decls.push('text-decoration:underline line-through')
  else if (marks['underline'] === true) decls.push('text-decoration:underline')
  else if (marks['strikethrough'] === true) decls.push('text-decoration:line-through')
  if (typeof marks['color'] === 'string') decls.push(`color:${marks['color']}`)
  if (typeof marks['background'] === 'string') decls.push(`background-color:${marks['background']}`)
  if (typeof marks['fontFamily'] === 'string') decls.push(`font-family:${marks['fontFamily']}`)
  if (typeof marks['fontSize'] === 'string') decls.push(`font-size:${marks['fontSize']}`)
  return decls.join(';')
}

// ---------------------------------------------------------------------------
// Drawings (SVG) — VNode port of DrawingView.tsx
// ---------------------------------------------------------------------------

function renderDrawing(doc: Doc, node: Node, path: SourcePath): VNode {
  const width = getShapeAttrNumber(node, 'width', 800)
  const height = getShapeAttrNumber(node, 'height', 600)
  const bg = getShapeAttrString(node, 'bg', '#fff')
  const id = getShapeAttrString(node, 'id', '')
  const svgChildren = node.content
    .map((c, i) => (isNode(c) ? renderDrawingChild(doc, c, [...path, i]) : null))
    .filter((v): v is VNode => v !== null)
  return el(
    'div',
    {
      'class': 'rdt-drawing',
      'data-source-path': stringifyPath(path),
      'data-tag': 'drawing',
      'data-drawing-focus': id,
      'style': `width:${width}px;height:${height}px;background:${bg};display:inline-block`
    },
    [el('svg', { viewBox: `0 0 ${width} ${height}`, width, height }, svgChildren, true)]
  )
}

function renderDrawingChild(doc: Doc, node: Node, path: SourcePath): VNode | null {
  switch (node.tag) {
    case 'layer': return renderLayer(doc, node, path)
    case 'group': return renderGroup(doc, node, path)
    case 'shape': return renderShape(node, path)
    case 'connector': return renderConnector(doc, node, path)
    case 'text-frame': return renderTextFrame(node, path)
    default: return null
  }
}

function renderContainerG(doc: Doc, node: Node, path: SourcePath, attrs: VAttrs): VNode {
  const children = node.content
    .map((c, i) => (isNode(c) ? renderDrawingChild(doc, c, [...path, i]) : null))
    .filter((v): v is VNode => v !== null)
  return el('g', attrs, children, true)
}

function renderLayer(doc: Doc, node: Node, path: SourcePath): VNode | null {
  if (getShapeAttrString(node, 'visible', 'true') === 'false') return null
  return renderContainerG(doc, node, path, {
    'data-source-path': stringifyPath(path),
    'data-tag': 'layer',
    'data-layer': getShapeAttrString(node, 'id', '')
  })
}

function renderGroup(doc: Doc, node: Node, path: SourcePath): VNode {
  return renderContainerG(doc, node, path, {
    'data-source-path': stringifyPath(path),
    'data-tag': 'group'
  })
}

function renderShape(node: Node, path: SourcePath): VNode | null {
  const kind = getShapeKind(node)
  if (kind === null) return null
  const x = getShapeAttrNumber(node, 'x', 0)
  const y = getShapeAttrNumber(node, 'y', 0)
  const w = getShapeAttrNumber(node, 'width', 0)
  const h = getShapeAttrNumber(node, 'height', 0)
  const rotate = getShapeAttrNumber(node, 'rotate', 0)
  const common: VAttrs = {
    'data-source-path': stringifyPath(path),
    'data-tag': 'shape',
    'data-shape-id': getShapeAttrString(node, 'id', ''),
    'fill': getShapeAttrString(node, 'fill', 'transparent'),
    'stroke': getShapeAttrString(node, 'stroke', '#000'),
    'stroke-width': getShapeAttrNumber(node, 'stroke-width', 1),
    'opacity': getShapeAttrNumber(node, 'opacity', 1)
  }
  if (rotate !== 0) common['transform'] = `rotate(${rotate} ${x + w / 2} ${y + h / 2})`

  switch (kind) {
    case 'rect':
      return el('rect', { ...common, x, y, width: w, height: h }, [], true)
    case 'ellipse': {
      const c = rectCenter({ x, y, width: w, height: h } as Rect)
      return el('ellipse', { ...common, cx: c.x, cy: c.y, rx: w / 2, ry: h / 2 }, [], true)
    }
    case 'line':
      return el('line', { ...common, x1: x, y1: y, x2: x + w, y2: y + h, fill: 'none' }, [], true)
    case 'polyline':
      return el('polyline', { ...common, points: getShapeAttrString(node, 'points', ''), fill: 'none' }, [], true)
    case 'polygon':
      return el('polygon', { ...common, points: getShapeAttrString(node, 'points', '') }, [], true)
    case 'path':
    case 'freehand':
      return el('path', { ...common, d: getShapeAttrString(node, 'points', ''), fill: 'none' }, [], true)
    case 'image':
      return el('image', {
        'data-source-path': stringifyPath(path),
        'data-tag': 'shape',
        'data-shape-id': getShapeAttrString(node, 'id', ''),
        x, y, width: w, height: h,
        'href': getShapeAttrString(node, 'src', ''),
        'opacity': common['opacity']
      }, [], true)
    default:
      return null
  }
}

function renderConnector(doc: Doc, node: Node, path: SourcePath): VNode | null {
  const route = computeRoute(node, doc)
  if (route.length < 2) return null
  const routing = getShapeAttrString(node, 'routing', 'orthogonal')
  const d = routing === 'curved' ? routeToCurvedSvgPath(route) : routeToSvgPath(route)
  return el('path', {
    'data-source-path': stringifyPath(path),
    'data-tag': 'connector',
    'data-shape-id': getShapeAttrString(node, 'id', ''),
    d,
    'fill': 'none',
    'stroke': getShapeAttrString(node, 'stroke', '#000'),
    'stroke-width': getShapeAttrNumber(node, 'stroke-width', 1),
    'stroke-dasharray': getShapeAttrString(node, 'stroke-dash', '')
  }, [], true)
}

function renderTextFrame(node: Node, path: SourcePath): VNode {
  const x = getShapeAttrNumber(node, 'x', 0)
  const y = getShapeAttrNumber(node, 'y', 0)
  const w = getShapeAttrNumber(node, 'width', 0)
  const h = getShapeAttrNumber(node, 'height', 0)
  const inner = el(
    'div',
    { 'style': `background:${getShapeAttrString(node, 'bg', 'transparent')};width:100%;height:100%;overflow:auto` },
    node.content.map(c => renderSimpleFlowChild(c))
  )
  return el('foreignObject', {
    'data-source-path': stringifyPath(path),
    'data-tag': 'text-frame',
    'data-shape-id': getShapeAttrString(node, 'id', ''),
    x, y, width: w, height: h
  }, [inner], true)
}

function renderSimpleFlowChild(child: Child): VNode {
  if (child.kind === 'text') return txt(child.text)
  const tag = child.tag === 'doc' ? 'div' : child.tag
  if (tag === 'br') return el('br', {})
  const attrs: VAttrs = {}
  for (const a of child.attrs) attrs[a.name] = a.value as VAttrs[string]
  return el(tag, attrs, child.content.map(c => renderSimpleFlowChild(c)))
}
