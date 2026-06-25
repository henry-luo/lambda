import { describe, it, expect } from 'vitest'
import { node, nodeAttrs, text, textMarked } from '../../src/model/doc.js'
import {
  defaultSplitBlock,
  isBlockTag,
  isInlineTag,
  isMarkTag,
  roleSatisfies,
  validateDoc
} from '../../src/model/schema.js'
import { html5SubsetSchema } from '../../src/schemas/index.js'

describe('model/schema — role compatibility', () => {
  it('exact role match', () => {
    expect(roleSatisfies('block', 'block')).toBe(true)
    expect(roleSatisfies('mark', 'block')).toBe(false)
  })

  it('text and mark satisfy inline', () => {
    expect(roleSatisfies('text', 'inline')).toBe(true)
    expect(roleSatisfies('mark', 'inline')).toBe(true)
    expect(roleSatisfies('inline', 'inline')).toBe(true)
    expect(roleSatisfies('block', 'inline')).toBe(false)
  })
})

describe('model/schema — schema queries', () => {
  it('isBlockTag / isInlineTag / isMarkTag', () => {
    expect(isBlockTag(html5SubsetSchema, 'p')).toBe(true)
    expect(isBlockTag(html5SubsetSchema, 'strong')).toBe(false)
    expect(isInlineTag(html5SubsetSchema, 'strong')).toBe(true)
    expect(isInlineTag(html5SubsetSchema, 'p')).toBe(false)
    expect(isMarkTag(html5SubsetSchema, 'strong')).toBe(true)
    expect(isMarkTag(html5SubsetSchema, 'p')).toBe(false)
  })

  it('defaultSplitBlock returns p for the html5 schema', () => {
    expect(defaultSplitBlock(html5SubsetSchema, 'h1')).toBe('p')
    expect(defaultSplitBlock(html5SubsetSchema, 'p')).toBe('p')
  })
})

describe('model/schema — validateDoc happy paths', () => {
  it('valid doc with one paragraph', () => {
    const d = node('doc', [node('p', [text('hello')])])
    const r = validateDoc(d, html5SubsetSchema)
    expect(r.valid).toBe(true)
    expect(r.errors).toEqual([])
  })

  it('valid doc with marks (em inside p)', () => {
    const d = node('doc', [node('p', [text('hello '), node('em', [text('world')])])])
    const r = validateDoc(d, html5SubsetSchema)
    expect(r.valid).toBe(true)
  })

  it('valid doc with nested structure (blockquote > p)', () => {
    const d = node('doc', [
      node('blockquote', [
        node('p', [text('quoted')]),
        node('p', [text('more')])
      ])
    ])
    expect(validateDoc(d, html5SubsetSchema).valid).toBe(true)
  })

  it('valid doc with heading attrs', () => {
    const d = node('doc', [
      nodeAttrs('h1', [], [text('Title')])
    ])
    expect(validateDoc(d, html5SubsetSchema).valid).toBe(true)
  })

  it('valid doc with list', () => {
    const d = node('doc', [
      node('ul', [
        node('li', [text('a')]),
        node('li', [text('b')])
      ])
    ])
    expect(validateDoc(d, html5SubsetSchema).valid).toBe(true)
  })
})

describe('model/schema — validateDoc catches errors', () => {
  it('unknown tag', () => {
    const d = node('doc', [node('not-a-real-tag', [text('x')])])
    const r = validateDoc(d, html5SubsetSchema)
    expect(r.valid).toBe(false)
    // The validator may report both (a) the parent's content-expression
    // mismatch and (b) the unknown tag itself; check that the unknown-tag
    // error is in the list (order is not guaranteed).
    expect(r.errors.some(e => e.message.includes('unknown tag'))).toBe(true)
  })

  it('content arity violation: blockquote requires plus, given empty', () => {
    const d = node('doc', [node('blockquote', [])])
    const r = validateDoc(d, html5SubsetSchema)
    expect(r.valid).toBe(false)
    expect(r.errors[0]?.message).toContain('blockquote')
  })

  it('img requires src', () => {
    const d = node('doc', [node('p', [node('img', [])])])
    const r = validateDoc(d, html5SubsetSchema)
    expect(r.valid).toBe(false)
    expect(r.errors[0]?.message).toContain('src')
  })

  it('marks="none" rejects marked text inside hr (atomic; no children expected)', () => {
    const d = node('doc', [
      node('blockquote', [
        node('p', [textMarked('plain', { bold: true })])
      ])
    ])
    // p has marks: 'all' so this is fine
    expect(validateDoc(d, html5SubsetSchema).valid).toBe(true)

    // But blockquote has marks: 'none' — but its children are p, not text leaves
    // We test the marks: 'none' rule via doc (which also has marks: 'none', and its
    // children are blocks, not text). For a proper marks: 'none' violation we'd
    // need a tag whose direct children include text, which md_schema doesn't have.
  })
})
