import model: ~~.mod_model
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let base = model.open_editor(node('doc',[node('p',[text_marked("Hello world",[mark('strong',true)])])]))
let selected = edit_set_selection(base,text_selection(pos([0,0],8),pos([0,0],2)))
let colored = model.execute(selected,"format.color",{color:"#2864ad"})^
let cleared = model.execute(colored,"format.clear")^
doc_text(cleared.doc) == "Hello world"
cleared.doc.content[0].content[1].marks == []
has_mark(cleared.doc.content[0].content[0].marks,'strong')
cleared.selection == colored.selection
model.execute(cleared,"edit.undo")^.doc == colored.doc
let pending = model.execute(base,"format.clear")^
model.current_marks(pending) == []
doc_text(model.execute(pending,"text.insert",{data:"Plain "})^.doc) == "Plain Hello world"

let paragraphs = model.open_editor(node('doc',[node('p',[text("First")]),node('p',[text("Second")])]))
let both = edit_set_selection(paragraphs,text_selection(pos([0,0],0),pos([1,0],6)))
let props = {align:"justify",lines:2,before:6,after:12,indent:18,first:-6,right:12}
let spaced = model.execute(both,"para.set",props)^
all([for (p in spaced.doc.content) model.css_get(attrs_get(p.attrs,'style'),"margin-left") == "18pt"])
model.paragraph_settings(spaced) == props
model.execute(spaced,"edit.undo")^.doc == paragraphs.doc
model.execute(both,"para.set",{*:props,before:-1}) ^ { true } ~ { false }

let table = model.execute(base,"insert.table",{rows:2,cols:2,header:true})^
let above = model.execute(table,"table.insertRowAbove")^
len(above.doc.content[1].content) == 3
above.doc.content[1].content[0].content[0].tag == 'td'
above.doc.content[1].content[1].content[0].tag == 'th'
let left = model.execute(table,"table.insertColumnLeft")^
all([for (row in left.doc.content[1].content) len(row.content)==3])
left.doc.content[1].content[0].content[0].tag == 'th'
model.execute(left,"edit.undo")^.doc == table.doc
let next = model.execute(table,"table.nextCell")^
next.selection.head.path == [1,0,1,0]
model.execute(next,"table.previousCell")^.selection == table.selection
let at_end = edit_set_selection(table,text_selection(pos([1,1,1,0],0),pos([1,1,1,0],0)))
let appended = model.execute(at_end,"table.nextCell")^
len(appended.doc.content[1].content) == 3
appended.selection.head.path == [1,2,0,0]
model.execute(appended,"edit.undo")^.doc == table.doc
let styled = model.execute(table,"table.style",{style:"contrast"})^
attrs_get(styled.doc.content[1].attrs,'data-word-table-style') == "contrast"
contains(files.html_document(styled.doc,"Table")^,"data-word-table-style=\"contrast\"")
let aligned = model.execute(table,"table.cellAlign",{value:"bottomRight"})^
model.css_get(attrs_get(aligned.doc.content[1].content[0].content[0].attrs,'style'),"text-align") == "right"
model.css_get(attrs_get(aligned.doc.content[1].content[0].content[0].attrs,'style'),"vertical-align") == "bottom"
let removed = model.execute(table,"table.deleteTable")^
removed.doc.content[1].tag == 'p'
model.execute(removed,"edit.undo")^.doc == table.doc
model.execute(base,"insert.table",{rows:0,cols:2}) ^ { true } ~ { false }
model.execute(base,"insert.table",{rows:2,cols:13}) ^ { true } ~ { false }

let grouped = node('doc',[node('table',[
    node('thead',[node('tr',[node('th',[text("A")]),node('th',[text("B")])])]),
    node('tbody',[node('tr',[node('td',[text("C")]),node('td',[text("D")])])])])])
let grouped_editor = edit_set_selection(model.open_editor(grouped),text_selection(pos([0,1,0,0,0],0),pos([0,1,0,0,0],0)))
let grouped_left = model.execute(grouped_editor,"table.insertColumnLeft")^
all([for (group in grouped_left.doc.content[0].content) len(group.content[0].content)==3])
doc_text(grouped_left.doc) == "ABCD"
model.execute(grouped_left,"edit.undo")^.doc == grouped

let picture = model.execute(base,"insert.picture",{src:"data:image/svg+xml;base64,PHN2Zy8+",alt:"Logo",width:320,height:0})^
picture.selection.kind == 'node'
let image = model.current_node(picture,'img')
attrs_get(image.node.attrs,'width') == 320
attrs_get(image.node.attrs,'height') == null
let resized = model.execute(picture,"picture.size",{width:160,height:80,alt:"Small logo"})^
attrs_get(model.current_node(resized,'img').node.attrs,'alt') == "Small logo"
model.execute(resized,"edit.undo")^.doc == picture.doc
model.execute(base,"insert.picture",{src:"x",width:0,height:0}) ^ { true } ~ { false }
files.restore(files.archive(resized.doc,"Pictures"))^.doc == resized.doc

let linked = model.execute(edit_set_selection(base,text_selection(pos([0,0],0),pos([0,0],5))),"insert.link",{href:"https://example.com",label:""})^
let updated = model.execute(linked,"insert.link",{href:"https://lambda.example",label:"Hello"})^
attrs_get(model.current_node(updated,'a').node.attrs,'href') == "https://lambda.example"
has_mark(model.current_node(updated,'a').node.content[0].marks,'strong')
let unlinked = model.execute(updated,"insert.removeLink")^
model.current_node(unlinked,'a') == null
doc_text(unlinked.doc) == "Hello world"
model.execute(unlinked,"edit.undo")^.doc == updated.doc

let words = node('doc',[node('p',[text("Cat "),text("cat CAT")])])
len(model.search(words,"cat",true)) == 1
len(model.search(words,"cat",false)) == 3
let replaced = model.execute(model.open_editor(words),"edit.replaceAll",{find:"cat",replace:"Dog",match_case:false})^
doc_text(replaced.doc) == "Dog Dog Dog"
model.execute(replaced,"edit.undo")^.doc == words
let unicode = node('doc',[node('p',[text("Café 東京 CAFÉ")])])
len(model.search(unicode,"café",false)) == 2
model.search(unicode,"café",false)[1].head.offset == len("Café 東京 CAFÉ")
let folded_unicode = node('doc',[node('p',[text("İA i\u0307a")])])
len(model.search(folded_unicode,"i\u0307a",false)) == 2
model.search(folded_unicode,"i\u0307a",false)[0].head.offset == len("İA")
