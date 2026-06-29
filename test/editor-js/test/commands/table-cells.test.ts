import { describe, it, expect } from 'vitest'
import { node, nodeAttrs, text } from '../../src/model/doc.js'
import { multiNodeSelection, nodeSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { serializeDocToHtml } from '../../src/view/html-parser.js'
import { cmdMergeCells, cmdSplitCell } from '../../src/commands/structural-commands.js'
import type { EditorState } from '../../src/commands/types.js'
import type { Selection } from '../../src/model/types.js'

// table > tr > td*  (the parser drops <tbody>, so the model nests cells directly)
function tableDoc(): any[] {
  return [node('table', [node('tr', [
    node('td', [text('a')]), node('td', [text('b')]), node('td', [text('c')])
  ])])]
}
function st(blocks: any[], sel: Selection): EditorState {
  return { doc: node('doc', blocks), schema: html5SubsetSchema, selection: sel, stored_marks: null }
}
const html = (d: any) => serializeDocToHtml(d, { schema: html5SubsetSchema })

describe('table cell merge / split', () => {
  it('merges two contiguous cells in a row (colspan), concatenating content', () => {
    const tx = cmdMergeCells(st(tableDoc(), multiNodeSelection([[0, 0, 0], [0, 0, 1]])))!
    expect(html(tx.doc_after)).toBe('<doc><table><tr><td colspan="2">ab</td><td>c</td></tr></table></doc>')
    expect(tx.sel_after).toEqual(nodeSelection([0, 0, 0]))
  })

  it('splits a colspan cell back into individual cells', () => {
    const merged = [node('table', [node('tr', [
      nodeAttrs('td', [{ name: 'colspan', value: 2 }], [text('ab')]), node('td', [text('c')])
    ])])]
    const tx = cmdSplitCell(st(merged, nodeSelection([0, 0, 0])))!
    expect(html(tx.doc_after)).toBe('<doc><table><tr><td>ab</td><td></td><td>c</td></tr></table></doc>')
  })

  it('refuses to merge non-contiguous cells', () => {
    expect(cmdMergeCells(st(tableDoc(), multiNodeSelection([[0, 0, 0], [0, 0, 2]])))).toBeNull()
  })

  it('refuses to merge cells from different rows', () => {
    const two = [node('table', [
      node('tr', [node('td', [text('a')]), node('td', [text('b')])]),
      node('tr', [node('td', [text('c')]), node('td', [text('d')])])
    ])]
    expect(cmdMergeCells(st(two, multiNodeSelection([[0, 0, 0], [0, 1, 0]])))).toBeNull()
  })

  it('split is a no-op on a colspan=1 cell', () => {
    expect(cmdSplitCell(st(tableDoc(), nodeSelection([0, 0, 0])))).toBeNull()
  })
})
