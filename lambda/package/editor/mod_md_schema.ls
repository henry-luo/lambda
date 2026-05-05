// mod_md_schema.ls — Markdown content schema (Radiant Rich Text Editing, Phase R1)
//
// A schema is a map  { tag_symbol -> entry }  where each entry has shape:
//   { role:    'block' | 'inline' | 'mark' | 'leaf',
//     content: [term, ...]               // empty array = leaf node (no children)
//     marks:   'all' | 'none',             // which marks may apply to inline content
//     attrs:   [{name, required?, type?, default?, validate?}, ...]
//     excludes:'all' | null,               // mark-only exclusion policy
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
  doc:        {role: 'block',  content: [{role: 'block',  qty: 'plus'}], marks: 'none'},
  paragraph:  {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  heading:    {role: 'block',  content: [{role: 'inline', qty: 'star'}], marks: 'all',
               attrs: [{name: 'level', required: false, type: 'int', default: 1,
                 validate: (v) => v >= 1 and v <= 6}]},
  blockquote: {role: 'block',  content: [{role: 'block',  qty: 'plus'}], marks: 'none'},
  list:       {role: 'block',  content: [{tag: 'list_item', qty: 'plus'}], marks: 'none',
               attrs: [{name: 'ordered', required: false, type: 'bool', default: false}]},
  list_item:  {role: 'block',  content: [{tag: 'paragraph', qty: 'one'},
                                        {role: 'block',    qty: 'star'}], marks: 'none'},
  code_block: {role: 'block',  content: [{role: 'text',   qty: 'star'}], marks: 'none', atomic: true},
  hr:         {role: 'block',  content: [],                              marks: 'none', atomic: true},
  image:      {role: 'inline', content: [],                              marks: 'none', atomic: true,
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

// The block element to materialise when the user splits at a position whose
// schema does not allow the current block to repeat (e.g. Enter at end of
// a heading produces a paragraph, not another heading).
pub md_default_block = 'paragraph'
