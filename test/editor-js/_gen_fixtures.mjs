// One-shot fixture generator for editor-js Tier B and Tier 0 fixtures.
// Run with: node _gen_fixtures.mjs

import fs from 'node:fs'
import path from 'node:path'

const ROOT = path.resolve('.')
const T_B_CURSOR  = path.join(ROOT, 'test/tier_b_prosemirror/state/cursor')
const T_B_RANGE   = path.join(ROOT, 'test/tier_b_prosemirror/state/range')
const T_0_MOVE    = path.join(ROOT, 'test/tier_0_drawing/move_shape/coords')
const T_0_SETATTR = path.join(ROOT, 'test/tier_0_drawing/setattr/colors')

function ensureDir(p) { fs.mkdirSync(p, { recursive: true }) }
function writeFix(dir, input, events, output) {
  ensureDir(dir)
  fs.writeFileSync(path.join(dir, 'input.html'),  input)
  fs.writeFileSync(path.join(dir, 'events.json'), JSON.stringify(events, null, 2) + '\n')
  fs.writeFileSync(path.join(dir, 'output.html'), output)
}

function selEvent(a, b) {
  return [{
    type: 'set_selection',
    selection: {
      kind: 'text',
      anchor: { path: [0, 0], offset: a },
      head:   { path: [0, 0], offset: b }
    }
  }]
}

function cursorInputHtml(text, tag = 'p') {
  return `<doc><${tag}>${text}<cursor></cursor></${tag}></doc>\n`
}

function cursorOutputHtml(text, offset, tag = 'p') {
  const a = text.slice(0, offset)
  const b = text.slice(offset)
  return `<doc><${tag}>${a}<cursor></cursor>${b}</${tag}></doc>\n`
}

// For ranges: A is anchor, B is head/focus. May have A > B (reversed) or A == B (collapsed).
function rangeOutputHtml(text, a, b, tag = 'p') {
  if (a === b) {
    // collapsed -> cursor marker
    const left = text.slice(0, a)
    const right = text.slice(a)
    return `<doc><${tag}>${left}<cursor></cursor>${right}</${tag}></doc>\n`
  }
  // determine which marker is first in document order
  const lo = Math.min(a, b)
  const hi = Math.max(a, b)
  const firstMarker = (a < b) ? '<anchor></anchor>' : '<focus></focus>'
  const lastMarker  = (a < b) ? '<focus></focus>'   : '<anchor></anchor>'
  return `<doc><${tag}>${text.slice(0, lo)}${firstMarker}${text.slice(lo, hi)}${lastMarker}${text.slice(hi)}</${tag}></doc>\n`
}

// ---------------------------------------------------------------------------
// Group A: cursor positions (20 fixtures)
// ---------------------------------------------------------------------------

const groupA = [
  ['to-0',          'helloworld',     0],
  ['to-1',          'helloworld',     1],
  ['to-2',          'helloworld',     2],
  ['to-3',          'helloworld',     3],
  ['to-4',          'helloworld',     4],
  ['to-5',          'helloworld',     5],
  ['to-6',          'helloworld',     6],
  ['to-7',          'helloworld',     7],
  ['to-8',          'helloworld',     8],
  ['to-9',          'helloworld',     9],
  ['to-mid-long',   'thequickbrown',  6],
  ['to-start-short','hi',             0],
  ['to-end-short',  'hi',             2],
  ['to-3-of-five',  'apple',          3],
  ['to-2-of-five',  'apple',          2],
  ['to-1-of-five',  'apple',          1],
  ['to-4-of-five',  'apple',          4],
  ['to-mid-7',      'sevenchar',      4],
  ['to-end-7',      'sevenchar',      9],
  ['to-0-of-7',     'sevenchar',      0],
  ['to-5-of-7',     'sevenchar',      5],
  ['to-1-of-7',     'sevenchar',      1],
  ['to-2-of-7',     'sevenchar',      2],
]
// Drop the first set's "5" and "7" duplicates to land at exactly 20 unique cases
// per the task table. The task table actually lists 20 distinct fixtures — let
// me re-derive the list strictly from it.

const groupA_final = [
  ['to-0',          'helloworld',     0],
  ['to-1',          'helloworld',     1],
  ['to-2',          'helloworld',     2],
  ['to-3',          'helloworld',     3],
  ['to-4',          'helloworld',     4],
  ['to-6',          'helloworld',     6],
  ['to-8',          'helloworld',     8],
  ['to-mid-long',   'thequickbrown',  6],
  ['to-start-short','hi',             0],
  ['to-end-short',  'hi',             2],
  ['to-3-of-five',  'apple',          3],
  ['to-2-of-five',  'apple',          2],
  ['to-1-of-five',  'apple',          1],
  ['to-4-of-five',  'apple',          4],
  ['to-mid-7',      'sevenchar',      4],
  ['to-end-7',      'sevenchar',      9],
  ['to-0-of-7',     'sevenchar',      0],
  ['to-5-of-7',     'sevenchar',      5],
  ['to-1-of-7',     'sevenchar',      1],
  ['to-2-of-7',     'sevenchar',      2],
]

for (const [name, text, offset] of groupA_final) {
  const dir = path.join(T_B_CURSOR, name)
  writeFix(
    dir,
    cursorInputHtml(text),
    selEvent(offset, offset),
    cursorOutputHtml(text, offset)
  )
}
console.log(`Group A: ${groupA_final.length} cursor fixtures written`)

// ---------------------------------------------------------------------------
// Group B: ranges (20 fixtures)
// ---------------------------------------------------------------------------

