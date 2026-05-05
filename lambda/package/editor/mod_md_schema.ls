// mod_md_schema.ls — Markdown content schema (Radiant Rich Text Editing, Phase R1)
//
// A schema is a map  { tag_symbol -> entry }  where each entry has shape:
//   { role:    'block' | 'inline' | 'mark' | 'leaf',
//     content: [term, ...]               // empty array = leaf node (no children)
//     marks:   'all' | 'none',             // which marks may apply to inline content
//     attrs:   [{name, required?, type?, default?, validate?}, ...]
//     excludes:'all' | null,               // mark-only exclusion policy
//     selectable/editable/draggable: bool, // optional editor behavior flags
//     atomic:  bool }                    // optional; defaults to false
//
// A term has shape:
//   { role: <role symbol>, qty: 'one' | 'opt' | 'plus' | 'star' }   // role-keyed term, OR
//   { tag:  <tag symbol>,  qty: ... }                           // tag-keyed term
//
// 'text' is the synthetic role of bare string children inside an element. The
// validator treats 'text' as a sub-role of 'inline' (a string is always an inline)
// and lets 'mark' roles also satisfy 'inline' (an emphasis element is an inline).

pub md_schema = {
  doc:        {role: 'block',  content: [{role: 'block',  qty: 'plus'}], marks: 'none', editable: true},
  paragraph:  {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all', editable: true},
  heading:    {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all',
               attrs: [{name: 'level', required: false, type: 'int', default: 1,
                 validate: (v) => v >= 1 and v <= 6}], editable: true},
  blockquote: {role: 'block',  content: [{role: 'block',  qty: 'plus'}], marks: 'none'},
  list:       {role: 'block',  content: [{tag: 'list_item', qty: 'plus'}], marks: 'none',
               attrs: [{name: 'ordered', required: false, type: 'bool', default: false}]},
  list_item:  {role: 'block',  content: [{tag: 'paragraph', qty: 'one'},
                                        {role: 'block',    qty: 'star'}], marks: 'none'},
  code_block: {role: 'block',  content: [{role: 'text',   qty: 'star'}], marks: 'none', atomic: true, selectable: true},
  hr:         {role: 'block',  content: [],                              marks: 'none', atomic: true, selectable: true},
  image:      {role: 'inline', content: [],                              marks: 'none', atomic: true, selectable: true, draggable: true,
               attrs: [{name: 'src', required: true, type: 'string'},
                 {name: 'alt', required: false, type: 'string', default: ""}]},
  hard_break: {role: 'inline', content: [],                              marks: 'none', atomic: true},
  link:       {role: 'mark',   content: [{role: 'inline', qty: 'star'}],   marks: 'all',
               attrs: [{name: 'href', required: true, type: 'string'},
                 {name: 'title', required: false, type: 'string', default: ""}]},
  strong:     {role: 'mark',   content: [{role: 'inline', qty: 'star'}],   marks: 'all'},
  em:         {role: 'mark',   content: [{role: 'inline', qty: 'star'}],   marks: 'all'},
  code:       {role: 'mark',   content: [{role: 'inline', qty: 'star'}],   marks: 'none', excludes: 'all'}
}

pub markdown_schema = md_schema
pub commonmark_strict_schema = md_schema

pub html5_subset_schema = {
  doc:        {role: 'block',  content: [{role: 'block', qty: 'plus'}], marks: 'none', editable: true},
  html:       {role: 'block',  content: [{tag: 'head', qty: 'opt'}, {tag: 'body', qty: 'one'}], marks: 'none'},
  head:       {role: 'block',  content: [{tag: 'title', qty: 'opt'}], marks: 'none'},
  title:      {role: 'block',  content: [{role: 'text', qty: 'star'}], marks: 'none'},
  body:       {role: 'block',  content: [{role: 'block', qty: 'star'}], marks: 'none'},
  main:       {role: 'block',  content: [{role: 'block', qty: 'star'}], marks: 'none'},
  section:    {role: 'block',  content: [{role: 'block', qty: 'star'}], marks: 'none'},
  article:    {role: 'block',  content: [{role: 'block', qty: 'star'}], marks: 'none'},
  nav:        {role: 'block',  content: [{role: 'block', qty: 'star'}], marks: 'none'},
  header:     {role: 'block',  content: [{role: 'block', qty: 'star'}], marks: 'none'},
  footer:     {role: 'block',  content: [{role: 'block', qty: 'star'}], marks: 'none'},
  aside:      {role: 'block',  content: [{role: 'block', qty: 'star'}], marks: 'none'},
  div:        {role: 'block',  content: [{role: 'block', qty: 'star'}], marks: 'none'},
  p:          {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all', editable: true},
  h1:         {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all', editable: true},
  h2:         {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all', editable: true},
  h3:         {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all', editable: true},
  h4:         {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all', editable: true},
  h5:         {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all', editable: true},
  h6:         {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all', editable: true},
  blockquote: {role: 'block',  content: [{role: 'block', qty: 'plus'}], marks: 'none'},
  pre:        {role: 'block',  content: [{role: 'text', qty: 'star'}], marks: 'none', selectable: true},
  ul:         {role: 'block',  content: [{tag: 'li', qty: 'plus'}], marks: 'none'},
  ol:         {role: 'block',  content: [{tag: 'li', qty: 'plus'}], marks: 'none'},
  li:         {role: 'block',  content: [{any: [{role: 'block'}, {role: 'inline'}], qty: 'plus'}], marks: 'all'},
  figure:     {role: 'block',  content: [{any: [{tag: 'img'}, {tag: 'figcaption'}], qty: 'plus'}], marks: 'none'},
  figcaption: {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  table:      {role: 'block',  content: [{any: [{tag: 'thead'}, {tag: 'tbody'}, {tag: 'tfoot'}, {tag: 'tr'}], qty: 'plus'}], marks: 'none'},
  thead:      {role: 'block',  content: [{tag: 'tr', qty: 'plus'}], marks: 'none'},
  tbody:      {role: 'block',  content: [{tag: 'tr', qty: 'plus'}], marks: 'none'},
  tfoot:      {role: 'block',  content: [{tag: 'tr', qty: 'plus'}], marks: 'none'},
  tr:         {role: 'block',  content: [{any: [{tag: 'td'}, {tag: 'th'}], qty: 'plus'}], marks: 'none'},
  td:         {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  th:         {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  hr:         {role: 'block',  content: [], marks: 'none', atomic: true, selectable: true},
  br:         {role: 'inline', content: [], marks: 'none', atomic: true},
  img:        {role: 'inline', content: [], marks: 'none', atomic: true, selectable: true, draggable: true,
               attrs: [{name: 'src', required: true, type: 'string'},
                 {name: 'alt', required: false, type: 'string', default: ""}]},
  a:          {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'all',
               attrs: [{name: 'href', required: true, type: 'string'},
                 {name: 'title', required: false, type: 'string', default: ""}]},
  strong:     {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  b:          {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  em:         {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  i:          {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  u:          {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  span:       {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  code:       {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'none', excludes: 'all'}
}

// The block element to materialise when the user splits at a position whose
// schema does not allow the current block to repeat (e.g. Enter at end of
// a heading produces a paragraph, not another heading).
pub md_default_block = 'paragraph'
