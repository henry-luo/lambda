// Plain-DOM view-structure (Stage 4B).
//
// `renderDoc` (render-vnode.ts) produces a tree of these VNodes — a pure
// `doc → view-structure` description with no DOM/React dependency. The keyed
// reconciler (reconcile.ts) commits a VNode tree to real DOM. This is the
// framework-free replacement for the React `renderDoc(): ReactElement` path,
// and the JS-side analogue of the Lambda `render_map` template result.

export type VAttrValue = string | number | boolean | null | undefined

export type VAttrs = Record<string, VAttrValue>

export interface VEl {
  kind: 'el'
  tag: string
  // svg-namespace flag — set on <svg> and inherited by descendants until a
  // <foreignObject> switches back to the HTML namespace.
  ns: boolean
  // reconciliation key (the `data-source-path` string); null for unkeyed
  // structural children such as the placeholder <br> or the empty-leaf text.
  key: string | null
  attrs: VAttrs
  children: VNode[]
}

export interface VText {
  kind: 'text'
  text: string
}

export type VNode = VEl | VText

const SVG_TAGS = new Set([
  'svg', 'g', 'rect', 'ellipse', 'circle', 'line', 'polyline', 'polygon',
  'path', 'image', 'text', 'foreignObject'
])

// element constructor. The svg namespace is derived from the tag (svg subtree)
// or inherited via `svgCtx`; <foreignObject> children are HTML again.
export function el(
  tag: string,
  attrs: VAttrs,
  children: VNode[] = [],
  svgCtx = false
): VEl {
  const ns = tag !== 'foreignObject' && (svgCtx || SVG_TAGS.has(tag))
  const key = typeof attrs['data-source-path'] === 'string'
    ? (attrs['data-source-path'] as string)
    : null
  return { kind: 'el', tag, ns, key, attrs, children }
}

export function txt(text: string): VText {
  return { kind: 'text', text }
}

export function isVText(v: VNode): v is VText {
  return v.kind === 'text'
}
