import { describe, it, expect } from 'vitest'
import { node, nodeAttrs, nodeAt, text } from '../../src/model/doc.js'
import { pos, textSelection, nodeSelection } from '../../src/model/source-pos.js'
import { docSchema } from '../../src/schemas/doc.js'
import {
  cmdAddTableColumn,
  cmdAddTableRow,
  cmdDeleteTableColumn,
  cmdDeleteTableRow,
  cmdIndentListItem,
  cmdInsertImage,
  cmdInsertTable,
  cmdOutdentListItem,
  cmdResizeImage,
  cmdSetNodeAttr,
  cmdWrapInList,
  cmdAutoformatList
} from '../../src/commands/structural-commands.js'
import { cmdDeleteNode, cmdInsertParagraph } from '../../src/commands/text-commands.js'
import { attrsGet } from '../../src/model/doc.js'
import type { EditorState } from '../../src/commands/types.js'
import type { Selection } from '../../src/model/types.js'
import { tagAt, textAt } from '../helpers/narrow.js'

function st(blocks: any[], sel: Selection | null): EditorState {
  return { doc: node('doc', blocks), schema: docSchema, selection: sel, stored_marks: null }
}
const caret = (p: number[], o: number) => textSelection(pos(p, o), pos(p, o))

describe('lists — Enter in a list item makes a new <li>', () => {
  it('splits an <li>, second half is also an <li>', () => {
    const s = st([node('ul', [node('li', [text('hello')])])], caret([0, 0, 0], 2))
    const tx = cmdInsertParagraph(s)!
    const ul = nodeAt(tx.doc_after, [0])!
    expect((ul as any).tag).toBe('ul')
    expect((ul as any).content.length).toBe(2)
    expect(tagAt(tx.doc_after, [0, 0])).toBe('li')
    expect(tagAt(tx.doc_after, [0, 1])).toBe('li')
    expect(textAt(tx.doc_after, [0, 0, 0])).toBe('he')
    expect(textAt(tx.doc_after, [0, 1, 0])).toBe('llo')
  })
})

describe('cmdWrapInList', () => {
  it('wraps a paragraph into a single-item list', () => {
    const s = st([node('p', [text('item')])], caret([0, 0], 2))
    const tx = cmdWrapInList(s, 'ul')!
    expect(tagAt(tx.doc_after, [0])).toBe('ul')
    expect(tagAt(tx.doc_after, [0, 0])).toBe('li')
    expect(textAt(tx.doc_after, [0, 0, 0])).toBe('item')
    // caret follows into the li
    expect(tx.sel_after).toEqual(caret([0, 0, 0], 2))
  })
  it('ordered variant uses <ol>', () => {
    const s = st([node('p', [text('x')])], caret([0, 0], 0))
    const tx = cmdWrapInList(s, 'ol')!
    expect(tagAt(tx.doc_after, [0])).toBe('ol')
  })
  it('no-op on an existing list', () => {
    const s = st([node('ul', [node('li', [text('x')])])], caret([0, 0, 0], 0))
    expect(cmdWrapInList(s, 'ul')).toBeNull()
  })
})

