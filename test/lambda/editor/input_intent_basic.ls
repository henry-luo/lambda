// input_intent_basic.ls — Phase R4 intent dispatcher
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_input_intent

let d0 = node('doc', [
  node('paragraph', [text("Hello")])
])
let caret = text_selection(pos([0, 0], 5), pos([0, 0], 5))
let s0 = {doc: d0, selection: caret}

let tx_ins = dispatch_intent(s0, {input_type: "insertText", data: "!"})
"insert intent doc:"; doc_text(tx_ins.doc_after) == "Hello!"
"insert intent caret:"; tx_ins.sel_after.anchor.offset == 6

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

"unknown intent null:"; dispatch_intent(s0, {input_type: "formatStrikeThrough", data: null}) == null
