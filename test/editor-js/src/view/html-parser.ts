// HTML → Doc parser for fixtures and runtime loading.
//
// Accepts our fixture-format HTML: standard tags (p, h1, strong, em, ...)
// plus the three selection-marker custom elements (`<cursor>`, `<anchor>`,
// `<focus>`). Markers are stripped at load time and reported in the result.
//
// Whitespace policy: leading/trailing whitespace inside block elements is
// trimmed so that test fixtures can be indented for readability without
// affecting the parsed text content. Inter-text whitespace is preserved.

import { isNode, isText, node, nodeAttrs } from '../model/doc.js'
import { pos, textSelection } from '../model/source-pos.js'
import type {
  AttrValue,
  Child,
  Doc,
  MarkDict,
  Node,
  Selection,
  SourcePath,
  SourcePos,
  TextLeaf
} from '../model/types.js'
import type { Schema } from '../model/schema.js'
import { isMarkTag } from '../model/schema.js'
import { marksEqual } from '../model/step.js'

const MARKER_TAGS = new Set(['cursor', 'anchor', 'focus'])

// Tags treated as inline marks per Inline_Formatting §6.1. These elements are
// flattened during parse: the wrapping element disappears and the mark gets
// pushed down into every descendant text leaf's marks dict.
const MARK_TAGS = new Set(['strong', 'b', 'em', 'i', 'u', 's', 'del', 'sub', 'sup', 'code', 'a', 'span'])

function tagToMarks(el: Element): MarkDict {
  const tag = el.tagName.toLowerCase()
  const out: MarkDict = {}
  switch (tag) {
    case 'strong': case 'b':   out['bold']          = true; break
    case 'em':     case 'i':   out['italic']        = true; break
    case 'u':                  out['underline']     = true; break
    case 's':      case 'del': out['strikethrough'] = true; break
    case 'sub':                out['subscript']     = true; break
    case 'sup':                out['superscript']   = true; break
    case 'code':               out['code']          = true; break
    case 'a': {
      const href = el.getAttribute('href') ?? ''
      out['link'] = href
      break
    }
    // 'span' contributes only via its style attribute (handled below)
  }
  const styleAttr = el.getAttribute('style')
  if (styleAttr !== null && styleAttr.length > 0) {
    Object.assign(out, parseStyleAttr(styleAttr))
  }
  return out
}

function parseStyleAttr(style: string): MarkDict {
  const out: MarkDict = {}
  for (const decl of style.split(';')) {
    const colon = decl.indexOf(':')
    if (colon < 0) continue
    const prop = decl.slice(0, colon).trim().toLowerCase()
    const val  = decl.slice(colon + 1).trim()
    if (val.length === 0) continue
    if (prop === 'font-weight') {
      const num = parseInt(val, 10)
      if (val === 'bold' || val === 'bolder' || (Number.isFinite(num) && num >= 600)) out['bold'] = true
    }
    else if (prop === 'font-style' && val === 'italic') out['italic'] = true
    else if (prop === 'text-decoration' || prop === 'text-decoration-line') {
      if (/underline/.test(val))    out['underline']     = true
      if (/line-through/.test(val)) out['strikethrough'] = true
    }
    else if (prop === 'color')            out['color']      = val
    else if (prop === 'background-color') out['background'] = val
    else if (prop === 'font-family')      out['fontFamily'] = val
    else if (prop === 'font-size')        out['fontSize']   = val
  }
  return out
}

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

// Parse an HTML string into a container Element whose children are the parsed
// nodes. Uses DOMParser where available (browsers, jsdom) so behaviour is
// unchanged for every existing test; falls back to the `innerHTML` setter
// (which Radiant's LambdaJS DOM supports, running its HTML5 fragment parser)
// when DOMParser is absent — i.e. under Radiant. Keeps the DOM surface minimal
// (Stage 4B §5.3): no new host API beyond the already-supported innerHTML.
function htmlToBody(html: string): Element {
  if (typeof DOMParser !== 'undefined') {
    const dom = new DOMParser().parseFromString(`<!doctype html><html><body>${html}</body></html>`, 'text/html')
    return dom.body
  }
  const host = document.createElement('div')
  host.innerHTML = html
  return host
}

