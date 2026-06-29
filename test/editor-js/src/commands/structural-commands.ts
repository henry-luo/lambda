// Structural editing commands: lists, tables, and images.
//
// Ports the corresponding functions from lambda/package/editor/mod_commands.ls.
// Each command returns Transaction | null (null = does not apply). They build
// on the same step vocabulary (replace) as the text commands — no new step
// kinds.

import {
  attrsGet,
  isNode,
  isText,
  lastIndex,
  node,
  nodeAt,
  nodeAttrs,
  parentPath,
  withContent
} from '../model/doc.js'
import { nodeSelection, pos } from '../model/source-pos.js'
import { stepReplace, stepSetAttr } from '../model/step.js'
import { txBegin, txSetSelection, txStep } from '../model/transaction.js'
import { caret, selCollapsed } from './sel.js'
import type { EditorState } from './types.js'
import type { Attr, AttrValue, Child, Node, Selection, SourcePath, Transaction } from '../model/types.js'

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Walk up from `path` to the nearest ancestor whose tag matches `tag`.
function ancestorTag(doc: Node, path: SourcePath, tag: string): SourcePath | null {
  for (let depth = path.length; depth >= 0; depth--) {
    const p = path.slice(0, depth)
    const n = nodeAt(doc, p)
    if (n !== null && isNode(n) && n.tag === tag) return p
  }
  return null
}

function isListNode(n: Child | null): n is Node {
  return n !== null && isNode(n) && (n.tag === 'ul' || n.tag === 'ol' || n.tag === 'list')
}

// The top-level block index addressed by the current selection, or null.
function topBlockIndex(sel: Selection | null): number | null {
  if (sel === null) return null
  if (sel.kind === 'text') return sel.anchor.path.length > 0 ? (sel.anchor.path[0] as number) : null
  if (sel.kind === 'node') return sel.path.length > 0 ? (sel.path[0] as number) : null
  return null
}

// ---------------------------------------------------------------------------
// Lists
// ---------------------------------------------------------------------------

// Wrap the current top-level block in a single-item list. The block's inline
// content becomes the <li> content.
export function cmdWrapInList(state: EditorState, kind: 'ul' | 'ol' = 'ul'): Transaction | null {
  const sel = state.selection
  const blockIdx = topBlockIndex(sel)
  if (sel === null || blockIdx === null) return null
  const block = nodeAt(state.doc, [blockIdx])
  if (block === null || !isNode(block)) return null
  if (block.tag === 'ul' || block.tag === 'ol') return null  // already a list

  const li = node('li', block.content)
  const list = node(kind, [li])
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace([], blockIdx, blockIdx + 1, [list]))

  if (sel.kind === 'text') {
    // doc > list(blockIdx) > li(0) > ...originalSubPath
    const sub = sel.anchor.path.slice(1)
    const newPath = [blockIdx, 0, ...sub]
    return txSetSelection(tx, {
      kind: 'text',
      anchor: pos(newPath, sel.anchor.offset),
      head: pos(newPath, sel.anchor.offset)
    })
  }
  return txSetSelection(tx, nodeSelection([blockIdx, 0]))
}

// Markdown-style list autoformat: typing a space after a line that is exactly a
// list marker turns the block into a list. "-", "*", "+" → bullet list; "1."
// (any "N.") → ordered list. The marker text is consumed. Returns null when the
// block isn't a bare marker. Call this on space-insertion before cmdInsertText.
const UL_MARKERS = new Set(['-', '*', '+'])

export function cmdAutoformatList(state: EditorState): Transaction | null {
  const sel = state.selection
  if (sel === null || sel.kind !== 'text' || !selCollapsed(sel)) return null
  const p = sel.anchor
  if (p.path.length < 2) return null
  const leaf = nodeAt(state.doc, p.path)
  if (leaf === null || !isText(leaf) || p.offset !== leaf.text.length) return null
  const blockPath = parentPath(p.path)
  const block = nodeAt(state.doc, blockPath)
  if (block === null || !isNode(block)) return null
  if (block.tag === 'li' || block.tag === 'list_item') return null   // already a list item
  // the block must be exactly the marker (a single text leaf)
  if (block.content.length !== 1) return null
  const only = block.content[0] as Child
  if (!isText(only)) return null
  const marker = only.text
  let ordered: boolean
  if (UL_MARKERS.has(marker)) ordered = false
  else if (/^\d+\.$/.test(marker)) ordered = true
  else return null

  const useHtml = state.schema.entries['ul'] !== undefined
  const listTag = useHtml ? (ordered ? 'ol' : 'ul') : 'list'
  const itemTag = useHtml ? 'li' : 'list_item'
  const listAttrs: Attr[] = (!useHtml && ordered) ? [{ name: 'ordered', value: true }] : []
  const item = node(itemTag, useHtml ? [] : [node('paragraph', [])])
  const listNode = nodeAttrs(listTag, listAttrs, [item])

  const parent = parentPath(blockPath)
  const blockIdx = lastIndex(blockPath)
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(parent, blockIdx, blockIdx + 1, [listNode]))
  const itemPath = [...parent, blockIdx, 0]
  const caretPath = useHtml ? itemPath : [...itemPath, 0]
  return txSetSelection(tx, caret(pos(caretPath, 0)))
}

