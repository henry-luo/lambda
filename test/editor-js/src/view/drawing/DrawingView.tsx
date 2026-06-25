// SVG rendering for the <drawing> block and its descendants.
//
// Entry point: <DrawingView node={drawingNode} path={pathToDoc} />
// Children are rendered via DrawingChild which dispatches on tag.

import { isNode, attrsGet } from '../../model/doc.js'
import { computeRoute, routeToCurvedSvgPath, routeToSvgPath } from '../../drawing/router.js'
import { getShapeAttrNumber, getShapeAttrString, getShapeKind } from '../../drawing/shape-utils.js'
import { rectCenter, type Rect } from '../../drawing/geom.js'
import type { Doc, Node, SourcePath } from '../../model/types.js'
import { stringifyPath } from '../dom-bridge.js'
import { createElement } from 'react'

export interface DrawingViewProps {
  doc: Doc
  node: Node
  path: SourcePath
}

export function DrawingView({ doc, node, path }: DrawingViewProps) {
  const width  = getShapeAttrNumber(node, 'width', 800)
  const height = getShapeAttrNumber(node, 'height', 600)
  const bg     = getShapeAttrString(node, 'bg', '#fff')
  const id     = getShapeAttrString(node, 'id', '')
  return (
    <div
      className="rdt-drawing"
      data-source-path={stringifyPath(path)}
      data-tag="drawing"
      data-drawing-focus={id}
      style={{ width, height, background: bg, display: 'inline-block' }}
    >
      <svg viewBox={`0 0 ${width} ${height}`} width={width} height={height}>
        {node.content.map((c, i) =>
          isNode(c) ? (
            <DrawingChild key={i} doc={doc} node={c} path={[...path, i]} />
          ) : null
        )}
      </svg>
    </div>
  )
}

interface DrawingChildProps {
  doc: Doc
  node: Node
  path: SourcePath
}

function DrawingChild({ doc, node, path }: DrawingChildProps) {
  switch (node.tag) {
    case 'layer':      return <LayerView doc={doc} node={node} path={path} />
    case 'group':      return <GroupView doc={doc} node={node} path={path} />
    case 'shape':      return <ShapeView node={node} path={path} />
    case 'connector':  return <ConnectorView doc={doc} node={node} path={path} />
    case 'text-frame': return <TextFrameView node={node} path={path} />
    default:           return null
  }
}

function LayerView({ doc, node, path }: DrawingChildProps) {
  const id = getShapeAttrString(node, 'id', '')
  const visible = getShapeAttrString(node, 'visible', 'true')
  if (visible === 'false') return null
  return (
    <g data-source-path={stringifyPath(path)} data-tag="layer" data-layer={id}>
      {node.content.map((c, i) =>
        isNode(c) ? (
          <DrawingChild key={i} doc={doc} node={c} path={[...path, i]} />
        ) : null
      )}
    </g>
  )
}

function GroupView({ doc, node, path }: DrawingChildProps) {
  return (
    <g data-source-path={stringifyPath(path)} data-tag="group">
      {node.content.map((c, i) =>
        isNode(c) ? (
          <DrawingChild key={i} doc={doc} node={c} path={[...path, i]} />
        ) : null
      )}
    </g>
  )
}

// ---------------------------------------------------------------------------
// Shape — dispatches on kind
// ---------------------------------------------------------------------------

interface ShapeViewProps { node: Node; path: SourcePath }

