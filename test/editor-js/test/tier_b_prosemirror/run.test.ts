// Tier-B runner. Same auto-discovery pattern as Tier A.

import { describe, it, expect } from 'vitest'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { findFixtureDirs, loadFixture, runFixtureCase } from '../helpers/fixture-runner.js'
import { _resetShapeIdCounter } from '../../src/drawing/commands.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

const dirs = findFixtureDirs(__dirname)

describe('Tier B — ProseMirror-derived fixtures', () => {
  if (dirs.length === 0) {
    it.skip('no fixtures discovered', () => {})
    return
  }
  for (const dir of dirs) {
    const rel = path.relative(__dirname, dir)
    // Skip fixtures whose NOTES.md flags them as an "infrastructure
    // follow-up" — they're authored placeholders, not yet runnable.
    if (isInfrastructureFollowup(dir)) {
      it.skip(`${rel} (infrastructure follow-up)`, () => {})
      continue
    }
    it(rel, () => {
      _resetShapeIdCounter()
      const c = loadFixture(dir)
      const r = runFixtureCase(c)
      expect(r.actualDoc).toEqual(r.expectedDoc)
      // Selection equality is checked only when expected is explicit
      if (r.expectedSelection !== null) {
        expect(r.actualSelection).toEqual(r.expectedSelection)
      }
    })
  }
})

function isInfrastructureFollowup(dir: string): boolean {
  try {
    const fs = require('node:fs') as typeof import('node:fs')
    const np = path.join(dir, 'NOTES.md')
    if (!fs.existsSync(np)) return false
    return fs.readFileSync(np, 'utf8').includes('infrastructure follow-up')
  } catch { return false }
}
