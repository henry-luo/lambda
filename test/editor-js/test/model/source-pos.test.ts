import { describe, it, expect } from 'vitest'
import { node, text } from '../../src/model/doc.js'
import { resolvedParentTag } from '../helpers/narrow.js'
import {
  allSelection,
  multiNodeSelection,
  nodeSelection,
  pathCompare,
  pathEqual,
  pathIsPrefix,
  pos,
  posCompare,
  posEqual,
  posMax,
  posMin,
  resolveAfter,
  resolveBefore,
  resolvePos,
  selectionToString,
  textSelection
} from '../../src/model/source-pos.js'

describe('model/source-pos — path comparison', () => {
  it('pathCompare lexicographic', () => {
    expect(pathCompare([0, 1], [0, 2])).toBe(-1)
    expect(pathCompare([0, 2], [0, 1])).toBe(1)
    expect(pathCompare([0, 1], [0, 1])).toBe(0)
    expect(pathCompare([0], [0, 0])).toBe(-1)  // ancestor < descendant
    expect(pathCompare([0, 0], [0])).toBe(1)
    expect(pathCompare([], [0])).toBe(-1)
  })

  it('pathEqual', () => {
    expect(pathEqual([1, 2], [1, 2])).toBe(true)
    expect(pathEqual([], [])).toBe(true)
    expect(pathEqual([1], [1, 0])).toBe(false)
  })

  it('pathIsPrefix', () => {
    expect(pathIsPrefix([], [1, 2, 3])).toBe(true)
    expect(pathIsPrefix([1, 2], [1, 2, 3])).toBe(true)
    expect(pathIsPrefix([1, 2, 3], [1, 2, 3])).toBe(true)
    expect(pathIsPrefix([1, 3], [1, 2, 3])).toBe(false)
    expect(pathIsPrefix([1, 2, 3, 4], [1, 2, 3])).toBe(false)
  })
})

describe('model/source-pos — pos arithmetic', () => {
  it('posCompare prioritizes path then offset', () => {
    expect(posCompare(pos([0], 0), pos([0], 1))).toBe(-1)
    expect(posCompare(pos([0], 1), pos([1], 0))).toBe(-1)
    expect(posEqual(pos([0, 0], 3), pos([0, 0], 3))).toBe(true)
    expect(posMin(pos([0], 5), pos([0], 3))).toEqual(pos([0], 3))
    expect(posMax(pos([0], 5), pos([0], 3))).toEqual(pos([0], 5))
  })
})

describe('model/source-pos — resolvePos', () => {
  const doc = node('doc', [
    node('p', [text('hello')]),
    node('p', [text('world')])
  ])

  it('resolves the root', () => {
    const r = resolvePos(doc, pos([], 0))
    expect(r.found).toBe(true)
    expect(r.node).toBe(doc)
    expect(r.parent).toBeNull()
    expect(r.depth).toBe(0)
  })

  it('resolves a leaf with parent', () => {
    const r = resolvePos(doc, pos([0, 0], 2))
    expect(r.found).toBe(true)
    expect(r.node?.kind).toBe('text')
    expect(r.parent?.kind).toBe('node')
    expect(resolvedParentTag(r)).toBe('p')
    expect(r.parent_index).toBe(0)
    expect(r.depth).toBe(2)
    expect(r.ancestors).toHaveLength(3)
  })

  it('returns found=false for off-tree paths', () => {
    const r = resolvePos(doc, pos([0, 99], 0))
    expect(r.found).toBe(false)
  })

  it('resolveBefore / resolveAfter at depth 0 give doc boundaries', () => {
    const r = resolvePos(doc, pos([0, 0], 0))
    expect(resolveBefore(r, 0)).toEqual(pos([], 0))
    expect(resolveAfter(r, 0)).toEqual(pos([], 2))
  })

  it('resolveBefore at intermediate depth points before the ancestor', () => {
    const r = resolvePos(doc, pos([1, 0], 0))
    expect(resolveBefore(r, 1)).toEqual(pos([], 1))  // before the second <p>
    expect(resolveAfter(r, 1)).toEqual(pos([], 2))
  })
})

describe('model/source-pos — selectionToString', () => {
  const doc = node('doc', [
    node('p', [text('hello')]),
    node('p', [text('world')])
  ])

  it('AllSelection returns full doc text', () => {
    expect(selectionToString(doc, allSelection())).toBe('helloworld')
  })

  it('NodeSelection returns the node\'s text', () => {
    expect(selectionToString(doc, nodeSelection([1]))).toBe('world')
  })

  it('TextSelection — same leaf', () => {
    const sel = textSelection(pos([0, 0], 1), pos([0, 0], 4))
    expect(selectionToString(doc, sel)).toBe('ell')
  })

  it('TextSelection — across leaves', () => {
    const sel = textSelection(pos([0, 0], 3), pos([1, 0], 3))
    expect(selectionToString(doc, sel)).toBe('lowor')
  })

  it('MultiNodeSelection concatenates selected node texts', () => {
    const sel = multiNodeSelection([[0], [1]])
    expect(selectionToString(doc, sel)).toBe('helloworld')
  })
})