// Maximum indent level (Word caps around 9; keep it bounded).
const MAX_INDENT = 8

function listItemIndent(item: Node): number {
  const v = attrsGet(item.attrs, 'indent')
  return typeof v === 'number' ? v : 0
}

// Indent / outdent the current list item by one indent LEVEL — a flat-list model
// (Word / Google-Docs style). The level is stored as an `indent` attribute and
// rendered with left-margin; no nested <ul>/<li> is created. Works from any
// caret position and on the FIRST item; Tab repeats up to MAX_INDENT, Shift-Tab
// decreases to 0 (which removes the attribute). The caret is untouched because
// only an attribute changes.
export function cmdIndentListItem(state: EditorState): Transaction | null {
  const sel = state.selection
  const basePath = sel?.kind === 'node' ? sel.path : sel?.kind === 'text' ? sel.anchor.path : null
  if (sel === null || basePath === null) return null
  const itemPath = ancestorTag(state.doc, basePath, 'li')
  if (itemPath === null) return null
  const item = nodeAt(state.doc, itemPath)
  if (item === null || !isNode(item)) return null
  const cur = listItemIndent(item)
  if (cur >= MAX_INDENT) return null
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepSetAttr(itemPath, 'indent', cur + 1))
  return tx
}

export function cmdOutdentListItem(state: EditorState): Transaction | null {
  const sel = state.selection
  const basePath = sel?.kind === 'node' ? sel.path : sel?.kind === 'text' ? sel.anchor.path : null
  if (sel === null || basePath === null) return null
  const itemPath = ancestorTag(state.doc, basePath, 'li')
  if (itemPath === null) return null
  const item = nodeAt(state.doc, itemPath)
  if (item === null || !isNode(item)) return null
  const cur = listItemIndent(item)
  if (cur > 0) {
    // flat model: drop one indent level (null removes the attr at level 0;
    // cast mirrors invertSetAttr's null handling).
    const next = cur - 1
    let tx = txBegin(state.doc, sel)
    tx = txStep(tx, stepSetAttr(itemPath, 'indent', (next === 0 ? null : next) as AttrValue))
    return tx
  }
  // level 0: fall back to structural un-nesting so nested lists from HTML input
  // can still be lifted out.
  return outdentNestedItem(state, sel, itemPath)
}

// After a structurally-nested item moves on outdent, keep the caret inside it
// (its subtree moves intact, so the relative path is preserved).
function reselectMovedItem(sel: Selection, oldItemPath: SourcePath, newItemPath: SourcePath): Selection {
  if (sel.kind === 'text') {
    const rel = sel.anchor.path.slice(oldItemPath.length)
    return caret(pos([...newItemPath, ...rel], sel.anchor.offset))
  }
  return nodeSelection(newItemPath)
}

function replaceOrRemoveSublist(parentItem: Node, subIndex: number, sublist: Node): Child[] {
  if (sublist.content.length === 0) return [...parentItem.content.slice(0, subIndex), ...parentItem.content.slice(subIndex + 1)]
  return [...parentItem.content.slice(0, subIndex), sublist, ...parentItem.content.slice(subIndex + 1)]
}

// Lift a structurally-nested list item out to its grandparent list (the legacy
// nested-list outdent). Used only for items that came from nested HTML input.
function outdentNestedItem(state: EditorState, sel: Selection, itemPath: SourcePath): Transaction | null {
  const listPath = parentPath(itemPath)
  const itemIndex = lastIndex(itemPath)
  const parentItemPath = parentPath(listPath)
  const grandListPath = parentPath(parentItemPath)
  const parentItemIndex = lastIndex(parentItemPath)
  const listChildIndex = lastIndex(listPath)
  const listNode = nodeAt(state.doc, listPath)
  const parentItem = nodeAt(state.doc, parentItemPath)
  const grandList = nodeAt(state.doc, grandListPath)
  if (listNode === null || parentItem === null || grandList === null ||
      !isNode(listNode) || !isNode(parentItem) || !isNode(grandList) ||
      !isListNode(listNode) || parentItem.tag !== 'li' || !isListNode(grandList)) {
    return null
  }
  const item = listNode.content[itemIndex] as Child
  const list2 = withContent(listNode, [...listNode.content.slice(0, itemIndex), ...listNode.content.slice(itemIndex + 1)])
  const parent2 = nodeAttrs(parentItem.tag, parentItem.attrs, replaceOrRemoveSublist(parentItem, listChildIndex, list2))
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(grandListPath, parentItemIndex, parentItemIndex + 1, [parent2, item]))
  return txSetSelection(tx, reselectMovedItem(sel, itemPath, [...grandListPath, parentItemIndex + 1]))
}

