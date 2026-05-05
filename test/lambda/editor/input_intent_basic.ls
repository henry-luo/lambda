// input_intent_basic.ls — Phase R4 intent dispatcher
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_transaction
import lambda.package.editor.mod_input_intent
import lambda.package.editor.mod_decorations
import lambda.package.editor.mod_history
import lambda.package.editor.mod_md_schema

let d0 = node('doc', [
  node('paragraph', [text("Hello")])
])
let caret = text_selection(pos([0, 0], 5), pos([0, 0], 5))
let s0 = {doc: d0, selection: caret}

let tx_ins = dispatch_intent(s0, {input_type: "insertText", data: "!"})
"insert intent doc:"; doc_text(tx_ins.doc_after) == "Hello!"
"insert intent caret:"; tx_ins.sel_after.anchor.offset == 6
"insert intent scroll:"; tx_get_meta(tx_ins, "scrollIntoView")

let tx_paste = dispatch_intent(s0, {input_type: "insertFromPaste", data: " world"})
"paste intent doc:"; doc_text(tx_paste.doc_after) == "Hello world"
"paste intent caret:"; tx_paste.sel_after.anchor.offset == 11

let s0_deco = {doc: d0, selection: text_selection(pos([0, 0], 0), pos([0, 0], 0)),
  decorations: find_decorations_in_doc(d0, "Hello", {class: "find-hit"})}
let tx_deco_ins = dispatch_intent(s0_deco, {input_type: "insertText", data: "X"})
let s_deco_after = state_after_intent(s0_deco, tx_deco_ins)
"decorations preserved:"; len(s_deco_after.decorations.items) == 1
"decorations mapped from:"; s_deco_after.decorations.items[0].from.offset == 1
"decorations mapped to:"; s_deco_after.decorations.items[0].to.offset == 6

let tx_html_paste = dispatch_intent(s0, {
  input_type: "insertFromPaste", mime: "text/html",
  html: "<p> <strong>world</strong></p>", data: " world"})
let html_leaf = node_at(tx_html_paste.doc_after, [0, 1])
"html paste doc:"; doc_text(tx_html_paste.doc_after) == "Hello world"
"html paste caret:"; tx_html_paste.sel_after.anchor.offset == 5
"html paste mark:"; node_at(tx_html_paste.doc_after, [0, 2]).marks[0] == 'strong'

let drop_doc = node('doc', [
  node('paragraph', [text("A")]),
  node('paragraph', [text("B")]),
  node('paragraph', [text("C")])
])
let drop_state = {doc: drop_doc, selection: node_selection([0])}
let tx_drop_move = dispatch_intent(drop_state, {
  input_type: "insertFromDrop", source_path: [0], target_parent_path: [], target_index: 3})
"drop move doc:"; [for (n in tx_drop_move.doc_after.content) doc_text(n)] == ["B", "C", "A"]
"drop move selected:"; path_equal(tx_drop_move.sel_after.path, [2])
let tx_drop_insert = dispatch_intent(drop_state, {
  input_type: "insertFromDrop", slice: [node('paragraph', [text("X")])], target_parent_path: [], target_index: 1})
"drop insert doc:"; [for (n in tx_drop_insert.doc_after.content) doc_text(n)] == ["A", "X", "B", "C"]
"drop insert selected:"; path_equal(tx_drop_insert.sel_after.path, [1])

let atom_doc = node('doc', [
  node('paragraph', [text("A")]),
  node('image', []),
  node('paragraph', [text("C")])
])
let atom_state = {doc: atom_doc, selection: node_selection([1])}
let tx_node_type = dispatch_intent(atom_state, {input_type: "insertText", data: "B"})
"node type doc:"; [for (n in tx_node_type.doc_after.content) doc_text(n)] == ["A", "B", "C"]
"node type caret:"; path_equal(tx_node_type.sel_after.anchor.path, [1, 0]) and tx_node_type.sel_after.anchor.offset == 1
let tx_node_delete = dispatch_intent(atom_state, {input_type: "deleteContentBackward", data: null})
"node delete doc:"; [for (n in tx_node_delete.doc_after.content) doc_text(n)] == ["A", "C"]
"node delete caret:"; path_equal(tx_node_delete.sel_after.anchor.path, []) and tx_node_delete.sel_after.anchor.offset == 1
let tx_node_paste = dispatch_intent(atom_state, {
  input_type: "insertFromPaste", mime: "text/html", html: "<p>Alpha</p><p>Beta</p>", data: "Alpha\nBeta"})
