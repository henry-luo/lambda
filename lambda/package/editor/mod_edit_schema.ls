// mod_edit_schema.ls — Schema-based content validation over Mark trees
// (Radiant Rich Text Editing, Phase R1)
//
// Pure functional validator: a doc is well-formed under a schema iff every
// element's' tag is declared and its children sequence matches the declared
// content expression. Strings are treated as the synthetic role 'text' and
// satisfy any term whose role is 'inline'.

// ---------------------------------------------------------------------------
// Lookup helpers
// ---------------------------------------------------------------------------

// Schema entry for a tag, or null when the tag is not declared.
pub fn schema_entry(schema, tag) => schema[tag]

// Role of a child relative to a schema:
//   bare strings           -> 'text'
//   declared elements      -> entry.role
//   undeclared elements    -> 'unknown'
pub fn child_role(schema, child) {
  if (type(child) == string) 'text'
  else if (type(child) == element) {
    let entry = schema[name(child)]
    if (entry == null) 'unknown' else entry.role
  }
  else 'unknown'
}

// Does a single child satisfy a single term?
//
// Term variants:
//   { tag:  S }   -> child must be an element whose name is S
//   { role: R }   -> child's' role must be R, with two relaxations:
//                       'inline' accepts 'text' and 'mark'
//                       'block'  accepts only 'block'
//   { any: [T...] } -> child may satisfy any nested term (content-expression alternation)
pub fn satisfies(schema, child, term) {
  if (term.any != null) {
    satisfies_any(schema, child, term.any, 0, len(term.any))
  } else if (term.tag != null) {
    type(child) == element and name(child) == term.tag
  } else {
    let r = child_role(schema, child)
    if (r == term.role) true
    else if (term.role == 'inline') (r == 'text' or r == 'mark')
    else false
  }
}

fn satisfies_any(schema, child, terms, i, n) {
  if (i >= n) false
  else if (satisfies(schema, child, terms[i])) true
  else satisfies_any(schema, child, terms, i + 1, n)
}

// ---------------------------------------------------------------------------
// Greedy content-expression matching
// ---------------------------------------------------------------------------

// Count consecutive children starting at `ci` that satisfy `term`, capped at `mx`.
// Pure-functional tail recursion.
fn count_sat(schema, children, ci, mx, term, acc) {
  if (acc >= mx) acc
  else if (ci + acc >= len(children)) acc
  else if (not satisfies(schema, children[ci + acc], term)) acc
  else count_sat(schema, children, ci, mx, term, acc + 1)
}

// Try quantity counts from high to low, but backtrack if the suffix does not
// match. This keeps the common greedy behavior while making sequences such as
// `block* paragraph` decidable instead of over-consuming the paragraph.
fn match_with_count(schema, children, ti, ci, terms, k, mn) {
  if (k < mn) false
  else if (match_terms_at(schema, children, ti + 1, ci + k, terms)) true
  else match_with_count(schema, children, ti, ci, terms, k - 1, mn)
}

// Match `terms` against `children` starting at term index `ti`, child index `ci`.
// Returns true iff every child is consumed by some term.
fn match_terms_at(schema, children, ti, ci, terms) {
  if (ti >= len(terms)) ci == len(children)
  else {
    let term = terms[ti]
    let qty = term.qty
    let mn = if (qty == 'plus' or qty == 'one') 1 else 0
    let mx = if (qty == 'one' or qty == 'opt') 1 else len(children) - ci
    let n = count_sat(schema, children, ci, mx, term, 0)
    if (n < mn) false
    else match_with_count(schema, children, ti, ci, terms, n, mn)
  }
}

// Match an entire content expression against a child sequence.
pub fn match_content(schema, children, terms) =>
  match_terms_at(schema, children, 0, 0, terms)

// ---------------------------------------------------------------------------
// Violations & whole-tree validation
// ---------------------------------------------------------------------------

// Violation shape: { path: [int, ...], tag: <symbol|null>, message: string }

fn mk_violation(path, tag, message) => {path: path, tag: tag, message: message}

fn node_attr(node, attr_name) => node[attr_name]

fn attr_value(node, spec) {
  let value = node_attr(node, spec.name)
  if (value == null and spec.default != null) spec.default else value
}