describe('cmdIndentListItem / cmdOutdentListItem', () => {
  it('indent sets an indent level on the item (flat model, no nesting)', () => {
    const s = st([node('ul', [node('li', [text('a')]), node('li', [text('b')])])], caret([0, 1, 0], 0))
    const tx = cmdIndentListItem(s)!
    const ul = nodeAt(tx.doc_after, [0]) as any
    expect(ul.content.length).toBe(2)             // flat: both items stay at top level
    expect(attrsGet(ul.content[1].attrs, 'indent')).toBe(1)
    expect(ul.content[1].content[0].text).toBe('b')
  })

  it('indent works from any caret position and leaves the caret untouched', () => {
    // caret in the middle of "bc" — only an attribute changes, so the caret stays put
    const s = st([node('ul', [node('li', [text('a')]), node('li', [text('bc')])])], caret([0, 1, 0], 1))
    const tx = cmdIndentListItem(s)!
    expect(tx.sel_after).toEqual(caret([0, 1, 0], 1))
    expect(attrsGet((nodeAt(tx.doc_after, [0, 1]) as any).attrs, 'indent')).toBe(1)
  })

  it('indents the first item and repeats (Word/Docs style)', () => {
    const s = st([node('ul', [node('li', [text('a')]), node('li', [text('b')])])], caret([0, 0, 0], 0))
    const t1 = cmdIndentListItem(s)!
    expect(attrsGet((nodeAt(t1.doc_after, [0, 0]) as any).attrs, 'indent')).toBe(1)
    const s2 = { ...s, doc: t1.doc_after, selection: t1.sel_after }
    const t2 = cmdIndentListItem(s2)!
    expect(attrsGet((nodeAt(t2.doc_after, [0, 0]) as any).attrs, 'indent')).toBe(2)
  })

  it('indent then outdent round-trips (caret preserved both ways)', () => {
    const s = st([node('ul', [node('li', [text('a')]), node('li', [text('b')])])], caret([0, 1, 0], 0))
    const tx1 = cmdIndentListItem(s)!
    const s2 = { ...s, doc: tx1.doc_after, selection: tx1.sel_after }
    const tx2 = cmdOutdentListItem(s2)!
    expect(tx2.doc_after).toEqual(node('doc', [node('ul', [node('li', [text('a')]), node('li', [text('b')])])]))
  })

  it('outdent reverses indent (round-trip)', () => {
    const nested = node('ul', [
      nodeAttrs('li', [], [text('a'), node('ul', [node('li', [text('b')])])])
    ])
    const s = st([nested], caret([0, 0, 1, 0, 0], 0))
    const tx = cmdOutdentListItem(s)!
    const ul = nodeAt(tx.doc_after, [0]) as any
    expect(ul.content.length).toBe(2)
    expect(ul.content[0].content[0].text).toBe('a')
    expect(ul.content[1].content[0].text).toBe('b')
  })
})

describe('cmdInsertImage + node delete', () => {
  it('inserts an inline image and selects it as a node', () => {
    const s = st([node('p', [text('abcd')])], caret([0, 0], 2))
    const tx = cmdInsertImage(s, 'x.png', 'alt')!
    const p = nodeAt(tx.doc_after, [0]) as any
    expect(p.content.length).toBe(3)       // "ab", <img>, "cd"
    expect(p.content[1].tag).toBe('img')
    expect(tx.sel_after).toEqual(nodeSelection([0, 1]))
  })

  it('deleteNode removes the selected image', () => {
    const s0 = st([node('p', [text('ab'), nodeAttrs('img', [{ name: 'src', value: 'x' }], []), text('cd')])], nodeSelection([0, 1]))
    const tx = cmdDeleteNode(s0)!
    const p = nodeAt(tx.doc_after, [0]) as any
    expect(p.content.length).toBe(2)
    expect(p.content.every((c: any) => c.kind === 'text')).toBe(true)
  })
})

// Regression guard for bug 3 (image resize)
describe('cmdResizeImage / cmdSetNodeAttr', () => {
  const imgDoc = () => st(
    [node('p', [nodeAttrs('img', [{ name: 'src', value: 'x.png' }, { name: 'alt', value: 'a' }], [])])],
    nodeSelection([0, 0])
  )

  it('cmdResizeImage sets width + height (rounded) in one transaction', () => {
    const tx = cmdResizeImage(imgDoc(), [0, 0], 180.4, 90.6)!
    const img = nodeAt(tx.doc_after, [0, 0]) as any
    expect(attrsGet(img.attrs, 'width')).toBe(180)
    expect(attrsGet(img.attrs, 'height')).toBe(91)
    expect(tx.steps.length).toBe(2)
    // existing attrs preserved
    expect(attrsGet(img.attrs, 'src')).toBe('x.png')
    expect(tx.sel_after).toEqual(nodeSelection([0, 0]))
  })

  it('cmdResizeImage updates existing width/height in place', () => {
    const s = st(
      [node('p', [nodeAttrs('img', [{ name: 'src', value: 'x' }, { name: 'width', value: 200 }, { name: 'height', value: 100 }], [])])],
      nodeSelection([0, 0])
    )
    const tx = cmdResizeImage(s, [0, 0], 50, 25)!
    const img = nodeAt(tx.doc_after, [0, 0]) as any
    expect(attrsGet(img.attrs, 'width')).toBe(50)
    expect(attrsGet(img.attrs, 'height')).toBe(25)
  })

  it('cmdSetNodeAttr sets an arbitrary attribute', () => {
    const tx = cmdSetNodeAttr(imgDoc(), [0, 0], 'alt', 'new alt')!
    const img = nodeAt(tx.doc_after, [0, 0]) as any
    expect(attrsGet(img.attrs, 'alt')).toBe('new alt')
  })

  it('returns null when the path is not a node', () => {
    const s = st([node('p', [text('x')])], nodeSelection([0, 0]))
    expect(cmdResizeImage(s, [0, 0], 10, 10)).toBeNull()  // [0,0] is a text leaf
  })
})

