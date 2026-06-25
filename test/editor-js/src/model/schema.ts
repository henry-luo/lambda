// Port of the schema layer of lambda/package/editor/mod_md_schema.ls
//
// A Schema is a map  {tagName -> SchemaEntry}  describing per-tag content
// rules. Validation walks the doc tree and asks: do the children of each
// node satisfy that node's content expression (the array of ContentTerm)?
//
// Roles:
//   'block' | 'inline' | 'mark' | 'leaf' | 'text' | 'drawing-container' | 'drawing-object'
//
// Content expression = ContentTerm[]; each term has a quantifier:
//   'one' | 'opt' | 'plus' | 'star'
//
// A term references either a role, a tag, or an alternation (any).
// The matcher is greedy left-to-right — sufficient for every pattern in the
// shipped schemas (no overlapping star-then-tag patterns).

import { isNode, isText } from './doc.js'
import type { AttrValue, Child, Doc, Node } from './types.js'

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type Role =
  | 'block'
  | 'inline'
  | 'mark'
  | 'leaf'
  | 'text'
  | 'drawing-container'
  | 'drawing-object'

export type Quantifier = 'one' | 'opt' | 'plus' | 'star'

export type ContentTerm =
  | { role: Role, qty: Quantifier }
  | { tag: string, qty: Quantifier }
  | { any: ContentTerm[], qty: Quantifier }

export type MarksPolicy = 'all' | 'none'

export type AttrType = 'string' | 'int' | 'float' | 'bool' | 'symbol' | 'array' | 'map'

export interface AttrSpec {
  name: string
  required?: boolean
  type?: AttrType
  default?: AttrValue
  validate?: (v: AttrValue) => boolean
}

export interface SchemaEntry {
  role: Role
  content: ContentTerm[]
  marks: MarksPolicy
  attrs?: AttrSpec[]
  excludes?: 'all' | null
  selectable?: boolean
  editable?: boolean
  draggable?: boolean
  atomic?: boolean
}

export interface Schema {
  entries: Record<string, SchemaEntry>
  default_block: string  // tag used when splitting where current block can't repeat
}

export interface ValidationError {
  path: number[]
  message: string
}

export interface ValidationResult {
  valid: boolean
  errors: ValidationError[]
}

// ---------------------------------------------------------------------------
// Role compatibility
// ---------------------------------------------------------------------------

// True iff a child of role `actual` satisfies a content term requiring `required`.
export function roleSatisfies(actual: Role, required: Role): boolean {
  if (actual === required) return true
  if (required === 'inline') return actual === 'text' || actual === 'mark' || actual === 'inline'
  if (required === 'drawing-object') {
    return actual === 'drawing-object' || actual === 'drawing-container'
  }
  return false
}

// ---------------------------------------------------------------------------
// Child → role/tag
// ---------------------------------------------------------------------------

export function childRole(c: Child, schema: Schema): Role {
  if (isText(c)) return 'text'
  const e = schema.entries[c.tag]
  if (e === undefined) return 'inline'  // unknown tags default to inline
  return e.role
}

export function childTag(c: Child): string | null {
  return isNode(c) ? c.tag : null
}

// ---------------------------------------------------------------------------
// Term matcher
// ---------------------------------------------------------------------------

function termMatches(c: Child, term: ContentTerm, schema: Schema): boolean {
  if ('role' in term) return roleSatisfies(childRole(c, schema), term.role)
  if ('tag' in term)  return childTag(c) === term.tag
  // any: matches if any alternative matches (qty in alternatives is ignored)
  for (const alt of term.any) {
    if (termMatches(c, { ...alt, qty: 'one' }, schema)) return true
  }
  return false
}

// Greedy left-to-right matcher. Returns the index after the last consumed
// child on success, or -1 on failure.
function matchTerms(children: Child[], terms: ContentTerm[], schema: Schema): number {
  let ci = 0
  for (const term of terms) {
    let count = 0
    while (ci < children.length && termMatches(children[ci] as Child, term, schema)) {
      count++
      ci++
    }
    if (term.qty === 'one'  && count !== 1) return -1
    if (term.qty === 'opt'  && count > 1)   return -1   // greedy can match >1 if same-shape repeated; flag
    if (term.qty === 'plus' && count < 1)   return -1
    // 'star': any count is fine
  }
  return ci
}