// True iff a block's content is "empty" — nothing, a single empty text leaf,
// or a single empty wrapper block (e.g. <li></li>, <li><p></p></li>).
function isEmptyBlockContent(content: Child[]): boolean {
  if (content.length === 0) return true
  if (content.length === 1) {
    const c = content[0] as Child
    if (isText(c)) return c.text === ''
    if (isNode(c)) return isEmptyBlockContent(c.content)
  }
  return false
}

// cmdEnterEmptyListItem — Mac-Notes Enter behaviour. Pressing Enter in an EMPTY
// list item lifts it instead of creating another empty item:
//   * nested item   → outdent one level (still a bullet, parent list level)
//   * top-level item → exit the list as an empty paragraph (the list splits if
//                      items follow), caret placed in that paragraph.
// Returns null when the caret is not in an empty list item (caller falls back
// to the normal split).
export function cmdEnterEmptyListItem(state: EditorState): Transaction | null {
  const sel = state.selection
  const basePath = sel?.kind === 'text' ? sel.anchor.path : null
  if (sel === null || basePath === null) return null
  const itemPath = ancestorTag(state.doc, basePath, 'li')
  if (itemPath === null) return null
  const item = nodeAt(state.doc, itemPath)
  if (item === null || !isNode(item) || !isEmptyBlockContent(item.content)) return null

  // Nested item: reuse the outdent transform, but leave a caret inside the
  // (empty) outdented item rather than selecting it, so the user keeps typing.
  const outdent = cmdOutdentListItem(state)
  if (outdent !== null) {
    const after = outdent.sel_after
    if (after !== null && after.kind === 'node') {
      return txSetSelection(outdent, caret(pos(after.path, 0)))
    }
    return outdent
  }

  // Top-level item: exit the list as an empty default block (paragraph).
  const listPath = parentPath(itemPath)
  const listNode = nodeAt(state.doc, listPath)
  if (listNode === null || !isNode(listNode) || !isListNode(listNode)) return null
  const listParentPath = parentPath(listPath)
  const listIndex = lastIndex(listPath)
  const itemIndex = lastIndex(itemPath)

  const before = listNode.content.slice(0, itemIndex)
  const after = listNode.content.slice(itemIndex + 1)
  const para = node(state.schema.default_block, [])
  const replacement: Child[] = []
  if (before.length > 0) replacement.push(withContent(listNode, before))
  const paraOffset = replacement.length
  replacement.push(para)
  if (after.length > 0) replacement.push(withContent(listNode, after))

  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(listParentPath, listIndex, listIndex + 1, replacement))
  const paraPath = [...listParentPath, listIndex + paraOffset]
  return txSetSelection(tx, caret(pos(paraPath, 0)))
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------

// Insert an inline image at a collapsed text caret. The image becomes a
// NodeSelection so it can be styled or deleted as a unit.
export function cmdInsertImage(state: EditorState, src: string, alt = ''): Transaction | null {
  const sel = state.selection
  if (sel === null || sel.kind !== 'text') return null
  const lo = sel.anchor
  const hi = sel.head
  // same-leaf only (the common caret case)
  if (lo.path.join(',') !== hi.path.join(',')) return null
  const leaf = nodeAt(state.doc, lo.path)
  if (leaf === null || leaf.kind !== 'text' || lo.path.length < 2) return null

  const from = Math.min(lo.offset, hi.offset)
  const to = Math.max(lo.offset, hi.offset)
  const blockPath = parentPath(lo.path)
  const leafIdx = lastIndex(lo.path)
  const before = leaf.text.slice(0, from)
  const after = leaf.text.slice(to)
  const img: Node = nodeAttrs('img', [{ name: 'src', value: src }, { name: 'alt', value: alt }] as Attr[], [])

  const slice: Child[] = []
  if (before.length > 0) slice.push({ kind: 'text', text: before, marks: leaf.marks })
  slice.push(img)
  if (after.length > 0) slice.push({ kind: 'text', text: after, marks: leaf.marks })

  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(blockPath, leafIdx, leafIdx + 1, slice))
  const imgIdx = leafIdx + (before.length > 0 ? 1 : 0)
  return txSetSelection(tx, nodeSelection([...blockPath, imgIdx]))
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