export function parseHtmlToDoc(html: string, schema: Schema): ParseResult {
  const body = htmlToBody(html)
  const root = findDocRoot(body)
  const state: ParseState = {
    schema,
    cursorPath: null, cursorOffset: null,
    anchorPath: null, anchorOffset: null,
    focusPath: null, focusOffset: null
  }
  const rawDoc = root === null
    ? walkBlockContainer(body, 'doc', [], state)
    : (walkBlockContainer(root, 'doc', [], state) as Doc)

  // Slate-style normalization: merge adjacent text leaves that share marks.
  // Marker positions recorded against pre-merge leaves are translated to
  // their post-merge equivalents.
  const markers = collectMarkers(state)
  const normalized = normalizeNode(rawDoc, [], markers)
  // Then push markers off non-leaf boundaries into adjacent text leaves so
  // that `<p><cursor></cursor>text</p>` resolves to path=[…,0],offset=0
  // rather than path=[…],offset=0 (matches command output semantics).
  preferLeafAnchor(normalized, markers)
  applyMarkers(state, markers)

  const selection = buildSelection(state)
  return { doc: normalized, selection }
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
    walkChild(el.childNodes[i] as ChildNode, [...path, content.length], content, st, /*isInline*/ false, /*markCtx*/ {})
  }
  return attrs.length === 0
    ? node(tag, content)
    : nodeAttrs(tag, attrs, content)
}

function walkInlineContainer(el: Element, tag: string, path: SourcePath, st: ParseState, markCtx: MarkDict): Node {
  const attrs = readAttrs(el)
  const content: Child[] = []
  for (let i = 0; i < el.childNodes.length; i++) {
    walkChild(el.childNodes[i] as ChildNode, [...path, content.length], content, st, /*isInline*/ true, markCtx)
  }
  return attrs.length === 0
    ? node(tag, content)
    : nodeAttrs(tag, attrs, content)
}