describe('tables', () => {
  const tableDoc = () => st(
    [node('p', [text('before')]),
     node('table', [
       node('tr', [node('th', [text('A')]), node('th', [text('B')])]),
       node('tr', [node('td', [text('1')]), node('td', [text('2')])])
     ])],
    caret([1, 1, 0, 0], 0)  // caret in cell (row 1, col 0)
  )

  it('cmdInsertTable inserts a table after the current block', () => {
    const s = st([node('p', [text('x')])], caret([0, 0], 0))
    const tx = cmdInsertTable(s, 2, 3, true)!
    expect(tagAt(tx.doc_after, [1])).toBe('table')
    const table = nodeAt(tx.doc_after, [1]) as any
    expect(table.content.length).toBe(2)            // 2 rows
    expect(table.content[0].content.length).toBe(3) // 3 cols
    expect(table.content[0].content[0].tag).toBe('th') // header row
    expect(table.content[1].content[0].tag).toBe('td')
  })

  it('cmdAddTableRow adds a row below', () => {
    const tx = cmdAddTableRow(tableDoc())!
    const table = nodeAt(tx.doc_after, [1]) as any
    expect(table.content.length).toBe(3)
    expect(table.content[2].content.length).toBe(2)
  })

  it('cmdDeleteTableRow removes the current row', () => {
    const tx = cmdDeleteTableRow(tableDoc())!
    const table = nodeAt(tx.doc_after, [1]) as any
    expect(table.content.length).toBe(1)
  })

  it('cmdAddTableColumn adds a cell to every row', () => {
    const tx = cmdAddTableColumn(tableDoc())!
    const table = nodeAt(tx.doc_after, [1]) as any
    expect(table.content[0].content.length).toBe(3)
    expect(table.content[1].content.length).toBe(3)
    // header row's new cell is a <th>, body's is a <td>
    expect(table.content[0].content[2].tag).toBe('th')
    expect(table.content[1].content[2].tag).toBe('td')
  })

  it('cmdDeleteTableColumn removes a cell from every row', () => {
    const tx = cmdDeleteTableColumn(tableDoc())!
    const table = nodeAt(tx.doc_after, [1]) as any
    expect(table.content[0].content.length).toBe(1)
    expect(table.content[1].content.length).toBe(1)
  })

  it('cmdDeleteTableRow no-ops when only one row remains', () => {
    const s = st([node('table', [node('tr', [node('td', [text('only')])])])], caret([0, 0, 0, 0], 0))
    expect(cmdDeleteTableRow(s)).toBeNull()
  })
})

describe('cmdAutoformatList', () => {
  it('"- " turns a paragraph into a bullet list', () => {
    const s = st([node('p', [text('-')])], caret([0, 0], 1))
    const tx = cmdAutoformatList(s)!
    expect((nodeAt(tx.doc_after, [0]) as any).tag).toBe('ul')
    expect((nodeAt(tx.doc_after, [0, 0]) as any).tag).toBe('li')
    expect((nodeAt(tx.doc_after, [0, 0]) as any).content).toEqual([])
  })

  it('"1." turns a paragraph into an ordered list', () => {
    const s = st([node('p', [text('1.')])], caret([0, 0], 2))
    const tx = cmdAutoformatList(s)!
    expect((nodeAt(tx.doc_after, [0]) as any).tag).toBe('ol')
  })

  it('"*" and "+" also start a bullet list; "42." an ordered list', () => {
    for (const m of ['*', '+']) {
      const s = st([node('p', [text(m)])], caret([0, 0], m.length))
      expect((nodeAt(cmdAutoformatList(s)!.doc_after, [0]) as any).tag).toBe('ul')
    }
    const s = st([node('p', [text('42.')])], caret([0, 0], 3))
    expect((nodeAt(cmdAutoformatList(s)!.doc_after, [0]) as any).tag).toBe('ol')
  })

  it('does not fire when the block has more than the marker', () => {
    const s = st([node('p', [text('-x')])], caret([0, 0], 2))
    expect(cmdAutoformatList(s)).toBeNull()
  })

  it('does not fire inside an existing list item', () => {
    const s = st([node('ul', [node('li', [text('-')])])], caret([0, 0, 0], 1))
    expect(cmdAutoformatList(s)).toBeNull()
  })
})