// New cells are created with empty content (not a text('') leaf) so they
// round-trip through the HTML parser, which produces no leaf for `<td></td>`.
function makeCells(cols: number, cellTag: 'td' | 'th'): Node[] {
  const out: Node[] = []
  for (let i = 0; i < cols; i++) out.push(node(cellTag, []))
  return out
}

function makeRow(cols: number, header: boolean): Node {
  return node('tr', makeCells(cols, header ? 'th' : 'td'))
}

function makeTableNode(rows: number, cols: number, header: boolean): Node {
  const trs: Node[] = []
  for (let r = 0; r < rows; r++) trs.push(makeRow(cols, header && r === 0))
  return node('table', trs)
}

function emptyCellLike(cell: Child | null): Node {
  return node(cell !== null && isNode(cell) && cell.tag === 'th' ? 'th' : 'td', [])
}

// Insert a block after the top-level block containing the selection.
function insertBlockAfter(state: EditorState, block: Node): Transaction | null {
  const sel = state.selection
  const blockIdx = topBlockIndex(sel)
  if (sel === null || blockIdx === null) return null
  const insertAt = blockIdx + 1
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace([], insertAt, insertAt, [block]))
  return txSetSelection(tx, nodeSelection([insertAt]))
}

export function cmdInsertTable(state: EditorState, rows = 2, cols = 2, header = true): Transaction | null {
  if (rows <= 0 || cols <= 0) return null
  return insertBlockAfter(state, makeTableNode(rows, cols, header))
}

interface TableContext {
  cellPath: SourcePath
  rowPath: SourcePath
  tablePath: SourcePath
  table: Node
  row: Node
  rowContainerPath: SourcePath
  rowContainer: Node
  rowIndex: number
  colIndex: number
}

// Resolve the table context around the current selection. Supports
// `table > tr > (td|th)` and `table > (thead|tbody|tfoot) > tr > (td|th)`.
function tableContext(state: EditorState): TableContext | null {
  const sel = state.selection
  if (sel === null || sel.kind === 'multi-node') return null
  const basePath = sel.kind === 'node' ? sel.path : sel.anchor.path
  let cellPath = ancestorTag(state.doc, basePath, 'td')
  if (cellPath === null) cellPath = ancestorTag(state.doc, basePath, 'th')
  if (cellPath === null) return null

  const rowPath = parentPath(cellPath)
  const rowContainerPath = parentPath(rowPath)
  const rowContainer = nodeAt(state.doc, rowContainerPath)
  if (rowContainer === null || !isNode(rowContainer)) return null

  let tablePath: SourcePath
  if (rowContainer.tag === 'table') tablePath = rowContainerPath
  else tablePath = parentPath(rowContainerPath)  // tbody/thead/tfoot → table

  const table = nodeAt(state.doc, tablePath)
  const row = nodeAt(state.doc, rowPath)
  if (table === null || row === null || !isNode(table) || !isNode(row) ||
      table.tag !== 'table' || row.tag !== 'tr') {
    return null
  }
  return {
    cellPath, rowPath, tablePath, table, row,
    rowContainerPath, rowContainer,
    rowIndex: lastIndex(rowPath),
    colIndex: lastIndex(cellPath)
  }
}

export function cmdAddTableRow(state: EditorState): Transaction | null {
  const ctx = tableContext(state)
  if (ctx === null) return null
  const insertAt = ctx.rowIndex + 1
  const cols = ctx.row.content.length
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepReplace(ctx.rowContainerPath, insertAt, insertAt, [makeRow(cols, false)]))
  return txSetSelection(tx, nodeSelection([...ctx.rowContainerPath, insertAt]))
}

export function cmdDeleteTableRow(state: EditorState): Transaction | null {
  const ctx = tableContext(state)
  if (ctx === null || ctx.rowContainer.content.length <= 1) return null
  let tx = txBegin(state.doc, state.selection)
  tx = txStep(tx, stepReplace(ctx.rowContainerPath, ctx.rowIndex, ctx.rowIndex + 1, []))
  const nextRow = ctx.rowIndex >= ctx.rowContainer.content.length - 1 ? ctx.rowIndex - 1 : ctx.rowIndex
  return txSetSelection(tx, nodeSelection([...ctx.rowContainerPath, Math.max(0, nextRow)]))
}

