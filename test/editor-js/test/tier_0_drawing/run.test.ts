// Tier-0 runner: every fixture under `test/tier_0_drawing/` (recursive) gets
// loaded + executed + diffed against output.html.

import { describe, it, expect } from 'vitest'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { findFixtureDirs, loadFixture, runFixtureCase } from '../helpers/fixture-runner.js'
import { _resetShapeIdCounter } from '../../src/drawing/commands.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

const dirs = findFixtureDirs(__dirname)

describe('Tier 0 — drawing fixtures', () => {
  if (dirs.length === 0) {
    it.skip('no fixtures discovered', () => {})
    return
  }
  for (const dir of dirs) {
    const rel = path.relative(__dirname, dir)
    it(rel, () => {
      _resetShapeIdCounter()
      const c = loadFixture(dir)
      const r = runFixtureCase(c)
      expect(r.actualDoc).toEqual(r.expectedDoc)
      expect(r.actualSelection).toEqual(r.expectedSelection)
    })
  }
})
