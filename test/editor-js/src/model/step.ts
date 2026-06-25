// Port of lambda/package/editor/mod_step.ls
//
// Seven typed, invertible Steps. Every step supports:
//
//   stepApply(step, doc)    -> new_doc      (immutable; doc is unchanged)
//   stepInvert(step, doc)   -> inverse_step (apply(inv) undoes apply(step))
//   stepMap(step, pos)      -> new_pos      (translate a SourcePos through the step)
//
//   'replace_text'   {path, from, to, text}      replace a slice of one text leaf
//   'replace'        {parent, from, to, slice}   replace children[from..to] of a node
//   'replace_around' {parent, from, to, gap_from, gap_to, slice, insert}
//   'add_mark'       {path, mark}                add `mark` to a text leaf
//   'remove_mark'    {path, mark}                remove `mark` from a text leaf
//   'set_attr'       {path, name, value}         set one attribute
//   'set_node_type'  {path, tag}                 retag a node

import {
  attrsGet,
  attrsSet,
  isNode,
  isText,
  listDrop,
  listSplice,
  listTake,
  nodeAt,
  replaceNodeAt,
  spliceChildrenAt
} from './doc.js'
import { pathEqual, pathIsPrefix, pos } from './source-pos.js'
import type {
  AddMarkStep,
  AttrValue,
  Child,
  Doc,
  MarkDict,
  RemoveMarkStep,
  ReplaceAroundStep,
  ReplaceStep,
  ReplaceTextStep,
  SetAttrStep,
  SetNodeTypeStep,
  SourcePath,
  SourcePos,
  Step,
  TextLeaf
} from './types.js'

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

export function stepReplaceText(path: SourcePath, from: number, to: number, text: string): ReplaceTextStep {
  return { kind: 'replace_text', path, from, to, text }
}

export function stepReplace(parent: SourcePath, from: number, to: number, slice: Child[]): ReplaceStep {
  return { kind: 'replace', parent, from, to, slice }
}

export function stepReplaceAround(
  parent: SourcePath,
  from: number,
  to: number,
  gap_from: number,
  gap_to: number,
  slice: Child[],
  insert: number
): ReplaceAroundStep {
  return { kind: 'replace_around', parent, from, to, gap_from, gap_to, slice, insert }
}

export function stepAddMark(path: SourcePath, name: string, value: AttrValue = true): AddMarkStep {
  return { kind: 'add_mark', path, name, value }
}

export function stepRemoveMark(path: SourcePath, name: string): RemoveMarkStep {
  return { kind: 'remove_mark', path, name }
}

export function stepSetAttr(path: SourcePath, name: string, value: AttrValue): SetAttrStep {
  return { kind: 'set_attr', path, name, value }
}

export function stepSetNodeType(path: SourcePath, tag: string): SetNodeTypeStep {
  return { kind: 'set_node_type', path, tag }
}

// ---------------------------------------------------------------------------
// Mark helpers — marks is a flat dict (see Inline_Formatting design §2)
// ---------------------------------------------------------------------------

export function hasMark(marks: MarkDict, name: string): boolean {
  return name in marks
}

export function withMark(marks: MarkDict, name: string, value: AttrValue = true): MarkDict {
  return { ...marks, [name]: value }
}

export function withoutMark(marks: MarkDict, name: string): MarkDict {
  if (!(name in marks)) return marks
  const out: MarkDict = {}
  for (const k of Object.keys(marks)) {
    if (k !== name) out[k] = marks[k] as AttrValue
  }
  return out
}

// Deep-equal-ish — used by normalization to merge adjacent same-mark text runs.
export function marksEqual(a: MarkDict, b: MarkDict): boolean {
  const ka = Object.keys(a), kb = Object.keys(b)
  if (ka.length !== kb.length) return false
  for (const k of ka) {
    if (!(k in b)) return false
    if (a[k] !== b[k]) return false   // shallow comparison sufficient for v1
  }
  return true
}

// ---------------------------------------------------------------------------
// stepApply
// ---------------------------------------------------------------------------

function applyReplaceText(step: ReplaceTextStep, doc: Doc): Doc {
  const leaf = nodeAt(doc, step.path)
  if (!isText(leaf)) throw new Error('replace_text: path does not target a text leaf')
  const newText = leaf.text.slice(0, step.from) + step.text + leaf.text.slice(step.to)
  const newLeaf: TextLeaf = { kind: 'text', text: newText, marks: leaf.marks }
  return replaceNodeAt(doc, step.path, newLeaf)
}

function applyReplace(step: ReplaceStep, doc: Doc): Doc {
  return spliceChildrenAt(doc, step.parent, step.from, step.to - step.from, step.slice)
}

