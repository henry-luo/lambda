// svg.ls — SVG format adapter for lambda.edit
// (vibe/radiant/Radiant_Design_Edit_Mode.md §6).
//
// The saved SVG tree is the drawing model: every element is a model node with
// its attributes in source order and its children, text included, so a save
// writes back namespaces, the viewBox and dimensions, ids, transforms, <defs>,
// references, groups, and elements no tool edits. The source before the root
// element (XML declaration, comments, a DOCTYPE the parser does not keep) and
// after it is kept as text. Editor-only handles and previews come from
// session state beside the model (drawing.ls), never from the model itself.
//
// Round-trip contract (§8): the tree survives exactly, except that white
// space between elements is re-indented and text-only content is trimmed as
// the XML reader reads it.

import .model
import lambda.editor.mod_doc
import lambda.editor.mod_step

// Drawing edits are attribute and child Steps on the source tree; there is no
// content schema to satisfy, so the drawing is not validated against one.
pub let schema = {}

// ---------------------------------------------------------------------------
// Import: parsed XML -> editor model
// ---------------------------------------------------------------------------

fn model_child(c) {
  if (type(c) == string) text(c)
  else if (type(c) == element) model_node(c)
  else text(string(c))
}

// One element: tag, attributes in source order, children.
pub fn model_node(el) =>
  node_attrs(name(el), [for (k, v at map(el)) {name: k, value: v}], [for (c in content(el)) model_child(c)])

fn after_markup(source, i, close) {
  let at = index_of(slice(source, i, len(source)), close)
  if (at == null) len(source) else i + at + len(close)
}

// A DOCTYPE ends at the first `>` outside its internal subset `[...]`.
fn after_doctype(source, i, depth) {
  if (i >= len(source)) i
  else if (source[i] == "[") after_doctype(source, i + 1, depth + 1)
  else if (source[i] == "]") after_doctype(source, i + 1, depth - 1)
  else if (source[i] == ">" and depth == 0) i + 1
  else after_doctype(source, i + 1, depth)
}

// Where the root element's start tag begins: after the XML declaration,
// processing instructions, comments, and DOCTYPE of the prologue.
fn root_start(source, i) {
  let lt = index_of(slice(source, i, len(source)), "<")
  if (lt == null) null
  else {
    let at = i + lt
    let rest = slice(source, at, at + 9)
    if (starts_with(rest, "<?")) root_start(source, after_markup(source, at, "?>"))
    else if (starts_with(rest, "<!--")) root_start(source, after_markup(source, at, "-->"))
    else if (starts_with(rest, "<!")) root_start(source, after_doctype(source, at + 2, 0))
    else at
  }
}

// The text after the root's end tag (a self-closing root has none to find).
fn epilogue_of(source, root_tag) {
  let close = "</" ++ string(root_tag)
  let at = last_index_of(source, close)
  if (at == null) ""
  else {
    let gt = index_of(slice(source, at, len(source)), ">")
    if (gt == null) "" else slice(source, at + gt + 1, len(source))
  }
}

fn root_element(parsed) {
  let found = [for (c in content(parsed) where type(c) == element and
                  (name(c) == 'svg' or ends_with(string(name(c)), ":svg"))) c]
  if (len(found) > 0) found[0] else null
}

// Parse SVG source into the drawing model and the source text around it.
pub fn import_text(source) map^ {
  let parsed = parse(source, 'xml') ^ { raise error("the SVG source could not be parsed", ^) }
  let root = root_element(parsed)
  let start = root_start(source, 0)
  if (root == null or start == null) raise error("the file has no <svg> root element")
  else {doc: model_node(root),
        envelope: {prologue: slice(source, 0, start), epilogue: epilogue_of(source, name(root))}}
}

// ---------------------------------------------------------------------------
// Export: editor model -> SVG text
// ---------------------------------------------------------------------------

pub fn attrs_xml(attrs) =>
  join([for (a in attrs where a.value != null)
          " " ++ string(a.name) ++ "=\"" ++ escape_attr(string(a.value)) ++ "\""], "")

fn indent(depth) => join([for (i in 0 to depth - 1) "  "], "")

fn has_text_child(n) => any([for (c in n.content) is_text(c)]) or false

// One node as XML. Element-only content goes one child per line; content
// holding text is written inline, since its white space is significant.
// `attrs_of(n, path)` chooses the attributes written and `keep(n)` the
// children (export writes the model's own; the surface projection filters).
pub fn node_xml(n, path, depth, attrs_of, keep) {
  if (is_text(n)) escape_text(n.text)
  else if (not is_node(n)) ""
  else if (n.tag == '!--') "<!--" ++ node_plain_text(n) ++ "-->"
  else if (starts_with(string(n.tag), "?")) "<" ++ string(n.tag) ++ " " ++ node_plain_text(n) ++ "?>"
  else {
    let start_tag = "<" ++ string(n.tag) ++ attrs_xml(attrs_of(n, path))
    let kids = [for (i in 0 to len(n.content) - 1) {child: n.content[i], path: [*path, i]}]
    let shown = [for (k in kids where keep(k.child)) k]
    let end_tag = "</" ++ string(n.tag) ++ ">"
    if (len(shown) == 0) start_tag ++ "/>"
    else if (has_text_child(n))
      start_tag ++ ">" ++ join([for (k in shown) node_xml(k.child, k.path, depth + 1, attrs_of, keep)], "") ++ end_tag
    else start_tag ++ ">" ++
      join([for (k in shown) "\n" ++ indent(depth + 1) ++ node_xml(k.child, k.path, depth + 1, attrs_of, keep)], "") ++
      "\n" ++ indent(depth) ++ end_tag
  }
}

fn own_attrs(n, path) => n.attrs
fn keep_all(n) => true

// Serialize the drawing with the source text around its root.
pub fn export_text(doc, envelope) =>
  envelope.prologue ++ node_xml(doc, [], 0, own_attrs, keep_all) ++ envelope.epilogue

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

// Attribute values compare as text (a tool may write a number), and how text
// is cut into leaves carries no meaning.
fn norm_node(n) {
  if (is_text(n)) n
  else if (not is_node(n)) n
  else node_attrs(n.tag, [for (a in n.attrs where a.value != null) {name: a.name, value: string(a.value)}],
                  [for (c in merge_leaves(n.content)) norm_node(c)])
}

pub fn normal_form(doc) any => norm_node(doc)

// null when the drawing survives export and re-import, else what would change.
pub fn check_roundtrip(doc, envelope) {
  let back = import_text(export_text(doc, envelope)) ^ { null }
  if (back == null) "the saved SVG could not be read back"
  else if (back.envelope != envelope) "the text around the drawing would change"
  else if (normal_form(back.doc) != normal_form(doc)) "the drawing would change"
  else null
}

// ---------------------------------------------------------------------------
// Descriptor
// ---------------------------------------------------------------------------

pub let descriptor = {
  id: 'svg', name: "SVG", suffixes: [".svg"],
  surface: 'drawing', schema: schema, schema_preset: null,
  unsupported_input_types: [], view_only_tags: [],
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip,
  toolbar: 'drawing'
}
