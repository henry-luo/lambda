import { describe, it, expect } from 'vitest'
import {
  attrsGet,
  attrsSet,
  docText,
  isNode,
  isText,
  lastIndex,
  listConcat,
  listDrop,
  listSet,
  listSplice,
  listTake,
  node,
  nodeAt,
  nodeAttrs,
  parentPath,
  replaceNodeAt,
  spliceChildrenAt,
  text,
  textMarked,
  withContent
} from '../../src/model/doc.js'
import { tagAt, textAt } from '../helpers/narrow.js'

describe('model/doc — constructors', () => {
  it('text() creates a text leaf with empty marks dict', () => {
    expect(text('hello')).toEqual({ kind: 'text', text: 'hello', marks: {} })
  })

  it('textMarked() preserves marks', () => {
    expect(textMarked('hi', { bold: true, italic: true }))
      .toEqual({ kind: 'text', text: 'hi', marks: { bold: true, italic: true } })
  })

  it('node() and nodeAttrs() create nodes', () => {
    const n = node('p', [text('hi')])
    expect(n.kind).toBe('node')
    expect(n.tag).toBe('p')
    expect(n.attrs).toEqual([])
    expect(n.content).toHaveLength(1)

    const h = nodeAttrs('heading', [{ name: 'level', value: 2 }], [text('Title')])
    expect(h.attrs).toEqual([{ name: 'level', value: 2 }])
  })
})

describe('model/doc — predicates', () => {
  it('isText/isNode discriminate', () => {
    expect(isText(text('x'))).toBe(true)
    expect(isText(node('p', []))).toBe(false)
    expect(isNode(node('p', []))).toBe(true)
    expect(isNode(text('x'))).toBe(false)
    expect(isText(null)).toBe(false)
    expect(isNode(undefined)).toBe(false)
  })
})

describe('model/doc — list ops are immutable', () => {
  it('listTake/Drop/Concat/Set/Splice produce new arrays', () => {
    const a = [1, 2, 3, 4, 5]
    expect(listTake(a, 2)).toEqual([1, 2])
    expect(listDrop(a, 2)).toEqual([3, 4, 5])
    expect(listConcat([1, 2], [3, 4])).toEqual([1, 2, 3, 4])
    expect(listSet(a, 1, 99)).toEqual([1, 99, 3, 4, 5])
    expect(a).toEqual([1, 2, 3, 4, 5])  // unchanged
    expect(listSplice([1, 2, 3, 4], 1, 2, [9, 8])).toEqual([1, 9, 8, 4])
  })
})

describe('model/doc — navigation', () => {
  const doc = node('doc', [
    node('p', [text('hello')]),
    node('p', [text('world')])
  ])

  it('nodeAt walks paths', () => {
    expect(tagAt(doc, [])).toBe('doc')
    expect(tagAt(doc, [0])).toBe('p')
    expect(textAt(doc, [1, 0])).toBe('world')
  })

  it('nodeAt returns null for off-tree paths', () => {
    expect(nodeAt(doc, [99])).toBeNull()
    expect(nodeAt(doc, [0, 0, 99])).toBeNull()
  })

  it('parentPath / lastIndex', () => {
    expect(parentPath([1, 2, 3])).toEqual([1, 2])
    expect(parentPath([])).toEqual([])
    expect(lastIndex([1, 2, 3])).toBe(3)
    expect(lastIndex([])).toBe(-1)
  })
})

describe('model/doc — immutable updates', () => {
  const doc = node('doc', [
    node('p', [text('hello')]),
    node('p', [text('world')])
  ])

  it('replaceNodeAt produces a new doc, original unchanged', () => {
    const updated = replaceNodeAt(doc, [0, 0], text('HELLO'))
    expect(textAt(updated, [0, 0])).toBe('HELLO')
    expect(textAt(doc, [0, 0])).toBe('hello')
  })

  it('replaceNodeAt at root replaces the root', () => {
    const r = node('doc', [node('p', [text('replaced')])])
    expect(replaceNodeAt(doc, [], r)).toBe(r)
  })

  it('spliceChildrenAt inserts/deletes children', () => {
    const inserted = spliceChildrenAt(doc, [], 1, 0, [node('p', [text('inserted')])])
    expect(textAt(inserted, [1, 0])).toBe('inserted')
    expect(inserted.content).toHaveLength(3)

    const deleted = spliceChildrenAt(doc, [], 0, 1, [])
    expect(deleted.content).toHaveLength(1)
    expect(textAt(deleted, [0, 0])).toBe('world')
  })

  it('withContent replaces content while preserving tag + attrs', () => {
    const n = nodeAttrs('p', [{ name: 'class', value: 'x' }], [text('a')])
    const w = withContent(n, [text('b')])
    expect(w.tag).toBe('p')
    expect(w.attrs).toEqual([{ name: 'class', value: 'x' }])
    expect(w.content[0]).toEqual(text('b'))
  })
})

describe('model/doc — attr helpers', () => {
  it('attrsGet/Set roundtrip', () => {
    const a = [{ name: 'level', value: 1 }]
    expect(attrsGet(a, 'level')).toBe(1)
    expect(attrsGet(a, 'missing')).toBeNull()

    const b = attrsSet(a, 'level', 3)
    expect(attrsGet(b, 'level')).toBe(3)
    expect(attrsGet(a, 'level')).toBe(1)
  })

  it('attrsSet appends new attrs', () => {
    const a: { name: string, value: any }[] = []
    const b = attrsSet(a, 'class', 'card')
    expect(b).toEqual([{ name: 'class', value: 'card' }])
  })

  it('attrsSet with value=null removes the entry', () => {
    const a = [{ name: 'level', value: 1 }, { name: 'class', value: 'x' }]
    const b = attrsSet(a, 'level', null)
    expect(b).toEqual([{ name: 'class', value: 'x' }])
  })
})

describe('model/doc — docText', () => {
  it('concatenates text across the tree', () => {
    const d = node('doc', [
      node('p', [text('hello '), text('world')]),
      node('p', [text('!')])
    ])
    expect(docText(d)).toBe('hello world!')
  })
})
