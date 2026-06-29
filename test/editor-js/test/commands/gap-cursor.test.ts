import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection, gapSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { serializeDocToHtml } from '../../src/view/html-parser.js'
import { cmdMoveCaret } from '../../src/commands/caret.js'
import {
  cmdInsertText, cmdInsertParagraph, cmdDeleteForward, cmdDeleteBackward
} from '../../src/commands/text-commands.js'
import type { EditorState } from '../../src/commands/types.js'
import type { Selection } from '../../src/model/types.js'

function st(blocks: any[], sel: Selection): EditorState {
  return { doc: node('doc', blocks), schema: html5SubsetSchema, selection: sel, stored_marks: null }
}
const html = (tx: { doc_after: any }) => serializeDocToHtml(tx.doc_after, { schema: html5SubsetSchema })
const hr = () => node('hr', [])
const p = (t: string) => node('p', [text(t)])

describe('gap cursor', () => {
  it('ArrowRight from end of a paragraph before an <hr> lands on a gap', () => {
    const tx = cmdMoveCaret(st([p('hi'), hr(), p('bye')], textSelection(pos([0, 0], 2), pos([0, 0], 2))), 'move', 'forward', 'character')!
    expect(tx.sel_after).toEqual({ kind: 'gap', path: [1] })
  })

  it('typing at a gap inserts a paragraph there, caret inside', () => {
    const tx = cmdInsertText(st([p('hi'), hr(), p('bye')], gapSelection([1])), 'X')!
    expect(html(tx)).toBe('<doc><p>hi</p><p>X</p><hr><p>bye</p></doc>')
    expect(tx.sel_after).toEqual({ kind: 'text', anchor: pos([1, 0], 1), head: pos([1, 0], 1) })
  })

  it('Enter at a gap inserts an empty paragraph', () => {
    const tx = cmdInsertParagraph(st([p('hi'), hr()], gapSelection([1])))!
    expect(html(tx)).toBe('<doc><p>hi</p><p></p><hr></doc>')
  })

  it('deleteForward at a gap removes the next block atom', () => {
    const tx = cmdDeleteForward(st([p('hi'), hr(), p('bye')], gapSelection([1])))!
    expect(html(tx)).toBe('<doc><p>hi</p><p>bye</p></doc>')
  })

  it('deleteBackward at a trailing gap removes the previous block atom', () => {
    const tx = cmdDeleteBackward(st([p('hi'), hr()], gapSelection([2])))!
    expect(html(tx)).toBe('<doc><p>hi</p></doc>')
  })

  it('an inline <img> in a paragraph is NOT a gap (keeps a text/node stop)', () => {
    const tx = cmdMoveCaret(st([node('p', [text('a'), node('img', []), text('b')])], textSelection(pos([0, 0], 1), pos([0, 0], 1))), 'move', 'forward', 'character')!
    expect(tx.sel_after?.kind).toBe('text')
  })
})