const groupB = [
  ['r-0-1',           'helloworld',     0,  1,  'p'],
  ['r-0-2',           'helloworld',     0,  2,  'p'],
  ['r-0-3',           'helloworld',     0,  3,  'p'],
  ['r-0-5',           'helloworld',     0,  5,  'p'],
  ['r-0-10',          'helloworld',     0, 10,  'p'],
  ['r-1-9',           'helloworld',     1,  9,  'p'],
  ['r-2-8',           'helloworld',     2,  8,  'p'],
  ['r-3-7',           'helloworld',     3,  7,  'p'],
  ['r-4-6',           'helloworld',     4,  6,  'p'],
  ['r-2-3',           'helloworld',     2,  3,  'p'],
  ['r-mid-3',         'apple',          1,  4,  'p'],
  ['r-mid-1',         'apple',          2,  3,  'p'],
  ['r-from-1-of-7',   'sevenchar',      1,  6,  'p'],
  ['r-from-2-of-9',   'nineletter',     2,  7,  'p'],
  ['r-from-3-of-11',  'elevenletter',   3,  8,  'p'],
  ['r-reverse-7-3',   'helloworld',     7,  3,  'p'],
  ['r-reverse-5-1',   'helloworld',     5,  1,  'p'],
  ['r-reverse-9-0',   'helloworld',     9,  0,  'p'],
  ['r-collapsed-mid', 'helloworld',     5,  5,  'p'],
  ['r-full-h1',       'longheading',    0, 11,  'h1'],
]

for (const [name, text, a, b, tag] of groupB) {
  const dir = path.join(T_B_RANGE, name)
  writeFix(
    dir,
    cursorInputHtml(text, tag),
    selEvent(a, b),
    rangeOutputHtml(text, a, b, tag)
  )
}
console.log(`Group B: ${groupB.length} range fixtures written`)

// ---------------------------------------------------------------------------
// Group C: move shape coords (20 fixtures)
// ---------------------------------------------------------------------------

function moveShapeInputHtml() {
  return `<doc>
  <drawing id="D1" width="400" height="300">
    <layer id="L1">
      <shape id="S1" kind="rect" x="100" y="100" width="50" height="50"></shape>
    </layer>
  </drawing>
</doc>`
}

function moveShapeOutputHtml(x, y) {
  return `<doc>
  <drawing id="D1" width="400" height="300">
    <layer id="L1">
      <shape id="S1" kind="rect" x="${x}" y="${y}" width="50" height="50"></shape>
    </layer>
  </drawing>
</doc>`
}

function moveEvents(dx, dy) {
  return [{
    type: 'command',
    name: 'moveShapes',
    args: {
      shape_paths: [[0, 0, 0]],
      dx,
      dy
    }
  }]
}

const groupC = [
  ['to-0-0',     0,   0],
  ['to-50-50',   50,  50],
  ['to-150-150', 150, 150],
  ['to-200-100', 200, 100],
  ['to-100-200', 100, 200],
  ['to-200-200', 200, 200],
  ['to-300-300', 300, 300],
  ['to-50-150',  50,  150],
  ['to-150-50',  150, 50],
  ['to-25-75',   25,  75],
  ['to-75-25',   75,  25],
  ['to-250-50',  250, 50],
  ['to-50-250',  50,  250],
  ['to-300-100', 300, 100],
  ['to-100-300', 100, 300],
  ['to-110-110', 110, 110],
  ['to-90-90',   90,  90],
  ['to-101-101', 101, 101],
  ['to-99-99',   99,  99],
  ['to-111-222', 111, 222],
]

for (const [name, x, y] of groupC) {
  const dx = x - 100
  const dy = y - 100
  const dir = path.join(T_0_MOVE, name)
  writeFix(
    dir,
    moveShapeInputHtml(),
    moveEvents(dx, dy),
    moveShapeOutputHtml(x, y)
  )
}
console.log(`Group C: ${groupC.length} move-shape fixtures written`)

// ---------------------------------------------------------------------------
// Group D: setShapeAttr fill colors (20 fixtures)
// ---------------------------------------------------------------------------

function fillInputHtml() {
  return `<doc>
  <drawing id="D1" width="400" height="300">
    <layer id="L1">
      <shape id="S1" kind="rect" x="50" y="50" width="100" height="80"></shape>
    </layer>
  </drawing>
</doc>`
}

function fillOutputHtml(color) {
  return `<doc>
  <drawing id="D1" width="400" height="300">
    <layer id="L1">
      <shape id="S1" kind="rect" x="50" y="50" width="100" height="80" fill="${color}"></shape>
    </layer>
  </drawing>
</doc>`
}

function fillEvents(color) {
  return [{
    type: 'command',
    name: 'setShapeAttr',
    args: {
      shape_path: [0, 0, 0],
      name: 'fill',
      value: color
    }
  }]
}

const colors = [
  'azure', 'bisque', 'chocolate', 'crimson', 'fuchsia',
  'indigo', 'lavender', 'lime', 'magenta', 'mediumblue',
  'mediumpurple', 'mintcream', 'mistyrose', 'moccasin', 'palegreen',
  'palevioletred', 'peachpuff', 'sienna', 'slateblue', 'springgreen'
]

for (const color of colors) {
  const dir = path.join(T_0_SETATTR, `fill-${color}`)
  writeFix(
    dir,
    fillInputHtml(),
    fillEvents(color),
    fillOutputHtml(color)
  )
}
console.log(`Group D: ${colors.length} fill-color fixtures written`)

console.log('All fixtures written successfully.')
