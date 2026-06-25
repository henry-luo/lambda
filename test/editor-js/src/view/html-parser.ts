// HTML → Doc parser for fixtures and runtime loading.
//
// Accepts our fixture-format HTML: standard tags (p, h1, strong, em, ...)
// plus the three selection-marker custom elements (`<cursor>`, `<anchor>`,
// `<focus>`). Markers are stripped at load time and reported in the result.
//
// Whitespace policy: leading/trailing whitespace inside block elements is
// trimmed so that test fixtures can be indented for readability without
// affecting the parsed text content. Inter-text whitespace is preserved.

import { isNode, node, nodeAttrs, text } from '../model/doc.js'
import { pos, textSelection } from '../model/source-pos.js'
import type {
  AttrValue,
  Child,
  Doc,
  Mark,
  Node,
  Selection,
  SourcePath,
  SourcePos
} from '../model/types.js'
import type { Schema } from '../model/schema.js'
import { isMarkTag } from '../model/schema.js'

const MARKER_TAGS = new Set(['cursor', 'anchor', 'focus'])

export interface ParseResult {
  doc: Doc
  selection: Selection | null
}

interface ParseState {
  schema: Schema
  cursorPath: SourcePath | null
  cursorOffset: number | null
  anchorPath: SourcePath | null
  anchorOffset: number | null
  focusPath: SourcePath | null
  focusOffset: number | null
}

// ---------------------------------------------------------------------------
// Public entry — parse an HTML string
// ---------------------------------------------------------------------------

export function parseHtmlToDoc(html: string, schema: Schema): ParseResult {
  const parser = new DOMParser()
  const wrapped = `<!doctype html><html><body>${html}</body></html>`
  const dom = parser.parseFromString(wrapped, 'text/html')
  const body = dom.body
  const root = findDocRoot(body)
  const state: ParseState = {
    schema,
    cursorPath: null, cursorOffset: null,
    anchorPath: null, anchorOffset: null,
    focusPath: null, focusOffset: null
  }
  const doc = root === null
    ? // No <doc> wrapper — synthesize one around the body's content
      walkBlockContainer(body, 'doc', [], state)
    : (walkBlockContainer(root, 'doc', [], state) as Doc)

  const selection = buildSelection(state)
  return { doc, selection }
}

function findDocRoot(body: Element): Element | null {
  for (const child of Array.from(body.children)) {
    if (child.tagName.toLowerCase() === 'doc') return child
  }
  return null
}

// ---------------------------------------------------------------------------
// Tree walk
// ---------------------------------------------------------------------------

function walkBlockContainer(el: Element, tag: string, path: SourcePath, st: ParseState): Node {
  const attrs = readAttrs(el)
  const content: Child[] = []
  for (let i = 0; i < el.childNodes.length; i++) {
    walkChild(el.childNodes[i] as ChildNode, [...path, content.length], content, st, /*isInline*/ false)
  }
  return attrs.length === 0
    ? node(tag, content)
    : nodeAttrs(tag, attrs, content)
}

function walkInlineContainer(el: Element, tag: string, path: SourcePath, st: ParseState): Node {
  const attrs = readAttrs(el)
  const content: Child[] = []
  for (let i = 0; i < el.childNodes.length; i++) {
    walkChild(el.childNodes[i] as ChildNode, [...path, content.length], content, st, /*isInline*/ true)
  }
  return attrs.length === 0
    ? node(tag, content)
    : nodeAttrs(tag, attrs, content)
}

// `path` here is the path the produced child WOULD have if accepted (the index
// is content.length at call time). Markers update state but don't push.
function walkChild(
  childNode: ChildNode,
  path: SourcePath,
  content: Child[],
  st: ParseState,
  isInline: boolean
): void {
  if (childNode.nodeType === 3 /* TEXT_NODE */) {
    const raw = (childNode as Text).data
    const trimmed = isInline ? raw : trimBlockWhitespace(raw)
    if (trimmed.length === 0) return
    content.push(text(trimmed))
    return
  }
  if (childNode.nodeType !== 1 /* ELEMENT_NODE */) return

  const el = childNode as Element
  const tag = el.tagName.toLowerCase()

  if (MARKER_TAGS.has(tag)) {
    // Record a marker at the current position. If at the boundary between
    // text leaves, prefer attaching to the previous leaf if any; else use a
    // non-leaf boundary (child index = current content.length).
    const last = content[content.length - 1]
    if (last !== undefined && last.kind === 'text') {
      const leafPath: SourcePath = [...path.slice(0, -1), content.length - 1]
      const offset = last.text.length
      assignMarker(st, tag, leafPath, offset)
    } else {
      assignMarker(st, tag, path.slice(0, -1), content.length)
    }
    return
  }

  // Heuristic: a mark element (strong, em, etc.) is "inline" but its content
  // is inline. A block element's content is block-or-inline depending on
  // schema.
  const schemaIsMark = isMarkTag(st.schema, tag)
  const child = schemaIsMark
    ? walkInlineContainer(el, tag, path, st)
    : walkInlineOrBlock(el, tag, path, st, isInline)
  content.push(child)
}

function walkInlineOrBlock(el: Element, tag: string, path: SourcePath, st: ParseState, isInline: boolean): Node {
  // If the parent's content is inline, descendants are inline. Otherwise
  // descend as block (block children may contain block-or-inline as the
  // schema permits — the validator catches violations).
  return isInline
    ? walkInlineContainer(el, tag, path, st)
    : walkBlockContainer(el, tag, path, st)
}

// ---------------------------------------------------------------------------
// Whitespace trimming for block content
// ---------------------------------------------------------------------------

