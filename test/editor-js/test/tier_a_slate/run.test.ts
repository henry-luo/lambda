// Tier-A runner: every fixture under `test/tier_a_slate/` (recursive) gets
// loaded + executed + diffed against output.html. Add a new fixture by
// creating a directory with input.html, events.json, output.html — no test
// code changes required.

import { describe, it, expect } from 'vitest'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { findFixtureDirs, loadFixture, runFixtureCase } from '../helpers/fixture-runner.js'
import { _resetShapeIdCounter } from '../../src/drawing/commands.js'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

const dirs = findFixtureDirs(__dirname)

function isInfrastructureFollowup(dir: string): boolean {
  const np = path.join(dir, 'NOTES.md')
  if (!fs.existsSync(np)) return false
  return fs.readFileSync(np, 'utf8').includes('infrastructure follow-up')
}

describe('Tier A — Slate-derived fixtures', () => {
  if (dirs.length === 0) {
    it.skip('no fixtures discovered', () => {})
    return
  }
  for (const dir of dirs) {
    const rel = path.relative(__dirname, dir)
    if (isInfrastructureFollowup(dir)) {
      it.skip(`${rel} (infrastructure follow-up)`, () => {})
      continue
    }
    it(rel, () => {
      _resetShapeIdCounter()
      const c = loadFixture(dir)
      const r = runFixtureCase(c)
      expect(r.actualDoc).toEqual(r.expectedDoc)
      expect(r.actualSelection).toEqual(r.expectedSelection)
    })
  }
})
