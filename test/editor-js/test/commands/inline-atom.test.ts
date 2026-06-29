import { describe, it, expect } from 'vitest'
import { node, nodeAttrs, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { parseHtmlToDoc, serializeDocToHtml } from '../../src/view/html-parser.js'
import { cmdInsertInlineAtom } from '../../src/commands/structural-commands.js'
import { cmdDeleteBackward } from '../../src/commands/text-commands.js'
import { cmdMoveCaret } from '../../src/commands/caret.js'
import type { EditorState } from '../../src/commands/types.js'
import type { Selection } from '../../src/model/types.js'

// The vitest environment is jsdom, so DOMParser (used by parseHtmlToDoc) is global.

function st(blocks: any[], sel: Selection): EditorState {
  return { doc: node('doc', blocks), schema: html5SubsetSchema, selection: sel, stored_marks: null }
}
const html = (d: any) => serializeDocToHtml(d, { schema: html5SubsetSchema })
const mentionAttrs = [{ name: 'id', value: 'u1' }, { name: 'label', value: 'alice' }]

describe('inline atom nodes (mention)', () => {
  it('inserts an inline atom, splitting the leaf, caret after it', () => {
    const tx = cmdInsertInlineAtom(st([node('p', [text('hi world')])], textSelection(pos([0, 0], 3), pos([0, 0], 3))), 'mention', mentionAttrs)!
    expect(html(tx.doc_after)).toBe('<doc><p>hi <mention id="u1" label="alice"></mention>world</p></doc>')
    expect(tx.sel_after).toEqual({ kind: 'text', anchor: pos([0, 2], 0), head: pos([0, 2], 0) })
  })

  it('backspace right after a mention deletes it as one unit (and merges text)', () => {
    const doc = [node('p', [text('hi '), nodeAttrs('mention', mentionAttrs, []), text('world')])]
    const tx = cmdDeleteBackward(st(doc, textSelection(pos([0, 2], 0), pos([0, 2], 0))))!
    expect(html(tx.doc_after)).toBe('<doc><p>hi world</p></doc>')
  })

  it('caret moves across a mention as a single stop (atomic)', () => {
    const doc = [node('p', [text('hi '), nodeAttrs('mention', mentionAttrs, []), text('world')])]
    const tx = cmdMoveCaret(st(doc, textSelection(pos([0, 0], 3), pos([0, 0], 3))), 'move', 'forward', 'character')!
    expect(tx.sel_after).toEqual({ kind: 'text', anchor: pos([0, 1], 0), head: pos([0, 1], 0) })
  })

  it('round-trips through HTML (parse → serialize)', () => {
    const src = '<doc><p>hi <mention id="u1" label="alice"></mention>world</p></doc>'
    const { doc } = parseHtmlToDoc(src, html5SubsetSchema)
    expect(html(doc)).toBe(src)
  })
})