// ---------------------------------------------------------------------------
// Attr validation
// ---------------------------------------------------------------------------

function validateAttrValue(spec: AttrSpec, value: AttrValue | null): string | null {
  if (value === null || value === undefined) {
    if (spec.required) return `required attribute "${spec.name}" missing`
    return null
  }
  if (spec.type !== undefined) {
    const ok = (
      (spec.type === 'string' && typeof value === 'string') ||
      (spec.type === 'int'    && typeof value === 'number' && Number.isInteger(value)) ||
      (spec.type === 'float'  && typeof value === 'number') ||
      (spec.type === 'bool'   && typeof value === 'boolean') ||
      (spec.type === 'symbol' && typeof value === 'string') ||
      (spec.type === 'array'  && Array.isArray(value)) ||
      (spec.type === 'map'    && typeof value === 'object' && value !== null && !Array.isArray(value))
    )
    if (!ok) return `attribute "${spec.name}" expected type ${spec.type}, got ${typeof value}`
  }
  if (spec.validate !== undefined && !spec.validate(value)) {
    return `attribute "${spec.name}" failed custom validation`
  }
  return null
}

function validateAttrs(entry: SchemaEntry, node_: Node, path: number[], errors: ValidationError[]): void {
  if (!entry.attrs) return
  for (const spec of entry.attrs) {
    const found = node_.attrs.find(a => a.name === spec.name)
    const err = validateAttrValue(spec, found === undefined ? null : found.value)
    if (err !== null) errors.push({ path, message: err })
  }
}

// ---------------------------------------------------------------------------
// Recursive validator
// ---------------------------------------------------------------------------

function validateChild(c: Child, schema: Schema, path: number[], errors: ValidationError[]): void {
  if (isText(c)) return  // text leaves have no schema entry; marks are tracked but not validated structurally
  const entry = schema.entries[c.tag]
  if (entry === undefined) {
    errors.push({ path, message: `unknown tag "${c.tag}"` })
    return
  }
  validateAttrs(entry, c, path, errors)
  const consumed = matchTerms(c.content, entry.content, schema)
  if (consumed !== c.content.length) {
    errors.push({
      path,
      message: consumed < 0
        ? `content of <${c.tag}> does not satisfy schema`
        : `extra children in <${c.tag}> after position ${consumed}`
    })
  }
  // Marks-on-children policy
  if (entry.marks === 'none') {
    for (let i = 0; i < c.content.length; i++) {
      const child = c.content[i] as Child
      if (isText(child) && Object.keys(child.marks).length > 0) {
        errors.push({ path: [...path, i], message: `marks not allowed in <${c.tag}>` })
      }
    }
  }
  for (let i = 0; i < c.content.length; i++) {
    validateChild(c.content[i] as Child, schema, [...path, i], errors)
  }
}

export function validateDoc(doc: Doc, schema: Schema): ValidationResult {
  const errors: ValidationError[] = []
  validateChild(doc, schema, [], errors)
  return { valid: errors.length === 0, errors }
}

// ---------------------------------------------------------------------------
// Schema queries used by commands
// ---------------------------------------------------------------------------

export function schemaEntry(schema: Schema, tag: string): SchemaEntry | undefined {
  return schema.entries[tag]
}

export function isBlockTag(schema: Schema, tag: string): boolean {
  const e = schemaEntry(schema, tag)
  return e !== undefined && e.role === 'block'
}

export function isInlineTag(schema: Schema, tag: string): boolean {
  const e = schemaEntry(schema, tag)
  return e !== undefined && (e.role === 'inline' || e.role === 'mark')
}

export function isMarkTag(schema: Schema, tag: string): boolean {
  const e = schemaEntry(schema, tag)
  return e !== undefined && e.role === 'mark'
}

// Container's default block to materialize when splitting at a position whose
// schema does not allow the current block to repeat (Enter at end of <h1> →
// <p>, not another <h1>).
export function defaultSplitBlock(schema: Schema, currentTag: string): string {
  void currentTag
  return schema.default_block
}
