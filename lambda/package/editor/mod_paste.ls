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

pub fn schema_default_block(schema) =>
  if (schema.default_block != null) { schema.default_block }
  else if (schema.paragraph != null) { 'paragraph' }
  else if (schema.p != null) { 'p' }
  else { md_default_block }

pub fn schema_hard_break(schema) =>
  if (schema.hard_break != null) { 'hard_break' }
  else if (schema.br != null) { 'br' }
  else { 'hard_break' }

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

fn symbol_in_list(items, value, i, n) {
  if (i >= n) { false }
  else if (items[i] == value) { true }
  else { symbol_in_list(items, value, i + 1, n) }
}

fn mark_allowed_by_parent(schema, parent_entry, mark) {
  if (not mark_is_known(schema, mark)) { false }
  else if (parent_entry == null or parent_entry.marks == null or parent_entry.marks == 'all') { true }
  else if (parent_entry.marks == 'none') { false }
  else { symbol_in_list(parent_entry.marks, mark, 0, len(parent_entry.marks)) }
}

fn mark_excludes_all(schema, mark) {
  let e = entry_for(schema, mark)
  e != null and e.excludes == 'all'
}

fn keep_allowed_marks_at(schema, parent_entry, marks, i, n, acc) {
  if (i >= n) { acc }
  else if (mark_allowed_by_parent(schema, parent_entry, marks[i])) {
    keep_allowed_marks_at(schema, parent_entry, marks, i + 1, n, [*acc, marks[i]])
  } else {
    keep_allowed_marks_at(schema, parent_entry, marks, i + 1, n, acc)
  }
}

fn has_exclusive_mark(schema, marks, i, n) {
  if (i >= n) { false }
  else if (mark_excludes_all(schema, marks[i])) { true }
  else { has_exclusive_mark(schema, marks, i + 1, n) }
}

fn keep_exclusive_marks_at(schema, marks, i, n, acc) {
  if (i >= n) { acc }
  else if (mark_excludes_all(schema, marks[i])) { keep_exclusive_marks_at(schema, marks, i + 1, n, [*acc, marks[i]]) }
  else { keep_exclusive_marks_at(schema, marks, i + 1, n, acc) }
}

pub fn keep_marks_for(schema, parent_entry, marks) {
  let allowed = keep_allowed_marks_at(schema, parent_entry, marks, 0, len(marks), [])
  if (has_exclusive_mark(schema, allowed, 0, len(allowed))) { keep_exclusive_marks_at(schema, allowed, 0, len(allowed), []) }
  else { allowed }
}

pub fn keep_marks(schema, marks) => keep_marks_for(schema, null, marks)

// ---------------------------------------------------------------------------
// Attribute filtering/defaulting
// ---------------------------------------------------------------------------

fn attr_value_at(attrs, name, i, n) {
  if (i >= n) { null }
  else if (attrs[i].name == name) { attrs[i].value }
  else { attr_value_at(attrs, name, i + 1, n) }
}

fn attr_type_ok(value, spec) {
  if (value == null) { true }
  else if (spec.type == null) { true }
  else if (spec.type == 'string') { type(value) == string }
  else if (spec.type == 'int') { type(value) == int }
  else if (spec.type == 'bool') { type(value) == bool }
  else { true }
}

fn value_in_list(items, value, i, n) {
  if (i >= n) { false }
  else if (items[i] == value) { true }
  else { value_in_list(items, value, i + 1, n) }
}

fn attr_constraints_ok(value, spec) {
  if (value == null) { true }
  else if (spec.one_of != null and not value_in_list(spec.one_of, value, 0, len(spec.one_of))) { false }
  else if (spec.min != null and value < spec.min) { false }
  else if (spec.max != null and value > spec.max) { false }
  else { true }
}

fn attr_validate_ok(value, spec) =>
  if (value == null or spec.validate == null) { true } else { spec.validate(value) }

fn attr_has_default(spec) =>
  spec.default != null or type(spec.default) == string or type(spec.default) == int or type(spec.default) == bool

