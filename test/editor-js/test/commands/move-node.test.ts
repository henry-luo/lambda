import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { nodeSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { serializeDocToHtml } from '../../src/view/html-parser.js'
import { cmdMoveNode } from '../../src/commands/structural-commands.js'
import type { EditorState } from '../../src/commands/types.js'

function st(): EditorState {
  return {
    doc: node('doc', [node('p', [text('A')]), node('p', [text('B')]), node('p', [text('C')])]),
    schema: html5SubsetSchema, selection: nodeSelection([0]), stored_marks: null
  }
}
const html = (d: any) => serializeDocToHtml(d, { schema: html5SubsetSchema })

describe('cmdMoveNode (drag-to-reorder model op)', () => {
  it('moves the first block to the end', () => {
    expect(html(cmdMoveNode(st(), [0], 3)!.doc_after)).toBe('<doc><p>B</p><p>C</p><p>A</p></doc>')
  })
  it('moves the last block to the front', () => {
    expect(html(cmdMoveNode(st(), [2], 0)!.doc_after)).toBe('<doc><p>C</p><p>A</p><p>B</p></doc>')
  })
  it('moves a middle block up', () => {
    expect(html(cmdMoveNode(st(), [1], 0)!.doc_after)).toBe('<doc><p>B</p><p>A</p><p>C</p></doc>')
  })
  it('is a no-op when dropped on its own slot', () => {
    expect(cmdMoveNode(st(), [0], 0)).toBeNull()
    expect(cmdMoveNode(st(), [0], 1)).toBeNull()
  })
})
