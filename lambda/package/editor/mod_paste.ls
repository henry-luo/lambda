// mod_paste.ls — schema-driven coercion for inserting external content
// (Phase R5 — the "parseSlice" half of paste/drop handling).
//
// Input: an arbitrary doc fragment (parsed from HTML/Markdown clipboard data)
// shaped like our internal Doc model (mod_doc).
// Output: a coerced fragment whose every node/mark conforms to the supplied
// schema. The coercion rules are intentionally simple and mechanical:
//
//   1. Drop nodes whose tag is not in the schema.
//   2. Drop marks not allowed by the schema.
//   3. Lift inline content that landed at block context into a default block.
//   4. Strip `atomic: true` nodes' content.
//   5. Unwrap stray block content found in inline context.

import .mod_doc
import .mod_md_schema

// ---------------------------------------------------------------------------
// Schema lookups
// ---------------------------------------------------------------------------

fn entry_for(schema, tag) => schema[tag]

fn role_of_node(schema, n) {
  if (is_text(n)) { 'inline' }
  else if (is_node(n)) {
    let e = entry_for(schema, n.tag)
    if (e == null) { null } else { e.role }
  }
  else { null }
}

// Inspect a schema entry's content expression and decide the context role its
// children live in. If any term wants 'inline' / 'text' / 'mark', the content
// is inline-context; otherwise it is block-context. (Empty content -> block,
// which is harmless because such nodes are atomic.)
fn content_role_at(content, i, n) {
  if (i >= n) { 'block' }
  else {
    let t = content[i]
    let r = if (t.role != null) { t.role } else { 'block' }
    if (r == 'inline' or r == 'text' or r == 'mark') { 'inline' }
    else { content_role_at(content, i + 1, n) }
  }
}
fn content_role(entry) => content_role_at(entry.content, 0, len(entry.content))

// ---------------------------------------------------------------------------
// Mark filtering
// ---------------------------------------------------------------------------

fn mark_is_known(schema, m) {
  let e = entry_for(schema, m)
  e != null and e.role == 'mark'
}

fn keep_marks_at(schema, marks, i, n, acc) {
  if (i >= n) { acc }
  else if (mark_is_known(schema, marks[i])) {
    keep_marks_at(schema, marks, i + 1, n, [*acc, marks[i]])
  } else {
    keep_marks_at(schema, marks, i + 1, n, acc)
  }
}

pub fn keep_marks(schema, marks) =>
  keep_marks_at(schema, marks, 0, len(marks), [])

// ---------------------------------------------------------------------------
// Recursive coercion
// ---------------------------------------------------------------------------

fn coerce_one(schema, n) {
  if (is_text(n)) { text_marked(n.text, keep_marks(schema, n.marks)) }
  else if (not is_node(n)) { null }
  else {
    let e = entry_for(schema, n.tag)
    if (e == null) { null }
    else if (e.atomic) { node_attrs(n.tag, n.attrs, []) }
    else {
      let cr = content_role(e)
      let kids = coerce_list_at(schema, n.content, cr, 0, len(n.content), [], [])
      node_attrs(n.tag, n.attrs, kids)
    }
  }
}

fn coerce_list_at(schema, kids, parent_role, i, n, acc, pending) {
  if (i >= n) {
    if (len(pending) > 0) { [*acc, node(md_default_block, pending)] } else { acc }
  } else {
    let raw = kids[i]
    let c = coerce_one(schema, raw)
    if (c == null) { coerce_list_at(schema, kids, parent_role, i + 1, n, acc, pending) }
    else {
      let r = role_of_node(schema, c)
      if (parent_role == 'block') {
        if (r == 'block') {
          let acc2 = if (len(pending) > 0) { [*acc, node(md_default_block, pending)] } else { acc }
          coerce_list_at(schema, kids, parent_role, i + 1, n, [*acc2, c], [])
        } else {
          coerce_list_at(schema, kids, parent_role, i + 1, n, acc, [*pending, c])
        }
      } else {
        if (r == 'block' and is_node(c)) {
          let inner = coerce_list_at(schema, c.content, parent_role, 0, len(c.content), [], [])
          coerce_list_at(schema, kids, parent_role, i + 1, n, list_concat(acc, inner), pending)
        } else {
          coerce_list_at(schema, kids, parent_role, i + 1, n, [*acc, c], pending)
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

pub fn coerce_to_schema(schema, fragment) => coerce_one(schema, fragment)

pub fn coerce_children(schema, children, parent_role) =>
  coerce_list_at(schema, children, parent_role, 0, len(children), [], [])

pub fn coerce_for_md_block(children) =>
  coerce_list_at(md_schema, children, 'block', 0, len(children), [], [])
