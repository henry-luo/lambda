// mod_doc_schema.ls — combined flow-doc + drawing schema
// (Radiant Rich Editor, Stage 4 — §3.1, §13.1).
//
// The default schema for editors that allow inline <drawing> blocks. Conceptually
//   doc_schema = md_schema ++ drawing_schema
// but Lambda maps are fixed-field records with no dynamic merge/spread, so the
// combined map is assembled here by referencing the entries of each source
// schema. Tags are disjoint between the two schemas, so there is no shadowing.

import .mod_md_schema
import .mod_drawing_schema

pub doc_schema = {
  // ── flow-doc (markdown subset) entries ──
  doc:        md_schema.doc,
  paragraph:  md_schema.paragraph,
  heading:    md_schema.heading,
  blockquote: md_schema.blockquote,
  list:       md_schema.list,
  list_item:  md_schema.list_item,
  table:      md_schema.table,
  tr:         md_schema.tr,
  td:         md_schema.td,
  th:         md_schema.th,
  code_block: md_schema.code_block,
  hr:         md_schema.hr,
  image:      md_schema.image,
  hard_break: md_schema.hard_break,
  link:       md_schema.link,
  strong:     md_schema.strong,
  em:         md_schema.em,
  u:          md_schema.u,
  code:       md_schema.code,

  // ── drawing-layer entries ──
  drawing:       drawing_schema.drawing,
  layer:         drawing_schema.layer,
  shape:         drawing_schema.shape,
  connector:     drawing_schema.connector,
  group:         drawing_schema.group,
  'text-frame':  drawing_schema['text-frame'],
  label:         drawing_schema.label
}

pub doc_default_block = md_default_block