"node paste count:"; len(tx_node_paste.doc_after.content) == 4
"node paste doc:"; [for (n in tx_node_paste.doc_after.content) doc_text(n)] == ["A", "Alpha", "Beta", "C"]
"node paste caret:"; path_equal(tx_node_paste.sel_after.anchor.path, [2, 0]) and tx_node_paste.sel_after.anchor.offset == 4

let mark_span_doc = node('doc', [node('paragraph', [
  text_marked("Hello", ['strong']),
  text(" "),
  text_marked("world", ['em'])
])])
let mark_span_state = {doc: mark_span_doc, selection: text_selection(pos([0, 0], 2), pos([0, 2], 3))}
let tx_span_type = dispatch_intent(mark_span_state, {input_type: "insertText", data: "X"})
"span type doc:"; doc_text(tx_span_type.doc_after) == "HeXld"
"span type mark:"; has_mark(node_at(tx_span_type.doc_after, [0, 1]).marks, 'strong')
let tx_span_delete = dispatch_intent(mark_span_state, {input_type: "deleteContentBackward", data: null})
"span delete doc:"; doc_text(tx_span_delete.doc_after) == "Held"
"span delete caret:"; path_equal(tx_span_delete.sel_after.anchor.path, [0, 0]) and tx_span_delete.sel_after.anchor.offset == 2
let tx_span_paste = dispatch_intent(mark_span_state, {input_type: "insertFromPaste", data: "Y"})
"span paste doc:"; doc_text(tx_span_paste.doc_after) == "HeYld"
"span paste caret:"; path_equal(tx_span_paste.sel_after.anchor.path, [0, 1]) and tx_span_paste.sel_after.anchor.offset == 1

let cross_block_doc = node('doc', [node('paragraph', [text("Alpha")]), node('paragraph', [text("Omega")])])
let cross_block_state = {doc: cross_block_doc, selection: text_selection(pos([0, 0], 2), pos([1, 0], 2))}
let tx_cross_type = dispatch_intent(cross_block_state, {input_type: "insertText", data: "X"})
"cross-block type doc:"; doc_text(tx_cross_type.doc_after) == "AlXega"
"cross-block type caret:"; path_equal(tx_cross_type.sel_after.anchor.path, [0, 1]) and tx_cross_type.sel_after.anchor.offset == 1
let tx_cross_delete = dispatch_intent(cross_block_state, {input_type: "deleteContentBackward", data: null})
"cross-block delete doc:"; doc_text(tx_cross_delete.doc_after) == "Alega"
"cross-block delete caret:"; path_equal(tx_cross_delete.sel_after.anchor.path, [0, 0]) and tx_cross_delete.sel_after.anchor.offset == 2
let tx_cross_line = dispatch_intent(cross_block_state, {input_type: "insertLineBreak", data: null})
"cross-block line tag:"; node_at(tx_cross_line.doc_after, [0, 1]).tag == 'hard_break'
"cross-block line caret:"; path_equal(tx_cross_line.sel_after.anchor.path, [0, 2])
let tx_cross_paste = dispatch_intent(cross_block_state, {
  input_type: "insertFromPaste", mime: "text/html", html: "<p>One</p><p>Two</p>", data: "One\nTwo"})
"cross-block paste count:"; len(tx_cross_paste.doc_after.content) == 2
"cross-block paste doc:"; [for (n in tx_cross_paste.doc_after.content) doc_text(n)] == ["AlOne", "Twoega"]
"cross-block paste caret:"; path_equal(tx_cross_paste.sel_after.anchor.path, [1, 1]) and tx_cross_paste.sel_after.anchor.offset == 3

let tx_back = dispatch_intent(s0, {input_type: "deleteContentBackward", data: null})
"delete-back intent doc:"; doc_text(tx_back.doc_after) == "Hell"
"delete-back intent caret:"; tx_back.sel_after.anchor.offset == 4

