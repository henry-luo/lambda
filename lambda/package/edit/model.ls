// model.ls — helpers shared by the lambda.edit format adapters.
//
// Adapters translate between parsed source (Mark elements) and the
// lambda.editor model ({kind: 'node', tag, attrs: [{name, value}], content}).
// Generic editor behavior stays in lambda.editor; these are adapter plumbing.

import lambda.editor.mod_doc
import lambda.editor.mod_step

// [{name, value}] from [[name, value], ...], dropping absent values.
pub fn attr_list(pairs) => [for (p in pairs where p[1] != null) {name: p[0], value: p[1]}]

// A model node's attribute value, or null.
pub fn attr_get(n, key) => if (is_node(n)) attrs_get(n.attrs, key) else null

// The literal text of a Mark element: its string descendants, concatenated.
pub fn plain_text(item) string {
  if (type(item) == string) item
  else if (type(item) == element) join([for (c in content(item)) plain_text(c)], "") or ""
  else ""
}

// Membership that answers false for a malformed collection, so predicates
// built on it stay total.
pub fn member(xs, x) bool => contains(xs, x) or false

// Order records by a string key; a malformed input sorts as empty.
pub fn sorted_by_name(xs) any => sort(xs, (x) => string(x.name)) or []

// The text of a model node (text leaves, concatenated).
pub fn node_plain_text(n) => doc_text(n)

// A reference resolved against the document's folder: no scheme, not rooted,
// not a same-document query or fragment (RFC 3986 §4.2). A colon before any
// "/", "?" or "#" ends a scheme, since a relative first segment cannot hold one.
pub fn is_relative_url(u: string) bool {
  if (u == "" or starts_with(u, "/") or starts_with(u, "#") or starts_with(u, "?")) false
  else {
    let colon = index_of(u, ":")
    colon == null or not (precedes(colon, index_of(u, "/")) and precedes(colon, index_of(u, "?"))
                          and precedes(colon, index_of(u, "#")))
  }
}

fn precedes(i, j) bool => j == null or i < j

// What in one attribute would break if the document moved to another folder:
// a relative link or image, or raw markup naming one (not rebased by structure).
fn attr_reference(a) {
  let value = string(a.value)
  if (a.name == 'href' and is_relative_url(value)) "the link " ++ value
  else if (a.name == 'src' and is_relative_url(value)) "the image " ++ value
  else if (a.name == 'html' and (contains(lower(value), "href=") or contains(lower(value), "src=")))
    "raw HTML with a link or image"
  else null
}

// The first such reference in a model tree, in document order, or null.
pub fn first_relative_reference(n) {
  if (not is_node(n)) null
  else {
    let own = [for (a in n.attrs where attr_reference(a) != null) attr_reference(a)]
    if (len(own) > 0) own[0] else first_reference_in(n.content, 0)
  }
}

fn first_reference_in(nodes, i) {
  if (i >= len(nodes)) null
  else {
    let found = first_relative_reference(nodes[i])
    if (found != null) found else first_reference_in(nodes, i + 1)
  }
}

pub fn is_heading_tag(tag) =>
  tag == 'h1' or tag == 'h2' or tag == 'h3' or tag == 'h4' or tag == 'h5' or tag == 'h6'

fn block_label(n) {
  let tag = if (is_node(n)) n.tag else 'text'
  let excerpt = slice(trim(node_plain_text(n)), 0, 40)
  if (excerpt == "") "<" ++ string(tag) ++ ">"
  else "<" ++ string(tag) ++ "> \"" ++ excerpt ++ "\""
}

// null when two block lists agree, else a sentence naming the first block
// that differs (1-based, as a reader counts).
pub fn first_block_difference(a, b, i) {
  if (i >= len(a) and i >= len(b)) null
  else if (i >= len(a)) "block " ++ string(i + 1) ++ " " ++ block_label(b[i]) ++ " would be added"
  else if (i >= len(b)) "block " ++ string(i + 1) ++ " " ++ block_label(a[i]) ++ " would be lost"
  else if (a[i] != b[i]) "block " ++ string(i + 1) ++ " " ++ block_label(a[i]) ++ " would change"
  else first_block_difference(a, b, i + 1)
}