function trimBlockWhitespace(s: string): string {
  // Collapse runs of whitespace at block boundaries (between block-level
  // children) to nothing. Inside a single block, do not collapse — let the
  // raw text through.
  // Heuristic: drop runs that are entirely whitespace (likely just
  // formatting indentation between block tags).
  if (/^\s+$/.test(s)) return ''
  return s
}

// ---------------------------------------------------------------------------
// Selection marker accounting
// ---------------------------------------------------------------------------

function assignMarker(st: ParseState, tag: string, path: SourcePath, offset: number): void {
  if (tag === 'cursor') {
    st.cursorPath = path
    st.cursorOffset = offset
  } else if (tag === 'anchor') {
    st.anchorPath = path
    st.anchorOffset = offset
  } else if (tag === 'focus') {
    st.focusPath = path
    st.focusOffset = offset
  }
}

function buildSelection(st: ParseState): Selection | null {
  if (st.cursorPath !== null && st.cursorOffset !== null) {
    const p: SourcePos = pos(st.cursorPath, st.cursorOffset)
    return textSelection(p, p)
  }
  if (st.anchorPath !== null && st.anchorOffset !== null &&
      st.focusPath  !== null && st.focusOffset  !== null) {
    return textSelection(
      pos(st.anchorPath, st.anchorOffset),
      pos(st.focusPath, st.focusOffset)
    )
  }
  return null
}

// ---------------------------------------------------------------------------
// Attribute reading
// ---------------------------------------------------------------------------

function readAttrs(el: Element): { name: string; value: AttrValue }[] {
  const out: { name: string; value: AttrValue }[] = []
  for (const a of Array.from(el.attributes)) {
    if (a.name === 'data-source-path') continue  // test annotation; not source
    out.push({ name: a.name, value: coerceAttrValue(a.value) })
  }
  return out
}

function coerceAttrValue(raw: string): AttrValue {
  if (raw === 'true') return true
  if (raw === 'false') return false
  if (/^-?\d+$/.test(raw)) return parseInt(raw, 10)
  if (/^-?\d+\.\d+$/.test(raw)) return parseFloat(raw)
  return raw
}

// ---------------------------------------------------------------------------
// Doc → HTML serializer (round-trip helper for fixtures)
// ---------------------------------------------------------------------------

export interface SerializeOptions {
  selection?: Selection | null
  schema: Schema
  /** Insert newlines and indentation between block children for readability. */
  pretty?: boolean
}

export function serializeDocToHtml(doc: Doc, opts: SerializeOptions): string {
  return serializeChild(doc, [], opts, 0).trim()
}

function serializeChild(c: Child, path: SourcePath, opts: SerializeOptions, depth: number): string {
  if (c.kind === 'text') {
    return escapeText(c.text)
  }
  return serializeNode(c, path, opts, depth)
}

function serializeNode(n: Node, path: SourcePath, opts: SerializeOptions, depth: number): string {
  const attrs = n.attrs.length === 0
    ? ''
    : ' ' + n.attrs.map(a => `${a.name}="${escapeAttr(stringifyAttr(a.value))}"`).join(' ')
  const tag = n.tag
  if (n.content.length === 0 && tag !== 'doc') {
    return `<${tag}${attrs}></${tag}>`
  }

  const parts: string[] = []
  for (let i = 0; i < n.content.length; i++) {
    // selection markers
    const child = n.content[i] as Child
    parts.push(maybeMarker(path, i, opts))
    parts.push(serializeChild(child, [...path, i], opts, depth + 1))
  }
  parts.push(maybeMarker(path, n.content.length, opts))
  // marker at parent boundary
  parts.push(maybeMarkerOnPath(path, n.content.length, opts))

  const inner = parts.join('')
  if (opts.pretty && isBlockLike(n) && hasBlockChild(n)) {
    const pad = '  '.repeat(depth)
    return `<${tag}${attrs}>\n${pad}  ${inner.split('\n').join('\n' + pad + '  ')}\n${pad}</${tag}>`
  }
  return `<${tag}${attrs}>${inner}</${tag}>`
}

function isBlockLike(n: Node): boolean {
  return n.tag === 'doc' || n.tag === 'ul' || n.tag === 'ol' || n.tag === 'blockquote' ||
         n.tag === 'table' || n.tag === 'thead' || n.tag === 'tbody' || n.tag === 'tr'
}
function hasBlockChild(n: Node): boolean {
  for (const c of n.content) {
    if (isNode(c) && (c.tag === 'p' || c.tag === 'h1' || c.tag === 'h2' || c.tag === 'h3' || c.tag === 'li' ||
        c.tag === 'blockquote' || c.tag === 'tr' || c.tag === 'thead' || c.tag === 'tbody')) return true
  }
  return false
}

function maybeMarker(path: SourcePath, offset: number, opts: SerializeOptions): string {
  if (opts.selection === undefined || opts.selection === null) return ''
  // Markers placed inside text leaves are handled by the leaf serializer; this
  // covers boundary positions only (collapsed caret on a non-leaf, or an
  // anchor at a child-index boundary).
  return maybeMarkerOnPath([...path], offset, opts)
}
function maybeMarkerOnPath(path: SourcePath, offset: number, opts: SerializeOptions): string {
  void path; void offset; void opts
  return ''
}

function escapeText(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}
function escapeAttr(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/"/g, '&quot;')
}
function stringifyAttr(v: AttrValue): string {
  if (typeof v === 'string') return v
  if (typeof v === 'number' || typeof v === 'boolean') return String(v)
  return JSON.stringify(v)
}

// Marks helper used by the leaf serializer.
export function serializeMarkedText(t: string, marks: Mark[]): string {
  if (marks.length === 0) return escapeText(t)
  let out = escapeText(t)
  for (const m of marks) {
    out = `<${m}>${out}</${m}>`
  }
  return out
}