// Apply a list of independent same-row column edits. Steps target different
// rows, so order does not matter for correctness here.
function applySteps(state: EditorState, steps: ReturnType<typeof stepReplace>[]): Transaction {
  let tx = txBegin(state.doc, state.selection)
  for (const s of steps) tx = txStep(tx, s)
  return tx
}

// Build per-row "insert a cell after colIndex" steps for one row container.
function addColStepsForContainer(containerPath: SourcePath, container: Node, colIndex: number): ReturnType<typeof stepReplace>[] {
  const steps: ReturnType<typeof stepReplace>[] = []
  for (let i = 0; i < container.content.length; i++) {
    const row = container.content[i]
    if (row !== undefined && isNode(row) && row.tag === 'tr') {
      const ref = colIndex < row.content.length ? (row.content[colIndex] as Child) : null
      steps.push(stepReplace([...containerPath, i], colIndex + 1, colIndex + 1, [emptyCellLike(ref)]))
    }
  }
  return steps
}

function tableColSteps(tablePath: SourcePath, table: Node, colIndex: number, mode: 'add' | 'del'): ReturnType<typeof stepReplace>[] {
  const steps: ReturnType<typeof stepReplace>[] = []
  for (let i = 0; i < table.content.length; i++) {
    const child = table.content[i]
    if (child === undefined || !isNode(child)) continue
    if (child.tag === 'tr') {
      if (mode === 'add') {
        const ref = colIndex < child.content.length ? (child.content[colIndex] as Child) : null
        steps.push(stepReplace([...tablePath, i], colIndex + 1, colIndex + 1, [emptyCellLike(ref)]))
      } else if (colIndex < child.content.length) {
        steps.push(stepReplace([...tablePath, i], colIndex, colIndex + 1, []))
      }
    } else if (child.tag === 'thead' || child.tag === 'tbody' || child.tag === 'tfoot') {
      if (mode === 'add') {
        steps.push(...addColStepsForContainer([...tablePath, i], child, colIndex))
      } else {
        for (let r = 0; r < child.content.length; r++) {
          const row = child.content[r]
          if (row !== undefined && isNode(row) && row.tag === 'tr' && colIndex < row.content.length) {
            steps.push(stepReplace([...tablePath, i, r], colIndex, colIndex + 1, []))
          }
        }
      }
    }
  }
  return steps
}

export function cmdAddTableColumn(state: EditorState): Transaction | null {
  const ctx = tableContext(state)
  if (ctx === null) return null
  const steps = tableColSteps(ctx.tablePath, ctx.table, ctx.colIndex, 'add')
  const tx = applySteps(state, steps)
  return txSetSelection(tx, nodeSelection([...ctx.rowPath, ctx.colIndex + 1]))
}

export function cmdDeleteTableColumn(state: EditorState): Transaction | null {
  const ctx = tableContext(state)
  if (ctx === null || ctx.row.content.length <= 1) return null
  const steps = tableColSteps(ctx.tablePath, ctx.table, ctx.colIndex, 'del')
  const tx = applySteps(state, steps)
  const nextCol = ctx.colIndex >= ctx.row.content.length - 1 ? ctx.colIndex - 1 : ctx.colIndex
  return txSetSelection(tx, nodeSelection([...ctx.rowPath, Math.max(0, nextCol)]))
}

// Convenience: place a caret inside a table cell (used by callers/tests).
export function caretInCell(rowPath: SourcePath, colIndex: number): Selection {
  return caret(pos([...rowPath, colIndex, 0], 0))
}

// ---------------------------------------------------------------------------
// Generic node-attribute edits (used by image resize, inspector, etc.)
// ---------------------------------------------------------------------------

export function cmdSetNodeAttr(state: EditorState, path: SourcePath, name: string, value: import('../model/types.js').AttrValue): Transaction | null {
  const n = nodeAt(state.doc, path)
  if (n === null || !isNode(n)) return null
  let tx = txBegin(state.doc, nodeSelection(path))
  tx = txStep(tx, stepSetAttr(path, name, value))
  return txSetSelection(tx, nodeSelection(path))
}

// Resize an image (or any node) by setting width + height in one transaction.
export function cmdResizeImage(state: EditorState, path: SourcePath, width: number, height: number): Transaction | null {
  const n = nodeAt(state.doc, path)
  if (n === null || !isNode(n)) return null
  let tx = txBegin(state.doc, nodeSelection(path))
  tx = txStep(tx, stepSetAttr(path, 'width', Math.round(width)))
  tx = txStep(tx, stepSetAttr(path, 'height', Math.round(height)))
  return txSetSelection(tx, nodeSelection(path))
}
