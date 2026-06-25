import { beforeEach, describe, expect, it } from 'vitest'
import {
  domBoundaryToSourcePos,
  findElementByPath,
  parsePath,
  pathOf,
  SOURCE_PATH_ATTR,
  sourcePosToDomBoundary,
  stringifyPath
} from '../../src/view/dom-bridge.js'

describe('view/dom-bridge — path serialization', () => {
  it('round-trips', () => {
    expect(stringifyPath([])).toBe('')
    expect(parsePath('')).toEqual([])
    expect(stringifyPath([0, 1, 2])).toBe('0,1,2')
    expect(parsePath('0,1,2')).toEqual([0, 1, 2])
  })
})

describe('view/dom-bridge — DOM helpers', () => {
  let root: HTMLDivElement

  beforeEach(() => {
    root = document.createElement('div')
    root.setAttribute(SOURCE_PATH_ATTR, '')  // doc root
    root.innerHTML = `
      <p data-source-path="0">
        <span data-source-path="0,0">hello</span>
      </p>
      <p data-source-path="1">
        <span data-source-path="1,0">world</span>
      </p>
    `.trim()
    document.body.appendChild(root)
  })

  it('findElementByPath finds the right element', () => {
    expect(findElementByPath(root, [0])?.tagName).toBe('P')
    expect(findElementByPath(root, [0, 0])?.tagName).toBe('SPAN')
    expect(findElementByPath(root, [99])).toBeNull()
  })

  it('pathOf reads data-source-path', () => {
    const el = findElementByPath(root, [0, 0])!
    expect(pathOf(el)).toEqual([0, 0])
  })

  it('domBoundaryToSourcePos: text node boundary → source pos', () => {
    const span = findElementByPath(root, [1, 0])!
    const textNode = span.firstChild as Text
    const r = domBoundaryToSourcePos({ node: textNode, offset: 3 })
    expect(r).toEqual({ path: [1, 0], offset: 3 })
  })

  it('domBoundaryToSourcePos: element boundary → source pos', () => {
    const p = findElementByPath(root, [0])!
    const r = domBoundaryToSourcePos({ node: p, offset: 0 })
    expect(r).toEqual({ path: [0], offset: 0 })
  })

  it('sourcePosToDomBoundary: into a text leaf wrapper', () => {
    const r = sourcePosToDomBoundary(root, { path: [0, 0], offset: 2 })
    expect(r).not.toBeNull()
    expect(r!.node.nodeType).toBe(3)  // TEXT_NODE
    expect((r!.node as Text).data).toBe('hello')
    expect(r!.offset).toBe(2)
  })

  it('sourcePosToDomBoundary: clamps over-offset', () => {
    const r = sourcePosToDomBoundary(root, { path: [0, 0], offset: 999 })
    expect(r!.offset).toBe(5)
  })
})