// ---------------------------------------------------------------------------
// Inline export shared by the adapters
// ---------------------------------------------------------------------------

// The marks of an inline item: a text leaf's own, or those an atomic node
// (a retained element, an image) was found inside, kept in `node_marks_attr`
// so export writes it back within them. A space never occurs in a parsed
// attribute name, so adapter bookkeeping cannot collide with source attributes.
pub let node_marks_attr = 'edit marks'

fn item_marks(item) {
  if (is_text(item)) item.marks
  else if (is_node(item)) (attr_get(item, node_marks_attr) or [])
  else []
}

// An item's value for `mark` (true for a valueless mark), or null without it.
fn run_key(item, mark) {
  let marks = item_marks(item)
  if (not has_mark(marks, mark)) null
  else {
    let value = mark_value(marks, mark)
    if (value == null) true else value
  }
}

// Split items into runs of equal `mark` value, keeping their order.
fn runs_at(items, mark, i, n, current, key, acc) {
  if (i >= n) { if (len(current) > 0) [*acc, {value: key, items: current}] else acc }
  else {
    let k = run_key(items[i], mark)
    if (len(current) == 0 or k == key) runs_at(items, mark, i + 1, n, [*current, items[i]], k, acc)
    else runs_at(items, mark, i + 1, n, [items[i]], k, [*acc, {value: key, items: current}])
  }
}

// Rebuild marked inline content. Marks nest in `order` (outermost first), so
// adjacent leaves with the same value for a mark share one wrapper.
// `wrap(mark, value, kids)` spells one mark around its content and
// `atom(item)` spells an item once its marks are applied; both return lists.
pub fn nest_marks(items, order, wrap, atom) => nest_level(items, order, 0, wrap, atom)

fn nest_level(items, order, level, wrap, atom) {
  if (level >= len(order)) [for (it in items) for (x in atom(it)) x]
  else {
    let mark = order[level];
    [for (run in runs_at(items, mark, 0, len(items), [], null, []))
       for (x in (if (run.value == null) nest_level(run.items, order, level + 1, wrap, atom)
                  else wrap(mark, run.value, nest_level(run.items, order, level + 1, wrap, atom)))) x]
  }
}

// ---------------------------------------------------------------------------
// Normal form helpers shared by the adapters
// ---------------------------------------------------------------------------

// How a run is cut into leaves carries no meaning: empty leaves drop and
// adjacent leaves with equal marks merge.
pub fn merge_leaves(items) => merge_leaves_at(items, 0, len(items), [])

fn merge_leaves_at(items, i, n, acc) {
  if (i >= n) { acc }
  else {
    let it = items[i]
    if (is_text(it) and len(it.text) == 0) { merge_leaves_at(items, i + 1, n, acc) }
    else if (is_text(it) and len(acc) > 0 and is_text(acc[len(acc) - 1]) and
             marks_equal(acc[len(acc) - 1].marks, it.marks)) {
      let prev = acc[len(acc) - 1]
      merge_leaves_at(items, i + 1, n,
        [*take(acc, len(acc) - 1), text_marked(prev.text ++ it.text, prev.marks)])
    }
    else { merge_leaves_at(items, i + 1, n, [*acc, it]) }
  }
}

// A node's present attributes in name order; attribute order carries no meaning.
pub fn sorted_attrs(n) => sorted_by_name([for (a in n.attrs where a.value != null) a])

// Commands can produce mark nodes (<strong> around leaves); in normal form a
// mark node is its leaves with that mark applied.
pub fn flatten_mark_nodes(items, tags) =>
  [for (it in items)
     for (x in (if (is_node(it) and member(tags, it.tag))
                  [for (leaf in flatten_mark_nodes(it.content, tags))
                     if (is_text(leaf)) text_marked(leaf.text, with_mark(leaf.marks, it.tag, true)) else leaf]
                else [it])) x]
