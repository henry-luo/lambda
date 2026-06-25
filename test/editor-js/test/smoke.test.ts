import { describe, it, expect } from 'vitest'
import { VERSION } from '../src/editor'

describe('editor-js bootstrap smoke', () => {
  it('jsdom environment provides document + window', () => {
    expect(typeof document).toBe('object')
    expect(typeof window).toBe('object')
    expect(document.createElement('div')).toBeTruthy()
  })

  it('library entry exports VERSION', () => {
    expect(VERSION).toBe('0.1.0')
  })

  it('DOM can parse a custom-element fixture skeleton', () => {
    const html = '<doc><p>w<cursor></cursor>ord</p></doc>'
    const tpl = document.createElement('template')
    tpl.innerHTML = html
    const doc = tpl.content.querySelector('doc')
    const p = doc?.querySelector('p')
    const cursor = p?.querySelector('cursor')
    expect(doc).not.toBeNull()
    expect(p).not.toBeNull()
    expect(cursor).not.toBeNull()
    expect(p?.textContent).toBe('word')
  })
})
