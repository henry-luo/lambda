// Structural editing commands: lists, tables, and images.
//
// Ports the corresponding functions from lambda/package/editor/mod_commands.ls.
// Each command returns Transaction | null (null = does not apply). They build
// on the same step vocabulary (replace) as the text commands — no new step
// kinds.

import {
  isNode,
  lastIndex,
  listSet,
  listSplice,
  node,
  nodeAt,
  nodeAttrs,
  parentPath,
  withContent
} from '../model/doc.js'
import { nodeSelection, pos } from '../model/source-pos.js'
import { stepReplace } from '../model/step.js'
import { txBegin, txSetSelection, txStep } from '../model/transaction.js'
import { caret } from './sel.js'
import type { EditorState } from './types.js'
import type { Attr, Child, Node, Selection, SourcePath, Transaction } from '../model/types.js'

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

function sameListKind(a: Child, b: Child): boolean {
  return isListNode(a) && isListNode(b) && a.tag === b.tag
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

function appendToSublist(prevItem: Node, listNode: Node, item: Child): Node {
  const kids = prevItem.content
  const last = kids[kids.length - 1]
  if (kids.length > 0 && last !== undefined && sameListKind(last, listNode)) {
    const sub = last as Node
    const sub2 = withContent(sub, [...sub.content, item])
    return nodeAttrs(prevItem.tag, prevItem.attrs, listSet(kids, kids.length - 1, sub2))
  }
  return nodeAttrs(prevItem.tag, prevItem.attrs, [...kids, nodeAttrs(listNode.tag, listNode.attrs, [item])])
}

// Indent the current list item — nest it as a sublist under the previous item.
export function cmdIndentListItem(state: EditorState): Transaction | null {
  const sel = state.selection
  const basePath = sel?.kind === 'node' ? sel.path : sel?.kind === 'text' ? sel.anchor.path : null
  if (sel === null || basePath === null) return null
  const itemPath = ancestorTag(state.doc, basePath, 'li')
  if (itemPath === null) return null
  const listPath = parentPath(itemPath)
  const itemIndex = lastIndex(itemPath)
  const listNode = nodeAt(state.doc, listPath)
  if (listNode === null || !isListNode(listNode) || itemIndex <= 0) return null

  const item = listNode.content[itemIndex] as Child
  const prevItem = listNode.content[itemIndex - 1] as Child
  if (!isNode(prevItem)) return null
  const prev2 = appendToSublist(prevItem, listNode, item)
  const newItems = listSplice(listSet(listNode.content, itemIndex - 1, prev2), itemIndex, 1, [])
  const list2 = nodeAttrs(listNode.tag, listNode.attrs, newItems)

  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(parentPath(listPath), lastIndex(listPath), lastIndex(listPath) + 1, [list2]))
  return txSetSelection(tx, nodeSelection([...listPath, itemIndex - 1]))
}

function replaceOrRemoveSublist(parentItem: Node, subIndex: number, sublist: Node): Child[] {
  if (sublist.content.length === 0) return listSplice(parentItem.content, subIndex, 1, [])
  return listSet(parentItem.content, subIndex, sublist)
}

// Outdent the current (nested) list item — lift it out to the grandparent list
// as a sibling of its parent item.
export function cmdOutdentListItem(state: EditorState): Transaction | null {
  const sel = state.selection
  const basePath = sel?.kind === 'node' ? sel.path : sel?.kind === 'text' ? sel.anchor.path : null
  if (sel === null || basePath === null) return null
  const itemPath = ancestorTag(state.doc, basePath, 'li')
  if (itemPath === null) return null
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
  const list2 = withContent(listNode, listSplice(listNode.content, itemIndex, 1, []))
  const parent2 = nodeAttrs(parentItem.tag, parentItem.attrs, replaceOrRemoveSublist(parentItem, listChildIndex, list2))

  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(grandListPath, parentItemIndex, parentItemIndex + 1, [parent2, item]))
  return txSetSelection(tx, nodeSelection([...grandListPath, parentItemIndex + 1]))
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
