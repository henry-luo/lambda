// Tier E — broad HTML-editing coverage.
//
// Lenient runner: asserts the resulting DOCUMENT always, and the selection
// only when output.html carries cursor/anchor/focus markers. This lets a
// large fixture batch focus on document transformations without pinning the
// exact post-edit caret in every case.

import { describe, it, expect } from 'vitest'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { findFixtureDirs, loadFixture, runFixtureCase } from '../helpers/fixture-runner.js'
import { _resetShapeIdCounter } from '../../src/drawing/commands.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const dirs = findFixtureDirs(__dirname)

function isFollowup(dir: string): boolean {
  const np = path.join(dir, 'NOTES.md')
  return fs.existsSync(np) && fs.readFileSync(np, 'utf8').includes('infrastructure follow-up')
}

describe('Tier E — HTML editing fixtures', () => {
  if (dirs.length === 0) {
    it.skip('no fixtures discovered', () => {})
    return
  }
  for (const dir of dirs) {
    const rel = path.relative(__dirname, dir)
    if (isFollowup(dir)) {
      it.skip(`${rel} (infrastructure follow-up)`, () => {})
      continue
    }
    it(rel, () => {
      _resetShapeIdCounter()
      const c = loadFixture(dir)
      const r = runFixtureCase(c)
      expect(r.actualDoc).toEqual(r.expectedDoc)
      // every applied transform must be invertible (Slate/PM invariant)
      if (r.invertRoundtrips !== null) expect(r.invertRoundtrips).toBe(true)
      if (r.expectedSelection !== null) {
        expect(r.actualSelection).toEqual(r.expectedSelection)
      }
    })
  }
})
