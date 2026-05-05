// SourceSelection text extraction & node walks (Phase R2)
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_doc

// Sample doc:
//   <doc
//     <paragraph "Hello world.">
//     <paragraph "See " <strong "this"> " example.">
//   >
let doc = <doc
  <paragraph; "Hello world.">
  <paragraph; "See " <strong; "this"> " example.">
>

// ---------------------------------------------------------------------------
// node_text — concatenates all string descendants in document order
// ---------------------------------------------------------------------------
"doc node_text len:";    len(node_text(doc))
"para0 node_text:";      node_text(doc[0]) == "Hello world."
"para1 node_text:";      node_text(doc[1]) == "See this example."
"strong node_text:";     node_text(doc[1][1]) == "this"
"string leaf:";          node_text("plain") == "plain"
"empty:";                node_text(42) == ""

// ---------------------------------------------------------------------------
// AllSelection — returns all of the document's text concatenated
// ---------------------------------------------------------------------------
let all_sel = all_selection()
"all sel str:";  selection_to_string(doc, all_sel) == "Hello world.See this example."

// ---------------------------------------------------------------------------
// NodeSelection — text content of a sub-tree
// ---------------------------------------------------------------------------
let n0 = node_selection([0])
"node sel para0:";  selection_to_string(doc, n0) == "Hello world."

let n1 = node_selection([1, 1])
"node sel strong:"; selection_to_string(doc, n1) == "this"

let n_bad = node_selection([99])
"node sel missing:"; selection_to_string(doc, n_bad) == ""

// ---------------------------------------------------------------------------
// TextSelection within a single text leaf
// ---------------------------------------------------------------------------
//   "Hello world." — extract substrings via byte offsets
let same_leaf_path = [0, 0]
let ts1 = text_selection(pos(same_leaf_path, 0),  pos(same_leaf_path, 5))
"text sel hello:";  selection_to_string(doc, ts1) == "Hello"

let ts2 = text_selection(pos(same_leaf_path, 6),  pos(same_leaf_path, 11))
"text sel world:";  selection_to_string(doc, ts2) == "world"

// reversed direction (head < anchor) still works (selection is normalized)
let ts3 = text_selection(pos(same_leaf_path, 11), pos(same_leaf_path, 6))
"text sel reversed:";  selection_to_string(doc, ts3) == "world"

// inside <strong> child of paragraph 1
let ts4 = text_selection(pos([1, 1, 0], 0), pos([1, 1, 0], 4))
"text sel strong:"; selection_to_string(doc, ts4) == "this"

// ---------------------------------------------------------------------------
// Cross-leaf TextSelection walks text leaves in document order.
// ---------------------------------------------------------------------------
let ts_cross = text_selection(pos([0, 0], 0), pos([1, 0], 4))
"cross-leaf text:";  selection_to_string(doc, ts_cross) == "Hello world.See "

// ---------------------------------------------------------------------------
// Unknown selection kind returns null
// ---------------------------------------------------------------------------
let bogus = {kind: 'mystery'}
"bogus null:";  selection_to_string(doc, bogus) == null

// ---------------------------------------------------------------------------
// Editor-map docs use the same SourcePos helpers as raw Mark elements.
// ---------------------------------------------------------------------------
let editor_doc = node('doc', [node('paragraph', [
  text("Alpha"),
  node('strong', [text("Beta")]),
  text("Gamma")
])])
"editor doc text:"; node_text(editor_doc) == "AlphaBetaGamma"
"editor node sel:"; selection_to_string(editor_doc, node_selection([0, 1])) == "Beta"
let editor_same = text_selection(pos([0, 0], 1), pos([0, 0], 4))
"editor same leaf:"; selection_to_string(editor_doc, editor_same) == "lph"
let editor_cross = text_selection(pos([0, 0], 2), pos([0, 2], 2))
"editor cross leaf:"; selection_to_string(editor_doc, editor_cross) == "phaBetaGa"
let resolved = resolve_pos(editor_doc, pos([0, 1, 0], 2))
"editor resolve found:"; resolved.found
"editor resolve node text:"; resolved.node.text == "Beta"
"editor resolve parent tag:"; resolved.parent.tag == 'strong'
"editor resolve depth:"; resolved.depth == 3
"editor resolve ancestors:"; len(resolved.ancestors) == 4
let before_strong = resolve_before(resolved, 2)
"editor before strong path:"; path_equal(before_strong.path, [0]) and before_strong.offset == 1
let after_strong = resolve_after(resolved, 2)
"editor after strong path:"; path_equal(after_strong.path, [0]) and after_strong.offset == 2
let after_root = resolve_after(resolved, 0)
"editor after root:"; path_equal(after_root.path, []) and after_root.offset == 1
let missing_resolved = resolve_pos(editor_doc, pos([0, 9], 0))
"editor resolve missing:"; not missing_resolved.found
