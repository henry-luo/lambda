// input_intent_basic.ls — Phase R4 intent dispatcher
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_transaction
import lambda.package.editor.mod_input_intent
import lambda.package.editor.mod_decorations
import lambda.package.editor.mod_history

let d0 = node('doc', [
  node('paragraph', [text("Hello")])
])
let caret = text_selection(pos([0, 0], 5), pos([0, 0], 5))
let s0 = {doc: d0, selection: caret}

let tx_ins = dispatch_intent(s0, {input_type: "insertText", data: "!"})
"insert intent doc:"; doc_text(tx_ins.doc_after) == "Hello!"
"insert intent caret:"; tx_ins.sel_after.anchor.offset == 6

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

let tx_back = dispatch_intent(s0, {input_type: "deleteContentBackward", data: null})
"delete-back intent doc:"; doc_text(tx_back.doc_after) == "Hell"
"delete-back intent caret:"; tx_back.sel_after.anchor.offset == 4

let tx_fwd = dispatch_intent(
  {doc: d0, selection: text_selection(pos([0, 0], 1), pos([0, 0], 1))},
  {input_type: "deleteContentForward", data: null})
"delete-fwd intent doc:"; doc_text(tx_fwd.doc_after) == "Hllo"

let tx_split = dispatch_intent(s0, {input_type: "insertParagraph", data: null})
"split intent count:"; len(tx_split.doc_after.content)
"split intent left:"; doc_text(node_at(tx_split.doc_after, [0])) == "Hello"
"split intent right empty:"; len(doc_text(node_at(tx_split.doc_after, [1]))) == 0

let tx_line = dispatch_intent({doc: d0, selection: text_selection(pos([0, 0], 2), pos([0, 0], 2))},
  {input_type: "insertLineBreak", data: null})
"line-break intent tag:"; node_at(tx_line.doc_after, [0, 1]).tag == 'hard_break'
"line-break intent caret:"; path_equal(tx_line.sel_after.anchor.path, [0, 2])

let word_doc = node('doc', [node('paragraph', [text("one two")])])
let tx_word = dispatch_intent({doc: word_doc, selection: text_selection(pos([0, 0], 7), pos([0, 0], 7))},
  {input_type: "deleteWordBackward", data: null})
"word-delete intent doc:"; doc_text(tx_word.doc_after) == "one "
"word-delete intent caret:"; tx_word.sel_after.anchor.offset == 4

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
let bold_leaf = node_at(tx_bold.doc_after, [0, 0])
"bold intent mark:"; has_mark(bold_leaf.marks, 'strong')
"bold intent text:"; bold_leaf.text == "Hello"

let tx_italic = dispatch_intent(s0, {input_type: "formatItalic", data: null})
let italic_leaf = node_at(tx_italic.doc_after, [0, 0])
"italic intent mark:"; has_mark(italic_leaf.marks, 'em')

let tx_under = dispatch_intent(s0, {input_type: "formatUnderline", data: null})
let under_leaf = node_at(tx_under.doc_after, [0, 0])
"underline intent mark:"; has_mark(under_leaf.marks, 'underline')

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
let h_tx_redo = dispatch_intent(h_state2, {input_type: "historyRedo", data: null})
let h_state3 = state_after_intent(h_state2, h_tx_redo)
"history redo doc:"; doc_text(h_state3.doc) == "Hello!"
"history redo undo:"; can_undo(h_state3.history)

"unknown intent null:"; dispatch_intent(s0, {input_type: "formatStrikeThrough", data: null}) == null
