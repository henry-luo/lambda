import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { pos, textSelection } from '../../src/model/source-pos.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'
import { serializeDocToHtml } from '../../src/view/html-parser.js'
import { dispatchIntent } from '../../src/input/intent.js'
import type { EditorState } from '../../src/commands/types.js'

// Type one character at a collapsed caret through the input pipeline and return
// the resulting HTML, or null if no transaction was produced.
function type1(blocks: any[], path: number[], offset: number, char: string): string | null {
  const st: EditorState = {
    doc: node('doc', blocks),
    schema: html5SubsetSchema,
    selection: textSelection(pos(path, offset), pos(path, offset)),
    stored_marks: null
  }
  const tx = dispatchIntent(st, { type: 'insertText', text: char })
  return tx === null ? null : serializeDocToHtml(tx.doc_after, { schema: html5SubsetSchema })
}

describe('input rules (markdown autoformat)', () => {
  it('# + space → h1', () =>
    expect(type1([node('p', [text('#')])], [0, 0], 1, ' ')).toBe('<doc><h1></h1></doc>'))
  it('### + space → h3', () =>
    expect(type1([node('p', [text('###')])], [0, 0], 3, ' ')).toBe('<doc><h3></h3></doc>'))
  it('> + space → blockquote', () =>
    expect(type1([node('p', [text('>')])], [0, 0], 1, ' ')).toBe('<doc><blockquote><p></p></blockquote></doc>'))
  it('- + space → bullet list (existing autoformat still works)', () =>
    expect(type1([node('p', [text('-')])], [0, 0], 1, ' ')).toBe('<doc><ul><li></li></ul></doc>'))
  it('-- + "-" → hr', () =>
    expect(type1([node('p', [text('--')])], [0, 0], 2, '-')).toBe('<doc><hr><p></p></doc>'))
  it('**bold* + "*" → bold', () =>
    expect(type1([node('p', [text('x **bold*')])], [0, 0], 9, '*'))
      .toBe('<doc><p>x <span style="font-weight: bold">bold</span></p></doc>'))
  it('*ital + "*" → italic', () =>
    expect(type1([node('p', [text('a *ital')])], [0, 0], 7, '*'))
      .toBe('<doc><p>a <span style="font-style: italic">ital</span></p></doc>'))
  it('`code + "`" → code', () =>
    expect(type1([node('p', [text('a `code')])], [0, 0], 7, '`'))
      .toBe('<doc><p>a <code>code</code></p></doc>'))
  it('~~s~ + "~" → strikethrough', () =>
    expect(type1([node('p', [text('a ~~s~')])], [0, 0], 6, '~'))
      .toBe('<doc><p>a <span style="text-decoration: line-through">s</span></p></doc>'))
  it('plain char → no rule, normal insert', () =>
    expect(type1([node('p', [text('x')])], [0, 0], 1, 'h')).toBe('<doc><p>xh</p></doc>'))
})