fn attr_value_ok(value, spec) =>
  value != null and attr_type_ok(value, spec) and attr_constraints_ok(value, spec) and attr_validate_ok(value, spec)

fn attr_clean_value(value, spec) {
  if (attr_value_ok(value, spec)) { value }
  else if (attr_has_default(spec) and attr_value_ok(spec.default, spec)) { spec.default }
  else { null }
}

fn normalize_attrs_at(attrs, specs, i, n, acc) {
  if (i >= n) { acc }
  else {
    let spec = specs[i]
    let value = attr_clean_value(attr_value_at(attrs, spec.name, 0, len(attrs)), spec)
    let next = if (value == null) { acc } else { [*acc, {name: spec.name, value: value}] }
    normalize_attrs_at(attrs, specs, i + 1, n, next)
  }
}

fn normalize_attrs(entry, attrs) =>
  if (entry.attrs == null) { [] } else { normalize_attrs_at(attrs, entry.attrs, 0, len(entry.attrs), []) }

// ---------------------------------------------------------------------------
// Recursive coercion
// ---------------------------------------------------------------------------

fn coerce_one_at(schema, parent_entry, n) {
  if (is_text(n)) { text_marked(n.text, keep_marks_for(schema, parent_entry, n.marks)) }
  else if (not is_node(n)) { null }
  else {
    let e = entry_for(schema, n.tag)
    if (e == null) { null }
    else if (e.atomic and len(e.content) == 0) { node_attrs(n.tag, normalize_attrs(e, n.attrs), []) }
    else {
      let cr = content_role(e)
      let kids = coerce_list_at(schema, n.content, cr, e, 0, len(n.content), [], [])
      node_attrs(n.tag, normalize_attrs(e, n.attrs), kids)
    }
  }
}

fn coerce_one(schema, n) => coerce_one_at(schema, null, n)

fn default_block_entry(schema) => entry_for(schema, schema_default_block(schema))

fn filtered_pending(schema, pending) =>
  coerce_list_at(schema, pending, 'inline', default_block_entry(schema), 0, len(pending), [], [])

fn coerce_list_at(schema, kids, parent_role, parent_entry, i, n, acc, pending) {
  if (i >= n) {
    if (len(pending) > 0) { [*acc, node(schema_default_block(schema), filtered_pending(schema, pending))] } else { acc }
  } else {
    let raw = kids[i]
    let c = coerce_one_at(schema, parent_entry, raw)
    if (c == null) { coerce_list_at(schema, kids, parent_role, parent_entry, i + 1, n, acc, pending) }
    else {
      let r = role_of_node(schema, c)
      if (parent_role == 'block') {
        if (r == 'block') {
          let acc2 = if (len(pending) > 0) { [*acc, node(schema_default_block(schema), filtered_pending(schema, pending))] } else { acc }
          coerce_list_at(schema, kids, parent_role, parent_entry, i + 1, n, [*acc2, c], [])
        } else {
          coerce_list_at(schema, kids, parent_role, parent_entry, i + 1, n, acc, [*pending, c])
        }
      } else {
        if (r == 'block' and is_node(c)) {
          let inner = coerce_list_at(schema, c.content, parent_role, parent_entry, 0, len(c.content), [], [])
          coerce_list_at(schema, kids, parent_role, parent_entry, i + 1, n, list_concat(acc, inner), pending)
        } else {
          coerce_list_at(schema, kids, parent_role, parent_entry, i + 1, n, [*acc, c], pending)
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
  coerce_list_at(schema, children, parent_role, null, 0, len(children), [], [])

pub fn coerce_children_for_parent(schema, parent_tag, children) {
  let parent_entry = entry_for(schema, parent_tag)
  if (parent_entry == null) { [] }
  else { coerce_list_at(schema, children, content_role(parent_entry), parent_entry, 0, len(children), [], []) }
}

pub fn coerce_for_md_block(children) =>
  coerce_list_at(md_schema, children, 'block', null, 0, len(children), [], [])
