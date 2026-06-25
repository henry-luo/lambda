// Shared data types for the model layer.
//
// Mirrors the Lambda mod_doc.ls / mod_source_pos.ls / mod_step.ls shapes
// exactly. Field names are snake_case so JSON fixtures round-trip identically
// between this TS reference and the future Lambda implementation.

// ---------------------------------------------------------------------------
// Document model
// ---------------------------------------------------------------------------

export type Mark = string

export type AttrValue =
  | string
  | number
  | boolean
  | null
  | AttrValue[]
  | { [k: string]: AttrValue }

export interface Attr {
  name: string
  value: AttrValue
}

export interface TextLeaf {
  kind: 'text'
  text: string
  marks: Mark[]
}

export interface Node {
  kind: 'node'
  tag: string
  attrs: Attr[]
  content: Child[]
}

export type Child = Node | TextLeaf
export type Doc = Node

// ---------------------------------------------------------------------------
// Position model
// ---------------------------------------------------------------------------

export type SourcePath = number[]

export interface SourcePos {
  path: SourcePath
  offset: number
}

export interface TextSelection {
  kind: 'text'
  anchor: SourcePos
  head: SourcePos
}

export interface NodeSelection {
  kind: 'node'
  path: SourcePath
}

export interface AllSelection {
  kind: 'all'
}

export interface MultiNodeSelection {
  kind: 'multi-node'
  paths: SourcePath[]
}

export type Selection = TextSelection | NodeSelection | AllSelection | MultiNodeSelection

// ---------------------------------------------------------------------------
// Step model — seven kinds, discriminated union on `kind`
// ---------------------------------------------------------------------------

export interface ReplaceTextStep {
  kind: 'replace_text'
  path: SourcePath
  from: number
  to: number
  text: string
}

export interface ReplaceStep {
  kind: 'replace'
  parent: SourcePath
  from: number
  to: number
  slice: Child[]
}

export interface ReplaceAroundStep {
  kind: 'replace_around'
  parent: SourcePath
  from: number
  to: number
  gap_from: number
  gap_to: number
  slice: Child[]
  insert: number
}

export interface AddMarkStep {
  kind: 'add_mark'
  path: SourcePath
  mark: Mark
}

export interface RemoveMarkStep {
  kind: 'remove_mark'
  path: SourcePath
  mark: Mark
}

export interface SetAttrStep {
  kind: 'set_attr'
  path: SourcePath
  name: string
  value: AttrValue
}

export interface SetNodeTypeStep {
  kind: 'set_node_type'
  path: SourcePath
  tag: string
}

export type Step =
  | ReplaceTextStep
  | ReplaceStep
  | ReplaceAroundStep
  | AddMarkStep
  | RemoveMarkStep
  | SetAttrStep
  | SetNodeTypeStep

// ---------------------------------------------------------------------------
// Transaction & history
// ---------------------------------------------------------------------------

export interface MetaEntry {
  name: string
  value: AttrValue
}

export interface Transaction {
  doc_before: Doc
  doc_after: Doc
  steps: Step[]
  sel_before: Selection | null
  sel_after: Selection | null
  meta: MetaEntry[]
}

export interface History {
  undo: Transaction[]
  redo: Transaction[]
  undo_groups: (string | null)[]
  redo_groups: (string | null)[]
}

// ---------------------------------------------------------------------------
// Resolved position — the result of walking a path against a tree
// ---------------------------------------------------------------------------

export interface ResolvedAncestor {
  path: SourcePath
  node: Child | null
  index: number
}

export interface ResolvedPos {
  node: Child | null
  parent: Child | null
  parent_index: number
  depth: number
  ancestors: ResolvedAncestor[]
  found: boolean
}