let tx_fwd = dispatch_intent(
  {doc: d0, selection: text_selection(pos([0, 0], 1), pos([0, 0], 1))},
  {input_type: "deleteContentForward", data: null})
"delete-fwd intent doc:"; doc_text(tx_fwd.doc_after) == "Hllo"

let join_doc = node('doc', [node('paragraph', [text("A")]), node('paragraph', [text("B")])])
let tx_back_join = dispatch_intent(
  {doc: join_doc, selection: text_selection(pos([1, 0], 0), pos([1, 0], 0))},
  {input_type: "deleteContentBackward", data: null})
"delete-back join intent:"; doc_text(tx_back_join.doc_after) == "AB" and len(tx_back_join.doc_after.content) == 1
let tx_fwd_join = dispatch_intent(
  {doc: join_doc, selection: text_selection(pos([0, 0], 1), pos([0, 0], 1))},
  {input_type: "deleteContentForward", data: null})
"delete-fwd join intent:"; doc_text(tx_fwd_join.doc_after) == "AB" and len(tx_fwd_join.doc_after.content) == 1

let tx_split = dispatch_intent(s0, {input_type: "insertParagraph", data: null})
"split intent count:"; len(tx_split.doc_after.content)
"split intent left:"; doc_text(node_at(tx_split.doc_after, [0])) == "Hello"
"split intent right empty:"; len(doc_text(node_at(tx_split.doc_after, [1]))) == 0
let tx_split_span = dispatch_intent(mark_span_state, {input_type: "insertParagraph", data: null})
"split span intent left:"; doc_text(node_at(tx_split_span.doc_after, [0])) == "He"
"split span intent right:"; doc_text(node_at(tx_split_span.doc_after, [1])) == "ld"
"split span intent caret:"; path_equal(tx_split_span.sel_after.anchor.path, [1, 0])

let tx_line = dispatch_intent({doc: d0, selection: text_selection(pos([0, 0], 2), pos([0, 0], 2))},
  {input_type: "insertLineBreak", data: null})
"line-break intent tag:"; node_at(tx_line.doc_after, [0, 1]).tag == 'hard_break'
"line-break intent caret:"; path_equal(tx_line.sel_after.anchor.path, [0, 2])
let tx_line_span = dispatch_intent(mark_span_state, {input_type: "insertLineBreak", data: null})
"line-break span intent tag:"; node_at(tx_line_span.doc_after, [0, 1]).tag == 'hard_break'
"line-break span intent doc:"; doc_text(tx_line_span.doc_after) == "Held"
"line-break span intent caret:"; path_equal(tx_line_span.sel_after.anchor.path, [0, 2])

let word_doc = node('doc', [node('paragraph', [text("one two")])])
let tx_word = dispatch_intent({doc: word_doc, selection: text_selection(pos([0, 0], 7), pos([0, 0], 7))},
  {input_type: "deleteWordBackward", data: null})
"word-delete intent doc:"; doc_text(tx_word.doc_after) == "one "
"word-delete intent caret:"; tx_word.sel_after.anchor.offset == 4
let tx_word_node = dispatch_intent(atom_state, {input_type: "deleteWordBackward", data: null})
"word-delete node doc:"; [for (n in tx_word_node.doc_after.content) doc_text(n)] == ["A", "C"]
"word-delete node caret:"; path_equal(tx_word_node.sel_after.anchor.path, []) and tx_word_node.sel_after.anchor.offset == 1

let list_doc = node('doc', [node_attrs('list', [{name: 'ordered', value: false}], [
  node('list_item', [node('paragraph', [text("A")])]),
  node('list_item', [node('paragraph', [text("B")])])
])])
let tx_indent_intent = dispatch_intent({doc: list_doc, selection: text_selection(pos([0, 1, 0, 0], 1), pos([0, 1, 0, 0], 1))},
  {input_type: "formatIndent", data: null})
"indent intent nested:"; doc_text(node_at(tx_indent_intent.doc_after, [0, 0, 1, 0])) == "B"
let tx_outdent_intent = dispatch_intent({doc: tx_indent_intent.doc_after, selection: text_selection(pos([0, 0, 1, 0, 0, 0], 1), pos([0, 0, 1, 0, 0, 0], 1))},
  {input_type: "formatOutdent", data: null})