function ShapeView({ node, path }: ShapeViewProps) {
  const kind = getShapeKind(node)
  if (kind === null) return null

  const common = {
    'data-source-path': stringifyPath(path),
    'data-tag': 'shape',
    'data-shape-id': getShapeAttrString(node, 'id', ''),
    fill: getShapeAttrString(node, 'fill', 'transparent'),
    stroke: getShapeAttrString(node, 'stroke', '#000'),
    strokeWidth: getShapeAttrNumber(node, 'stroke-width', 1),
    opacity: getShapeAttrNumber(node, 'opacity', 1)
  }
  const rotate = getShapeAttrNumber(node, 'rotate', 0)
  const x = getShapeAttrNumber(node, 'x', 0)
  const y = getShapeAttrNumber(node, 'y', 0)
  const w = getShapeAttrNumber(node, 'width', 0)
  const h = getShapeAttrNumber(node, 'height', 0)
  const transform = rotate !== 0
    ? `rotate(${rotate} ${x + w / 2} ${y + h / 2})`
    : undefined

  switch (kind) {
    case 'rect':
      return <rect {...common} x={x} y={y} width={w} height={h} transform={transform} />
    case 'ellipse': {
      const c = rectCenter({ x, y, width: w, height: h } as Rect)
      return <ellipse {...common} cx={c.x} cy={c.y} rx={w / 2} ry={h / 2} transform={transform} />
    }
    case 'line':
      return <line {...common} x1={x} y1={y} x2={x + w} y2={y + h} transform={transform} fill="none" />
    case 'polyline':
      return <polyline {...common} points={getShapeAttrString(node, 'points', '')} transform={transform} fill="none" />
    case 'polygon':
      return <polygon {...common} points={getShapeAttrString(node, 'points', '')} transform={transform} />
    case 'path':
    case 'freehand':
      return <path {...common} d={getShapeAttrString(node, 'points', '')} transform={transform} fill="none" />
    case 'image':
      return <image
                data-source-path={stringifyPath(path)}
                data-tag="shape"
                data-shape-id={getShapeAttrString(node, 'id', '')}
                x={x} y={y} width={w} height={h}
                href={getShapeAttrString(node, 'src', '')}
                opacity={common.opacity}
                transform={transform} />
    default:
      return null
  }
}

// ---------------------------------------------------------------------------
// Connector
// ---------------------------------------------------------------------------

interface ConnectorViewProps { doc: Doc; node: Node; path: SourcePath }

function ConnectorView({ doc, node, path }: ConnectorViewProps) {
  const route = computeRoute(node, doc)
  if (route.length < 2) return null
  const routing = getShapeAttrString(node, 'routing', 'orthogonal')
  const d = routing === 'curved' ? routeToCurvedSvgPath(route) : routeToSvgPath(route)
  return (
    <path
      data-source-path={stringifyPath(path)}
      data-tag="connector"
      data-shape-id={getShapeAttrString(node, 'id', '')}
      d={d}
      fill="none"
      stroke={getShapeAttrString(node, 'stroke', '#000')}
      strokeWidth={getShapeAttrNumber(node, 'stroke-width', 1)}
      strokeDasharray={getShapeAttrString(node, 'stroke-dash', '')}
    />
  )
}

// ---------------------------------------------------------------------------
// Text frame (foreignObject hosting flow-doc content)
// ---------------------------------------------------------------------------

function TextFrameView({ node, path }: { node: Node; path: SourcePath }) {
  const x = getShapeAttrNumber(node, 'x', 0)
  const y = getShapeAttrNumber(node, 'y', 0)
  const w = getShapeAttrNumber(node, 'width', 0)
  const h = getShapeAttrNumber(node, 'height', 0)
  return (
    <foreignObject
      data-source-path={stringifyPath(path)}
      data-tag="text-frame"
      data-shape-id={getShapeAttrString(node, 'id', '')}
      x={x} y={y} width={w} height={h}
    >
      <div
        // Stage-4 §8.1: text-frame content is the flow doc schema; rendered
        // via the standard renderDoc path. Inlined here to avoid a circular
        // import from `render.tsx` — the simplified version below covers the
        // baseline test fixtures (no nested drawings inside frames).
        style={{ background: getShapeAttrString(node, 'bg', 'transparent'), width: '100%', height: '100%', overflow: 'auto' }}
      >
        {node.content.map((c, i) => (
          <SimpleFlowChild key={i} child={c} />
        ))}
      </div>
    </foreignObject>
  )
}

function SimpleFlowChild({ child }: { child: import('../../model/types.js').Child }) {
  if (child.kind === 'text') return <>{child.text}</>
  if (!isNode(child)) return null
  const tag = child.tag === 'doc' ? 'div' : child.tag
  const attrs: Record<string, unknown> = {}
  for (const a of child.attrs) attrs[a.name] = a.value
  if (tag === 'br') return <br />
  const inner = child.content.map((c, i) => <SimpleFlowChild key={i} child={c} />)
  return createElement(tag, attrs, inner)
}

// Helper for callers that need to read the drawing-level attr in tests
export function readDrawingAttr(n: Node, name: string): import('../../model/types.js').AttrValue | null {
  return attrsGet(n.attrs, name)
}
