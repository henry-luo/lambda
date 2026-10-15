import model: ~~.mod_model
import document: ~~.mod_document
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let base = model.open_editor(node('doc',[node('p',[text("Hello world")])]))
let selected = edit_set_selection(base,text_selection(pos([0,0],6),pos([0,0],11)))
let bold = model.execute(selected,"format.bold")^
doc_text(bold.doc) == "Hello world"
len(bold.doc.content[0].content) == 2
let undone = model.execute(bold,"edit.undo")^
undone.doc == base.doc
model.execute(undone,"edit.redo")^.doc == bold.doc
let color = model.execute(selected,"format.color",{color:"#2864ad"})^
doc_text(color.doc) == "Hello world"
color.selection == text_selection(pos([0,1],0),pos([0,1],5))
model.char_value(color,"color","") == "#2864ad"
let size = model.execute(color,"format.size",{size:18})^
model.char_value(size,"color","") == "#2864ad"
model.char_value(size,"font-size","") == "18pt"
model.execute(size,"edit.undo")^.doc == color.doc
let styled = model.execute(selected,"para.style",{style:"Heading1"})^
styled.doc.content[0].tag == 'h2'
len(model.headings(styled.doc)) == 1
let inserted = model.execute(base,"text.insert",{data:"Welcome "})^
doc_text(inserted.doc) == "Welcome Hello world"
let replaced = model.execute(base,"edit.replaceAll",{find:"world",replace:"studio"})^
doc_text(replaced.doc) == "Hello studio"
model.execute(replaced,"edit.undo")^.doc == base.doc
let runs = node('doc',[node('p',[text("Open "),text("Studio")])])
len(model.search(runs,"Open Studio")) == 1
model.search(runs,"Open Studio")[0] == text_selection(pos([0,0],0),pos([0,1],6))
model.word_count(runs) == 2
let backwards = edit_set_selection(base,text_selection(pos([0,0],8),pos([0,0],2)))
let highlight = model.execute(backwards,"format.highlight",{color:"#f9e896"})^
highlight.selection == text_selection(pos([0,1],6),pos([0,1],0))
model.execute(highlight,"edit.undo")^.selection == backwards.selection
let joined = edit_set_selection(model.open_editor(runs),text_selection(pos([0,0],2),pos([0,1],4)))
let font = model.execute(joined,"format.font",{name:"Georgia"})^
doc_text(font.doc) == "Open Studio"
font.selection == text_selection(pos([0,1],0),pos([0,2],4))
model.execute(font,"edit.undo")^.doc == runs
let branch = model.execute(model.execute(inserted,"edit.undo")^,"text.insert",{data:"New "})^
not edit_can_redo(branch)
model.execute(base,"format.size",{size:1000}) ^ { true } ~ { false }
model.execute(base,"missing.command") ^ { true } ~ { false }
len(document.sample().content) == 12
let across = model.execute(model.open_editor(runs),"edit.replaceAll",{find:"Open Studio",replace:"Community"})^
doc_text(across.doc) == "Community"
model.execute(across,"edit.undo")^.doc == runs
let two = node('doc',[node('p',[text("cat cat")])])
let twice = model.execute(model.open_editor(two),"edit.replaceAll",{find:"cat",replace:"kitten"})^
doc_text(twice.doc) == "kitten kitten"
model.execute(twice,"edit.undo")^.doc == two
let table = model.execute(base,"insert.table",{rows:2,cols:2,header:true})^
table.selection.kind == 'text'
let row_added = model.execute(table,"table.insertRowBelow")^
len(row_added.doc.content[1].content) == 3
let column_added = model.execute(row_added,"table.insertColumnRight")^
all([for (row in column_added.doc.content[1].content) len(row.content)==3])
model.execute(model.execute(column_added,"edit.undo")^,"edit.redo")^.selection == column_added.selection
let unicode = model.open_editor(node('doc',[node('p',[text("Café 🌱 東京")])]))
let unicode_selected = edit_set_selection(unicode,text_selection(pos([0,0],0),pos([0,0],5)))
doc_text(model.execute(unicode_selected,"format.font",{name:"Arial"})^.doc) == "Café 🌱 東京"
model.word_count(unicode.doc) == 3
