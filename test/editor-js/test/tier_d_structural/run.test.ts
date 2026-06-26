// Tier D — structural editing fixtures (lists, tables, images).
//
// Many of these commands end on a NodeSelection, which has no fixture marker.
// So this runner asserts the resulting DOCUMENT always, but only checks the
// selection when the output.html actually carries cursor/anchor/focus markers
// (same leniency as the ProseMirror tier runner).

import { describe, it, expect } from 'vitest'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { findFixtureDirs, loadFixture, runFixtureCase } from '../helpers/fixture-runner.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const dirs = findFixtureDirs(__dirname)

function isFollowup(dir: string): boolean {
  const np = path.join(dir, 'NOTES.md')
  return fs.existsSync(np) && fs.readFileSync(np, 'utf8').includes('infrastructure follow-up')
}

describe('Tier D — structural fixtures (lists / tables / images)', () => {
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