fn attr_type_ok(value, spec) {
  if (value == null) true
  else if (spec.type == null) true
  else if (spec.type == 'string') type(value) == string
  else if (spec.type == 'int') type(value) == int
  else if (spec.type == 'bool') type(value) == bool
  else true
}

fn attr_validate_ok(value, spec) =>
  if (value == null or spec.validate == null) true else spec.validate(value)

fn validate_attrs_at(node, path, tag, specs, i, n, acc) {
  if (i >= n) { acc }
  else {
    let spec = specs[i]
    let value = attr_value(node, spec)
    let next = if (spec.required == true and value == null) {
        [*acc, mk_violation(path, tag, "required attribute missing")]
      } else if (not attr_type_ok(value, spec)) {
        [*acc, mk_violation(path, tag, "attribute type mismatch")]
      } else if (not attr_validate_ok(value, spec)) {
        [*acc, mk_violation(path, tag, "attribute validation failed")]
      } else { acc }
    validate_attrs_at(node, path, tag, specs, i + 1, n, next)
  }
}

fn validate_attrs(node, path, tag, entry) =>
  if (entry.attrs == null) [] else validate_attrs_at(node, path, tag, entry.attrs, 0, len(entry.attrs), [])

fn symbol_in_list(items, value, i, n) {
  if (i >= n) { false }
  else if (items[i] == value) { true }
  else { symbol_in_list(items, value, i + 1, n) }
}

fn child_is_mark(schema, child) {
  if (type(child) != element) { false }
  else {
    let entry = schema[name(child)]
    entry != null and entry.role == 'mark'
  }
}

fn mark_allowed(parent_entry, mark_tag) {
  if (parent_entry.marks == null or parent_entry.marks == 'all') { true }
  else if (parent_entry.marks == 'none') { false }
  else { symbol_in_list(parent_entry.marks, mark_tag, 0, len(parent_entry.marks)) }
}

fn validate_mark_policy_child(schema, parent_entry, child, child_path) {
  if (not child_is_mark(schema, child)) { [] }
  else if (parent_entry.role == 'mark' and parent_entry.excludes == 'all') {
    [mk_violation(child_path, name(child), "mark excluded")]
  } else if (not mark_allowed(parent_entry, name(child))) {
    [mk_violation(child_path, name(child), "mark not allowed")]
  } else { [] }
}

fn validate_mark_policy_at(schema, parent_entry, kids, path, i, n, acc) {
  if (i >= n) { acc }
  else {
    let violations = validate_mark_policy_child(schema, parent_entry, kids[i], [*path, i])
    validate_mark_policy_at(schema, parent_entry, kids, path, i + 1, n, [*acc, *violations])
  }
}

fn validate_mark_policy(schema, entry, kids, path) =>
  validate_mark_policy_at(schema, entry, kids, path, 0, len(kids), [])

// Children at element node (extracted as plain array — needed for indexing
// and len() inside recursive functional code). Element iteration includes
// attribute values, but numeric indexing is child-only.
fn children_array(elem) => [for (i in 0 to len(elem) - 1) elem[i]]

// Validate one element node and return [violations].
fn validate_node(schema, node, path) {
  let tag = name(node)
  let entry = schema[tag]
  if (entry == null) {
    [mk_violation(path, tag, "unknown tag")]
  } else {
    let kids = children_array(node)
    let attr_violations = validate_attrs(node, path, tag, entry)
    let mark_violations = validate_mark_policy(schema, entry, kids, path)
    let local = if (entry.atomic == true and len(kids) > 0)
        [mk_violation(path, tag, "atomic node has children")]
      else if (not match_content(schema, kids, entry.content))
        [mk_violation(path, tag, "content does not match schema")]
      else []
    let nested = for (i in 0 to len(kids) - 1)
        if (type(kids[i]) == element)
          validate_node(schema, kids[i], [*path, i])
        else []
    [*attr_violations, *mark_violations, *local, *(for (vs in nested) for (v in vs) v)]
  }
}

// Validate a whole document tree.
pub fn schema_validate(schema, doc) {
  if (type(doc) != element) {
    [mk_violation([], null, "root is not an element")]
  } else {
    validate_node(schema, doc, [])
  }
}

// True when the doc passes validation with zero violations.
pub fn is_valid(schema, doc) => len(schema_validate(schema, doc)) == 0
