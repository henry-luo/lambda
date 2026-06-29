// Input rules — markdown-style autoformat that fires as a single character is
// typed (a space, or a closing delimiter), generalizing the list autoformat
// (cmdAutoformatList) to headings, blockquote, hr, and inline bold / italic /
// code / strike. Each rule compiles to the existing step vocabulary and the
// triggering character is consumed (not inserted).
//
// Wired into dispatchIntent: on a 1-char insertText, cmdInputRule runs first; a
// non-null result means the rule fired and the character must NOT be inserted.

import { isNode, isText, lastIndex, node, nodeAttrs, nodeAt, parentPath } from '../model/doc.js'
import { stepReplace, withMark } from '../model/step.js'
import { pos } from '../model/source-pos.js'
import { txBegin, txSetSelection, txStep } from '../model/transaction.js'
import { caret, selCollapsed } from './sel.js'
import { cmdAutoformatList } from './structural-commands.js'
import type { EditorState } from './types.js'
import type { Node, Selection, SourcePath, TextLeaf, Transaction } from '../model/types.js'

const TRIGGERS = new Set([' ', '-', '*', '`', '~'])

// Inline-wrap rules: when the typed `char` completes `re` at the end of the text
// before the caret, wrap the captured group in `mark` and drop the delimiters.
// Order matters — bold (`**`) is tried before italic (`*`).
const INLINE_RULES: { char: string; re: RegExp; mark: string }[] = [
  { char: '*', re: /\*\*([^*]+?)\*\*$/, mark: 'bold' },
  { char: '*', re: /\*([^*]+?)\*$/,     mark: 'italic' },
  { char: '`', re: /`([^`]+?)`$/,       mark: 'code' },
  { char: '~', re: /~~([^~]+?)~~$/,     mark: 'strikethrough' }
]

export function cmdInputRule(state: EditorState, char: string): Transaction | null {
  if (!TRIGGERS.has(char)) return null
  const sel = state.selection
  if (sel === null || sel.kind !== 'text' || !selCollapsed(sel)) return null
  const p = sel.anchor
  if (p.path.length < 2) return null
  const leaf = nodeAt(state.doc, p.path)
  if (leaf === null || !isText(leaf)) return null
  const blockPath = parentPath(p.path)
  const block = nodeAt(state.doc, blockPath)
  if (block === null || !isNode(block)) return null

  if (char === ' ') return blockRule(state, sel, leaf, block, blockPath, p.offset)
  if (char === '-') {
    const hr = hrRule(state, sel, leaf, block, blockPath, p.offset)
    if (hr !== null) return hr
  }
  return inlineRule(state, sel, leaf, blockPath, lastIndex(p.path), p.offset, char)
}

// Replace the block at blockPath with newBlocks; place the caret at caretPos.
function replaceBlock(state: EditorState, sel: Selection, blockPath: SourcePath, newBlocks: Node[], caretPos: ReturnType<typeof pos>): Transaction {
  const parent = parentPath(blockPath)
  const idx = lastIndex(blockPath)
  let tx = txBegin(state.doc, sel)
  tx = txStep(tx, stepReplace(parent, idx, idx + 1, newBlocks))
  return txSetSelection(tx, caret(caretPos))
}

// Block rules fire on space when the block is exactly a marker (one text leaf,
// caret at its end): `#`..`######` → heading, `>` → blockquote. Otherwise defer
// to the list autoformat (`- `, `1. `).
function blockRule(state: EditorState, sel: Selection, leaf: TextLeaf, block: Node, blockPath: SourcePath, offset: number): Transaction | null {
  const bare = offset === leaf.text.length && block.content.length === 1 && isText(block.content[0])
  if (!bare || block.tag === 'li' || block.tag === 'list_item') return cmdAutoformatList(state)
  const marker = (block.content[0] as TextLeaf).text

  if (/^#{1,6}$/.test(marker)) {
    const level = marker.length
    let heading: Node
    if (state.schema.entries['h' + level] !== undefined) heading = node('h' + level, [])
    else if (state.schema.entries['heading'] !== undefined) heading = nodeAttrs('heading', [{ name: 'level', value: level }], [])
    else return cmdAutoformatList(state)
    return replaceBlock(state, sel, blockPath, [heading], pos(blockPath, 0))
  }
  if (marker === '>' && state.schema.entries['blockquote'] !== undefined) {
    const pTag = state.schema.entries['p'] !== undefined ? 'p' : 'paragraph'
    return replaceBlock(state, sel, blockPath, [node('blockquote', [node(pTag, [])])], pos([...blockPath, 0], 0))
  }
  return cmdAutoformatList(state)
}

// `---` → horizontal rule (fires on the third `-`, when the paragraph is `--`).
function hrRule(state: EditorState, sel: Selection, leaf: TextLeaf, block: Node, blockPath: SourcePath, offset: number): Transaction | null {
  if (offset !== leaf.text.length || block.content.length !== 1 || !isText(block.content[0])) return null
  if ((block.content[0] as TextLeaf).text !== '--') return null
  if (block.tag !== 'p' && block.tag !== 'paragraph') return null
  if (state.schema.entries['hr'] === undefined) return null
  const pTag = state.schema.entries['p'] !== undefined ? 'p' : 'paragraph'
  const parent = parentPath(blockPath)
  const idx = lastIndex(blockPath)
  return replaceBlock(state, sel, blockPath, [node('hr', []), node(pTag, [])], pos([...parent, idx + 1], 0))
}

// Inline-wrap rules fire on the closing delimiter; the captured run gets the mark
// and the delimiters + typed char are consumed.
function inlineRule(state: EditorState, sel: Selection, leaf: TextLeaf, blockPath: SourcePath, leafIdx: number, offset: number, char: string): Transaction | null {
  const combined = leaf.text.slice(0, offset) + char
  for (const rule of INLINE_RULES) {
    if (rule.char !== char) continue
    const m = rule.re.exec(combined)
    if (m === null || m.index === undefined) continue
    const captured = m[1] as string
    const before = leaf.text.slice(0, m.index)
    const after = leaf.text.slice(offset)
    const newLeaves: TextLeaf[] = []
    if (before.length > 0) newLeaves.push({ kind: 'text', text: before, marks: leaf.marks })
    const markedIdx = leafIdx + newLeaves.length
    newLeaves.push({ kind: 'text', text: captured, marks: withMark(leaf.marks, rule.mark, true) })
    if (after.length > 0) newLeaves.push({ kind: 'text', text: after, marks: leaf.marks })
    let tx = txBegin(state.doc, sel)
    tx = txStep(tx, stepReplace(blockPath, leafIdx, leafIdx + 1, newLeaves))
    return txSetSelection(tx, caret(pos([...blockPath, markedIdx], captured.length)))
  }
  return null
}