"outdent intent top count:"; len(node_at(tx_outdent_intent.doc_after, [0]).content) == 2

let tx_bold = dispatch_intent(s0, {input_type: "formatBold", data: null})
"bold intent stored:"; has_mark(tx_get_meta(tx_bold, "storedMarks"), 'strong')
let s_bold = state_after_intent(s0, tx_bold)
let tx_bold_type = dispatch_intent(s_bold, {input_type: "insertText", data: "!"})
"bold intent insert mark:"; has_mark(node_at(tx_bold_type.doc_after, [0, 1]).marks, 'strong')
"bold intent text:"; doc_text(tx_bold_type.doc_after) == "Hello!"
let tx_node_bold = dispatch_intent({doc: d0, selection: node_selection([0])}, {input_type: "formatBold", data: null})
"bold node mark:"; has_mark(node_at(tx_node_bold.doc_after, [0, 0]).marks, 'strong')
"bold node selection:"; tx_node_bold.sel_after.kind == 'node' and path_equal(tx_node_bold.sel_after.path, [0])
let tx_span_bold = dispatch_intent(mark_span_state, {input_type: "formatBold", data: null})
"bold span steps:"; len(tx_span_bold.steps) == 2
"bold span middle:"; has_mark(node_at(tx_span_bold.doc_after, [0, 1]).marks, 'strong')
"bold span last:"; has_mark(node_at(tx_span_bold.doc_after, [0, 2]).marks, 'strong')

let tx_italic = dispatch_intent(s0, {input_type: "formatItalic", data: null})
"italic intent stored:"; has_mark(tx_get_meta(tx_italic, "storedMarks"), 'em')

let tx_under = dispatch_intent(s0, {input_type: "formatUnderline", data: null})
"underline intent stored:"; has_mark(tx_get_meta(tx_under, "storedMarks"), 'underline')

let tx_select_all = dispatch_intent(s0, {input_type: "selectAll", data: null})
"select-all kind:"; tx_select_all.sel_after.kind == 'all'
"select-all history:"; tx_get_meta(tx_select_all, "addToHistory") == false
"select-all scroll:"; tx_get_meta(tx_select_all, "scrollIntoView")
let s_all = state_after_intent(s0, tx_select_all)
let tx_type_all = dispatch_intent(s_all, {input_type: "insertText", data: "New"})
"type-all doc:"; doc_text(tx_type_all.doc_after) == "New"
"type-all caret:"; path_equal(tx_type_all.sel_after.anchor.path, [0, 0]) and tx_type_all.sel_after.anchor.offset == 3
let tx_delete_all = dispatch_intent(s_all, {input_type: "deleteContentBackward", data: null})
"delete-all doc:"; doc_text(tx_delete_all.doc_after) == ""
"delete-all caret:"; path_equal(tx_delete_all.sel_after.anchor.path, [0, 0]) and tx_delete_all.sel_after.anchor.offset == 0
let tx_paste_all_html = dispatch_intent(s_all, {
  input_type: "insertFromPaste", mime: "text/html", html: "<p>Alpha</p><p>Beta</p>", data: "Alpha\nBeta"})
"paste-all html count:"; len(tx_paste_all_html.doc_after.content) == 2
"paste-all html doc:"; [for (n in tx_paste_all_html.doc_after.content) doc_text(n)] == ["Alpha", "Beta"]
"paste-all html caret:"; path_equal(tx_paste_all_html.sel_after.anchor.path, [1, 0]) and tx_paste_all_html.sel_after.anchor.offset == 4
let tx_bold_all = dispatch_intent(s_all, {input_type: "formatBold", data: null})
"bold-all mark:"; has_mark(node_at(tx_bold_all.doc_after, [0, 0]).marks, 'strong')
"bold-all selection:"; tx_bold_all.sel_after.kind == 'all'

let html_state = {doc: node('doc', [node('p', [text("Hello")])]), schema: html5_subset_schema,
  selection: text_selection(pos([0, 0], 5), pos([0, 0], 5))}
let tx_html_line = dispatch_intent(html_state, {input_type: "insertLineBreak", data: null})
"html line-break intent tag:"; node_at(tx_html_line.doc_after, [0, 1]).tag == 'br'
let tx_html_paste_schema = dispatch_intent(html_state, {
  input_type: "insertFromPaste", mime: "text/html", html: "<p> world</p>", data: " world"})