// `path` is the path the produced child WOULD have if accepted (the index is
// content.length at call time). Markers update state but don't push. Mark
// elements (strong, em, span style, …) are FLATTENED: the wrapping element
// disappears, its mark contribution is pushed into descendant text leaves.
function walkChild(
  childNode: ChildNode,
  path: SourcePath,
  content: Child[],
  st: ParseState,
  isInline: boolean,
  markCtx: MarkDict
): void {
  if (childNode.nodeType === 3 /* TEXT_NODE */) {
    const raw = (childNode as Text).data
    const trimmed = isInline ? raw : trimBlockWhitespace(raw)
    if (trimmed.length === 0) return
    content.push({ kind: 'text', text: trimmed, marks: { ...markCtx } })
    return
  }
  if (childNode.nodeType !== 1 /* ELEMENT_NODE */) return

  const el = childNode as Element
  const tag = el.tagName.toLowerCase()

  if (MARKER_TAGS.has(tag)) {
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

  // Flatten the implicit <tbody> that browsers' HTML parser inserts into
  // tables. Our model keeps tables as `table > tr` (matching Lambda); the
  // <tbody>'s rows are lifted directly into the parent table.
  if (tag === 'tbody') {
    for (let i = 0; i < el.childNodes.length; i++) {
      walkChild(el.childNodes[i] as ChildNode, [...path.slice(0, -1), content.length], content, st, false, markCtx)
    }
    return
  }

  // Flatten mark elements: recurse into children with the mark merged into
  // `markCtx`, pushing produced text leaves directly into the OUTER content.
  if (MARK_TAGS.has(tag)) {
    const merged = { ...markCtx, ...tagToMarks(el) }
    for (let i = 0; i < el.childNodes.length; i++) {
      walkChild(
        el.childNodes[i] as ChildNode,
        [...path.slice(0, -1), content.length],
        content,
        st,
        /*isInline*/ true,
        merged
      )
    }
    return
  }

  // Non-mark element — recurse as block or inline container per parent's
  // role. The schema check on isMarkTag continues to handle any unknown
  // mark-role tags declared in user schemas.
  const schemaIsMark = isMarkTag(st.schema, tag)
  if (schemaIsMark) {
    const merged = { ...markCtx, ...tagToMarks(el) }
    for (let i = 0; i < el.childNodes.length; i++) {
      walkChild(
        el.childNodes[i] as ChildNode,
        [...path.slice(0, -1), content.length],
        content,
        st,
        /*isInline*/ true,
        merged
      )
    }
    return
  }

  const child = isInline
    ? walkInlineContainer(el, tag, path, st, markCtx)
    : walkBlockContainer(el, tag, path, st)
  content.push(child)
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
// Normalization — merge adjacent same-mark text leaves; translate markers
// ---------------------------------------------------------------------------

interface MutableMarker {
  kind: 'cursor' | 'anchor' | 'focus'
  path: SourcePath
  offset: number
}

function collectMarkers(st: ParseState): MutableMarker[] {
  const out: MutableMarker[] = []
  if (st.cursorPath !== null && st.cursorOffset !== null)
    out.push({ kind: 'cursor', path: st.cursorPath, offset: st.cursorOffset })
  if (st.anchorPath !== null && st.anchorOffset !== null)
    out.push({ kind: 'anchor', path: st.anchorPath, offset: st.anchorOffset })
  if (st.focusPath !== null && st.focusOffset !== null)
    out.push({ kind: 'focus', path: st.focusPath, offset: st.focusOffset })
  return out
}

function applyMarkers(st: ParseState, markers: MutableMarker[]): void {
  for (const m of markers) {
    if (m.kind === 'cursor') { st.cursorPath = m.path; st.cursorOffset = m.offset }
    if (m.kind === 'anchor') { st.anchorPath = m.path; st.anchorOffset = m.offset }
    if (m.kind === 'focus')  { st.focusPath  = m.path; st.focusOffset  = m.offset }
  }
}

// `marksEqual` is imported from `model/step.js`.

// Walk down, recursing first, then merging adjacent text runs in this node's
// content and translating marker paths/offsets through the merge.
function normalizeNode(n: Node, path: SourcePath, markers: MutableMarker[]): Node {
  // Recurse into block children first.
  const recursed: Child[] = []
  for (let i = 0; i < n.content.length; i++) {
    const c = n.content[i] as Child
    if (isNode(c)) {
      recursed.push(normalizeNode(c, [...path, i], markers))
    } else {
      recursed.push(c)
    }
  }

  // Merge adjacent same-mark text leaves.
  const merged: Child[] = []
  // idxMap[oldChildIdx] = { newIdx, charShift }
  const idxMap: { newIdx: number; charShift: number }[] = []
  let i = 0
  while (i < recursed.length) {
    const c = recursed[i] as Child
    if (!isText(c)) {
      idxMap.push({ newIdx: merged.length, charShift: 0 })
      merged.push(c)
      i++
      continue
    }
    const runMarks = c.marks
    let buf = c.text
    const startNewIdx = merged.length
    idxMap.push({ newIdx: startNewIdx, charShift: 0 })
    let j = i + 1
    while (j < recursed.length) {
      const cj = recursed[j] as Child
      if (!isText(cj) || !marksEqual(cj.marks, runMarks)) break
      idxMap.push({ newIdx: startNewIdx, charShift: buf.length })
      buf += cj.text
      j++
    }
    merged.push({ kind: 'text', text: buf, marks: runMarks } as TextLeaf)
    i = j
  }

  // Translate markers whose path passes through this container.
  for (const m of markers) {
    if (m.path.length <= path.length) continue
    let prefixMatches = true
    for (let k = 0; k < path.length; k++) {
      if (m.path[k] !== path[k]) { prefixMatches = false; break }
    }
    if (!prefixMatches) continue
    const childIdx = m.path[path.length] as number
    if (childIdx < 0 || childIdx >= idxMap.length) continue
    const mapping = idxMap[childIdx] as { newIdx: number; charShift: number }
    m.path = [...path, mapping.newIdx, ...m.path.slice(path.length + 1)]
    // If the marker lands directly on this container's text leaf, shift the
    // offset by the position the old leaf occupies inside the merged run.
    if (m.path.length === path.length + 1) {
      m.offset += mapping.charShift
    }
  }

  return { kind: 'node', tag: n.tag, attrs: n.attrs, content: merged }
}

// Slide markers anchored at non-leaf boundaries into adjacent text leaves
// where applicable. This normalizes selection representation so that
// `<p><cursor></cursor>text</p>` and `<p>text<cursor></cursor></p>` both end
// up with a path that points INTO a text leaf — matching what commands
// produce when they set the post-edit caret.
function preferLeafAnchor(doc: Node, markers: MutableMarker[]): void {
  for (const m of markers) {
    const parent = nodeAtMutable(doc, m.path)
    if (parent === null || !isNode(parent)) continue
    const idx = m.offset
    // Marker pointing inside an existing text leaf at child index `idx`
    if (idx < parent.content.length) {
      const next = parent.content[idx] as Child
      if (isText(next)) {
        m.path = [...m.path, idx]
        m.offset = 0
        continue
      }
    }
    if (idx > 0) {
      const prev = parent.content[idx - 1] as Child
      if (isText(prev)) {
        m.path = [...m.path, idx - 1]
        m.offset = prev.text.length
        continue
      }
    }
    // else: leave the marker at the non-leaf boundary
  }
}

function nodeAtMutable(doc: Node, path: SourcePath): Node | null {
  let cur: Child = doc
  for (let i = 0; i < path.length; i++) {
    if (!isNode(cur)) return null
    const idx = path[i] as number
    if (idx >= cur.content.length) return null
    cur = cur.content[idx] as Child
  }
  return isNode(cur) ? cur : null
}

// ---------------------------------------------------------------------------
// Attribute reading
// ---------------------------------------------------------------------------

function readAttrs(el: Element): { name: string; value: AttrValue }[] {
  const out: { name: string; value: AttrValue }[] = []
  // Enumerate via getAttributeNames() — supported in browsers, jsdom, and
  // Radiant's LambdaJS DOM (Stage 4B). The `.attributes` NamedNodeMap is not
  // available under Radiant, so it is only a fallback for exotic hosts. (We
  // call directly rather than feature-detect with typeof: under Radiant a DOM
  // method read returns `true`, not a function, so typeof would misfire.)
  let names: string[]
  try {
    names = el.getAttributeNames()
  } catch {
    names = Array.from(el.attributes).map(a => a.name)
  }
  for (const name of names) {
    if (name === 'data-source-path') continue  // test annotation; not source
    out.push({ name, value: coerceAttrValue(el.getAttribute(name) ?? '') })
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

// HTML void elements — serialized without a closing tag (`<br>` not `<br></br>`,
// which the HTML5 parser would read as two <br>s).
const VOID_SERIALIZE_TAGS = new Set(['br', 'hr', 'img'])

function serializeChild(c: Child, path: SourcePath, opts: SerializeOptions, depth: number): string {
  if (c.kind === 'text') {
    return serializeMarkedText(c.text, c.marks)
  }
  return serializeNode(c, path, opts, depth)
}

function serializeNode(n: Node, path: SourcePath, opts: SerializeOptions, depth: number): string {
  const attrs = n.attrs.length === 0
    ? ''
    : ' ' + n.attrs.map(a => `${a.name}="${escapeAttr(stringifyAttr(a.value))}"`).join(' ')
  const tag = n.tag
  if (VOID_SERIALIZE_TAGS.has(tag)) {
    return `<${tag}${attrs}>`
  }
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

// Marks helper used by the leaf serializer (fixture-mode output per
// Inline_Formatting §7.1). Emits a single non-nested wrapper.
export function serializeMarkedText(t: string, marks: MarkDict): string {
  if (Object.keys(marks).length === 0) return escapeText(t)
  const style: string[] = []
  if (marks['bold']           === true) style.push('font-weight: bold')
  if (marks['italic']         === true) style.push('font-style: italic')
  if (marks['underline']      === true && marks['strikethrough'] === true) style.push('text-decoration: underline line-through')
  else if (marks['underline'] === true) style.push('text-decoration: underline')
  else if (marks['strikethrough'] === true) style.push('text-decoration: line-through')
  if (typeof marks['color']        === 'string') style.push(`color: ${marks['color']}`)
  if (typeof marks['background']   === 'string') style.push(`background-color: ${marks['background']}`)
  if (typeof marks['fontFamily']   === 'string') style.push(`font-family: ${marks['fontFamily']}`)
  if (typeof marks['fontSize']     === 'string') style.push(`font-size: ${marks['fontSize']}`)
  const styleAttr = style.length > 0 ? ` style="${escapeAttr(style.join('; '))}"` : ''
  const inner = escapeText(t)
  if ('link' in marks && typeof marks['link'] === 'string') return `<a href="${escapeAttr(marks['link'])}"${styleAttr}>${inner}</a>`
  if (marks['code'] === true) return `<code${styleAttr}>${inner}</code>`
  return `<span${styleAttr}>${inner}</span>`
}