function applyReplaceAround(step: ReplaceAroundStep, doc: Doc): Doc {
  const parent = nodeAt(doc, step.parent)
  if (!isNode(parent)) throw new Error('replace_around: parent is not a node')
  const gap = listTake(listDrop(parent.content, step.gap_from), step.gap_to - step.gap_from)
  const slice2 = listSplice(step.slice, step.insert, 0, gap)
  return spliceChildrenAt(doc, step.parent, step.from, step.to - step.from, slice2)
}

function applyAddMark(step: AddMarkStep, doc: Doc): Doc {
  const leaf = nodeAt(doc, step.path)
  if (!isText(leaf)) throw new Error('add_mark: path does not target a text leaf')
  return replaceNodeAt(doc, step.path, {
    kind: 'text',
    text: leaf.text,
    marks: withMark(leaf.marks, step.name, step.value)
  })
}

function applyRemoveMark(step: RemoveMarkStep, doc: Doc): Doc {
  const leaf = nodeAt(doc, step.path)
  if (!isText(leaf)) throw new Error('remove_mark: path does not target a text leaf')
  return replaceNodeAt(doc, step.path, {
    kind: 'text',
    text: leaf.text,
    marks: withoutMark(leaf.marks, step.name)
  })
}

function applySetAttr(step: SetAttrStep, doc: Doc): Doc {
  const n = nodeAt(doc, step.path)
  if (!isNode(n)) throw new Error('set_attr: path does not target a node')
  return replaceNodeAt(doc, step.path, {
    kind: 'node',
    tag: n.tag,
    attrs: attrsSet(n.attrs, step.name, step.value),
    content: n.content
  })
}

function applySetNodeType(step: SetNodeTypeStep, doc: Doc): Doc {
  const n = nodeAt(doc, step.path)
  if (!isNode(n)) throw new Error('set_node_type: path does not target a node')
  return replaceNodeAt(doc, step.path, {
    kind: 'node',
    tag: step.tag,
    attrs: n.attrs,
    content: n.content
  })
}

export function stepApply(step: Step, doc: Doc): Doc {
  switch (step.kind) {
    case 'replace_text':   return applyReplaceText(step, doc)
    case 'replace':        return applyReplace(step, doc)
    case 'replace_around': return applyReplaceAround(step, doc)
    case 'add_mark':       return applyAddMark(step, doc)
    case 'remove_mark':    return applyRemoveMark(step, doc)
    case 'set_attr':       return applySetAttr(step, doc)
    case 'set_node_type':  return applySetNodeType(step, doc)
  }
}

// ---------------------------------------------------------------------------
// stepInvert — produces the step that undoes `step`, given the doc BEFORE
// `step` was applied.
// ---------------------------------------------------------------------------

function invertReplaceText(step: ReplaceTextStep, before: Doc): Step {
  const leaf = nodeAt(before, step.path)
  if (!isText(leaf)) throw new Error('invert replace_text: not a text leaf')
  const oldText = leaf.text.slice(step.from, step.to)
  const newTo = step.from + step.text.length
  return stepReplaceText(step.path, step.from, newTo, oldText)
}

function invertReplace(step: ReplaceStep, before: Doc): Step {
  const parent = nodeAt(before, step.parent)
  if (!isNode(parent)) throw new Error('invert replace: parent not a node')
  const oldSlice = listTake(listDrop(parent.content, step.from), step.to - step.from)
  const newTo = step.from + step.slice.length
  return stepReplace(step.parent, step.from, newTo, oldSlice)
}

function invertReplaceAround(step: ReplaceAroundStep, before: Doc): Step {
  const parent = nodeAt(before, step.parent)
  if (!isNode(parent)) throw new Error('invert replace_around: parent not a node')
  const oldSlice = listTake(listDrop(parent.content, step.from), step.to - step.from)
  const newTo = step.from + step.slice.length + (step.gap_to - step.gap_from)
  return stepReplace(step.parent, step.from, newTo, oldSlice)
}

function invertSetAttr(step: SetAttrStep, before: Doc): Step {
  const n = nodeAt(before, step.path)
  if (!isNode(n)) throw new Error('invert set_attr: not a node')
  const prev = attrsGet(n.attrs, step.name)
  return stepSetAttr(step.path, step.name, prev as AttrValue)
}

function invertSetNodeType(step: SetNodeTypeStep, before: Doc): Step {
  const n = nodeAt(before, step.path)
  if (!isNode(n)) throw new Error('invert set_node_type: not a node')
  return stepSetNodeType(step.path, n.tag)
}

