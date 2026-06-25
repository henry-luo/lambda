// HTML5 subset schema — matches our fixture format (HTML tags directly).
// Mirrors the html5_subset_schema constant in
// lambda/package/editor/mod_md_schema.ls.

import type { Schema, SchemaEntry } from '../model/schema.js'

function block(content: SchemaEntry['content'], extras: Partial<SchemaEntry> = {}): SchemaEntry {
  return { role: 'block', content, marks: 'none', ...extras }
}
function blockMarks(content: SchemaEntry['content'], extras: Partial<SchemaEntry> = {}): SchemaEntry {
  return { role: 'block', content, marks: 'all', editable: true, ...extras }
}
function inlineLeaf(extras: Partial<SchemaEntry> = {}): SchemaEntry {
  return { role: 'inline', content: [], marks: 'none', atomic: true, ...extras }
}
function mark(extras: Partial<SchemaEntry> = {}): SchemaEntry {
  return {
    role: 'mark',
    content: [{ role: 'inline', qty: 'star' }],
    marks: 'all',
    ...extras
  }
}

export const html5SubsetEntries: Record<string, SchemaEntry> = {
  doc:        block([{ role: 'block', qty: 'plus' }], { editable: true }),
  html:       block([{ tag: 'head', qty: 'opt' }, { tag: 'body', qty: 'one' }]),
  head:       block([{ tag: 'title', qty: 'opt' }]),
  title:      block([{ role: 'text', qty: 'star' }]),
  body:       block([{ role: 'block', qty: 'star' }]),
  main:       block([{ role: 'block', qty: 'star' }]),
  section:    block([{ role: 'block', qty: 'star' }]),
  article:    block([{ role: 'block', qty: 'star' }]),
  nav:        block([{ role: 'block', qty: 'star' }]),
  header:     block([{ role: 'block', qty: 'star' }]),
  footer:     block([{ role: 'block', qty: 'star' }]),
  aside:      block([{ role: 'block', qty: 'star' }]),
  div:        block([{ role: 'block', qty: 'star' }]),

  p:          blockMarks([{ role: 'inline', qty: 'star' }]),
  h1:         blockMarks([{ role: 'inline', qty: 'star' }]),
  h2:         blockMarks([{ role: 'inline', qty: 'star' }]),
  h3:         blockMarks([{ role: 'inline', qty: 'star' }]),
  h4:         blockMarks([{ role: 'inline', qty: 'star' }]),
  h5:         blockMarks([{ role: 'inline', qty: 'star' }]),
  h6:         blockMarks([{ role: 'inline', qty: 'star' }]),

  blockquote: block([{ role: 'block', qty: 'plus' }]),
  pre:        block([{ role: 'text', qty: 'star' }], { selectable: true }),

  ul:         block([{ tag: 'li', qty: 'plus' }]),
  ol:         block([{ tag: 'li', qty: 'plus' }]),
  li:         {
    role: 'block',
    content: [{ any: [{ role: 'block', qty: 'one' }, { role: 'inline', qty: 'one' }], qty: 'plus' }],
    marks: 'all'
  },

  figure:     block([{ any: [{ tag: 'img', qty: 'one' }, { tag: 'figcaption', qty: 'one' }], qty: 'plus' }]),
  figcaption: blockMarks([{ role: 'inline', qty: 'star' }]),

  table:      block([{ any: [
                    { tag: 'thead', qty: 'one' },
                    { tag: 'tbody', qty: 'one' },
                    { tag: 'tfoot', qty: 'one' },
                    { tag: 'tr', qty: 'one' }
                  ], qty: 'plus' }]),
  thead:      block([{ tag: 'tr', qty: 'plus' }]),
  tbody:      block([{ tag: 'tr', qty: 'plus' }]),
  tfoot:      block([{ tag: 'tr', qty: 'plus' }]),
  tr:         block([{ any: [{ tag: 'td', qty: 'one' }, { tag: 'th', qty: 'one' }], qty: 'plus' }]),
  td:         blockMarks([{ role: 'inline', qty: 'star' }]),
  th:         blockMarks([{ role: 'inline', qty: 'star' }]),

  hr:         { role: 'block', content: [], marks: 'none', atomic: true, selectable: true },
  br:         { role: 'inline', content: [], marks: 'none', atomic: true },
  img:        inlineLeaf({
    selectable: true,
    draggable: true,
    attrs: [
      { name: 'src', required: true, type: 'string' },
      { name: 'alt', required: false, type: 'string', default: '' }
    ]
  }),

  a:          mark({
    attrs: [
      { name: 'href', required: true, type: 'string' },
      { name: 'title', required: false, type: 'string', default: '' }
    ]
  }),
  strong:     mark(),
  b:          mark(),
  em:         mark(),
  i:          mark(),
  u:          mark(),
  span:       mark(),
  code:       mark({ marks: 'none', excludes: 'all' })
}

export const html5SubsetSchema: Schema = {
  entries: html5SubsetEntries,
  default_block: 'p'
}