"html paste intent tag:"; node_at(tx_html_paste_schema.doc_after, [0]).tag == 'p'
"html paste intent text:"; doc_text(tx_html_paste_schema.doc_after) == "Hello world"


let tx_comp_start = dispatch_intent(s0, {input_type: "compositionStart", data: null})
let s_comp0 = state_after_intent(s0, tx_comp_start)
"composition start active:"; s_comp0.composition.active
"composition start history:"; tx_get_meta(tx_comp_start, "addToHistory") == false

let tx_comp1 = dispatch_intent(s_comp0, {input_type: "insertCompositionText", data: "a"})
let s_comp1 = state_after_intent(s_comp0, tx_comp1)
"composition update doc:"; doc_text(tx_comp1.doc_after) == "Helloa"
"composition update active:"; s_comp1.composition.active
"composition update range:"; s_comp1.composition.range.head.offset == 6
"composition update history:"; tx_get_meta(tx_comp1, "addToHistory") == false

let tx_comp2 = dispatch_intent(s_comp1, {input_type: "insertCompositionText", data: "ai"})
let s_comp2 = state_after_intent(s_comp1, tx_comp2)
"composition replace doc:"; doc_text(tx_comp2.doc_after) == "Helloai"
"composition replace range:"; s_comp2.composition.range.head.offset == 7

let tx_comp_end = dispatch_intent(s_comp2, {input_type: "insertFromComposition", data: "愛"})
let s_comp_end = state_after_intent(s_comp2, tx_comp_end)
"composition end doc:"; doc_text(tx_comp_end.doc_after) == "Hello愛"
"composition end cleared:"; s_comp_end.composition == null
"composition end history:"; tx_get_meta(tx_comp_end, "addToHistory")

let tx_comp_cancel0 = dispatch_intent(s_comp0, {input_type: "insertCompositionText", data: "x"})
let s_comp_cancel0 = state_after_intent(s_comp0, tx_comp_cancel0)
let tx_comp_cancel = dispatch_intent(s_comp_cancel0, {input_type: "deleteCompositionText", data: null})
"composition cancel doc:"; doc_text(tx_comp_cancel.doc_after) == "Hello"
"composition cancel history:"; tx_get_meta(tx_comp_cancel, "addToHistory") == false

let h_state0 = {doc: d0, selection: caret, history: history_new()}
let h_tx_ins = dispatch_intent(h_state0, {input_type: "insertText", data: "!"})
let h_state1 = state_after_intent(h_state0, h_tx_ins)
"history push undo:"; can_undo(h_state1.history)
let h_tx_undo = dispatch_intent(h_state1, {input_type: "historyUndo", data: null})
let h_state2 = state_after_intent(h_state1, h_tx_undo)
"history undo doc:"; doc_text(h_state2.doc) == "Hello"
"history undo redo:"; can_redo(h_state2.history)
"history undo scroll:"; tx_get_meta(h_tx_undo, "scrollIntoView")
let h_tx_redo = dispatch_intent(h_state2, {input_type: "historyRedo", data: null})
let h_state3 = state_after_intent(h_state2, h_tx_redo)
"history redo doc:"; doc_text(h_state3.doc) == "Hello!"
"history redo undo:"; can_undo(h_state3.history)

let hg_state0 = {doc: d0, selection: caret, history: history_new()}
let hg_tx1 = dispatch_intent(hg_state0, {input_type: "insertText", data: "A"})
let hg_state1 = state_after_intent(hg_state0, hg_tx1)
let hg_tx2 = dispatch_intent(hg_state1, {input_type: "insertText", data: "B"})
let hg_state2 = state_after_intent(hg_state1, hg_tx2)
"history group depth:"; len(hg_state2.history.undo) == 1
let hg_undo = dispatch_intent(hg_state2, {input_type: "historyUndo", data: null})
let hg_state3 = state_after_intent(hg_state2, hg_undo)
"history group undo doc:"; doc_text(hg_state3.doc) == "Hello"

"unknown intent null:"; dispatch_intent(s0, {input_type: "formatStrikeThrough", data: null}) == null