export function stepInvert(step: Step, before: Doc): Step {
  switch (step.kind) {
    case 'replace_text':   return invertReplaceText(step, before)
    case 'replace':        return invertReplace(step, before)
    case 'replace_around': return invertReplaceAround(step, before)
    case 'add_mark':       return stepRemoveMark(step.path, step.name)
    case 'remove_mark': {
      const leaf = nodeAt(before, step.path)
      if (!isText(leaf)) throw new Error('invert remove_mark: not a text leaf')
      const prev = leaf.marks[step.name]
      return stepAddMark(step.path, step.name, prev ?? true)
    }
    case 'set_attr':       return invertSetAttr(step, before)
    case 'set_node_type':  return invertSetNodeType(step, before)
  }
}

// ---------------------------------------------------------------------------
// stepMap — translate a SourcePos through a step.
//
// Bias rule (matches mod_step.ls): positions strictly after the affected range
// shift; positions at the boundary are pushed forward when bias >= 0, backward
// when bias < 0. Default bias = +1 (post-bias, ProseMirror's default).
// ---------------------------------------------------------------------------

function mapReplaceTextBias(step: ReplaceTextStep, p: SourcePos, bias: number): SourcePos {
  if (!pathEqual(p.path, step.path)) return p
  if (p.offset < step.from) return p
  if (p.offset > step.to) {
    return pos(p.path, p.offset + step.text.length - (step.to - step.from))
  }
  return pos(p.path, bias < 0 ? step.from : step.from + step.text.length)
}

function underReplaceParent(p: SourcePos, parentP: SourcePath): boolean {
  if (p.path.length <= parentP.length) return false
  return pathIsPrefix(parentP, p.path)
}

function mapReplaceBias(step: ReplaceStep, p: SourcePos, bias: number): SourcePos {
  const pp = step.parent
  const depth = pp.length
  const sameParentPos = p.path.length === depth && pathEqual(p.path, pp)
  const inside = underReplaceParent(p, pp)
  if (!sameParentPos && !inside) return p

  if (sameParentPos) {
    if (p.offset < step.from) return p
    if (p.offset > step.to) {
      return pos(p.path, p.offset + step.slice.length - (step.to - step.from))
    }
    return pos(p.path, bias < 0 ? step.from : step.from + step.slice.length)
  }

  // inside one of the parent's children
  const ci = p.path[depth] as number
  if (ci < step.from) return p
  if (ci >= step.to) {
    const newCi = ci + step.slice.length - (step.to - step.from)
    const newPath = [...listTake(p.path, depth), newCi, ...listDrop(p.path, depth + 1)]
    return pos(newPath, p.offset)
  }
  // p was inside a deleted child — collapse to the boundary
  return pos(pp, bias < 0 ? step.from : step.from + step.slice.length)
}

function replaceAroundNewSize(step: ReplaceAroundStep): number {
  return step.slice.length + (step.gap_to - step.gap_from)
}

function mapReplaceAroundBias(step: ReplaceAroundStep, p: SourcePos, bias: number): SourcePos {
  const pp = step.parent
  const depth = pp.length
  const sameParentPos = p.path.length === depth && pathEqual(p.path, pp)
  const inside = underReplaceParent(p, pp)
  if (!sameParentPos && !inside) return p

  if (sameParentPos) {
    if (p.offset < step.from) return p
    if (p.offset >= step.gap_from && p.offset <= step.gap_to) {
      return pos(p.path, step.from + step.insert + (p.offset - step.gap_from))
    }
    if (p.offset >= step.to) {
      return pos(p.path, p.offset + replaceAroundNewSize(step) - (step.to - step.from))
    }
    return pos(p.path, bias < 0 ? step.from : step.from + step.insert)
  }

  const ci = p.path[depth] as number
  if (ci < step.from) return p
  if (ci >= step.to) {
    const newCi = ci + replaceAroundNewSize(step) - (step.to - step.from)
    const newPath = [...listTake(p.path, depth), newCi, ...listDrop(p.path, depth + 1)]
    return pos(newPath, p.offset)
  }
  if (ci >= step.gap_from && ci < step.gap_to) {
    const newCi = step.from + step.insert + (ci - step.gap_from)
    const newPath = [...listTake(p.path, depth), newCi, ...listDrop(p.path, depth + 1)]
    return pos(newPath, p.offset)
  }
  return pos(pp, bias < 0 ? step.from : step.from + step.insert)
}

export function stepMapBias(step: Step, p: SourcePos, bias: number): SourcePos {
  switch (step.kind) {
    case 'replace_text':   return mapReplaceTextBias(step, p, bias)
    case 'replace':        return mapReplaceBias(step, p, bias)
    case 'replace_around': return mapReplaceAroundBias(step, p, bias)
    default:               return p
  }
}

export function stepMap(step: Step, p: SourcePos): SourcePos {
  return stepMapBias(step, p, 1)
}

export function stepMapAll(step: Step, ps: SourcePos[]): SourcePos[] {
  return ps.map(p => stepMap(step, p))
}
