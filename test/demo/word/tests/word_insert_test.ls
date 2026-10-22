import model: ~~.mod_model
import insert: ~~.mod_insert
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let doc = node('doc',[node('p',[text("Hello world")]),node('p',[text("Second paragraph")])])
let editor = model.open_editor(doc)
let covered = model.execute(editor,"insert.coverPage",{title:"Studio journal",subtitle:"Ideas for autumn",author:"Alex"})^
len(covered.doc.content)==5
attrs_get(covered.doc.content[0].attrs,'data-word-cover')=="title"
doc_text(covered.doc.content[0])=="Studio journal"
attrs_get(covered.doc.content[3].attrs,'data-word-page-break')=="true"
covered.doc.content[4]==doc.content[1]
covered.selection==text_selection(pos([0,0],0),pos([0,0],14))
model.execute(covered,"edit.undo")^.doc==doc
model.execute(model.execute(covered,"edit.undo")^,"edit.redo")^.doc==covered.doc
files.restore(files.archive(covered.doc,"Cover"))^.doc==covered.doc
attrs_get(files.import_html_document(files.html_document(covered.doc,"Cover")^)^.content[0].attrs,'data-word-cover')=="title"
model.execute(editor,"insert.coverPage",{title:"  ",subtitle:"",author:""}) ^ { true } ~ { false }

let caret = edit_set_selection(editor,text_selection(pos([0,0],5),pos([0,0],5)))
let blank = model.execute(caret,"insert.blankPage")^
len(blank.doc.content)==4
model.plain_text(blank.doc)=="Hello\n\n world\nSecond paragraph"
all([for (i in [1,2]) attrs_get(blank.doc.content[i].attrs,'data-word-page-break')=="true"])
blank.selection==text_selection(pos([1,0],0),pos([1,0],0))
model.execute(blank,"edit.undo")^.doc==doc
files.restore(files.archive(blank.doc,"Blank"))^.doc==blank.doc
let at_end = edit_set_selection(editor,text_selection(pos([0,0],11),pos([0,0],11)))
len(model.execute(at_end,"insert.blankPage")^.doc.content)==4
len(model.execute(model.open_editor(node('doc',[node('p',[])])),"insert.blankPage")^.doc.content)==3
let table = model.execute(editor,"insert.table",{rows:1,cols:1,header:false})^
model.execute(table,"insert.blankPage") ^ { true } ~ { false }

let selected = edit_set_selection(editor,text_selection(pos([0,0],5),pos([0,0],0)))
let inserted = model.execute(selected,"insert.symbol",{char:"Ω ± €"})^
doc_text(inserted.doc.content[0])=="Ω ± € world"
inserted.selection.head.offset==len("Ω ± €")
model.execute(inserted,"edit.undo")^.doc==doc
model.execute(editor,"insert.symbol",{char:""}) ^ { true } ~ { false }
model.execute(editor,"insert.symbol",{char:"a\nb"}) ^ { true } ~ { false }
model.execute(editor,"insert.symbol",{char:join([for (i in 1 to 65) "x"],"")}) ^ { true } ~ { false }

let marked = model.execute(caret,"insert.bookmark",{name:"Intro"})^
insert.bookmarks(marked.doc)==[{name:"Intro",path:[0],label:"Hello world"}]
attrs_get(marked.doc.content[0].attrs,'id')=="word-bookmark-Intro"
model.execute(marked,"edit.undo")^.doc==doc
let typed = model.execute(marked,"text.insert",{data:"!"})^
insert.bookmarks(typed.doc)[0].label=="Hello! world"
let split = model.execute(marked,"text.enter")^
len(insert.bookmarks(split.doc))==1
insert.bookmarks(split.doc)[0].path==[0]
let second = edit_set_selection(marked,text_selection(pos([1,0],0),pos([1,0],0)))
let moved = model.execute(second,"insert.bookmark",{name:"Intro"})^
insert.bookmarks(moved.doc)==[{name:"Intro",path:[1],label:"Second paragraph"}]
attrs_get(moved.doc.content[0].attrs,'id')==null
model.execute(moved,"edit.undo")^.doc==marked.doc
let renamed = model.execute(moved,"insert.bookmark",{name:"Notes_2"})^
insert.bookmarks(renamed.doc)[0].name=="Notes_2"
model.execute(editor,"insert.bookmark",{name:"1bad"}) ^ { true } ~ { false }
model.execute(editor,"insert.bookmark",{name:"has space"}) ^ { true } ~ { false }
let deleted = model.execute(renamed,"insert.deleteBookmark",{name:"Notes_2"})^
len(insert.bookmarks(deleted.doc))==0
model.execute(deleted,"edit.undo")^.doc==renamed.doc
model.execute(renamed,"insert.deleteBookmark",{name:"missing"}) ^ { true } ~ { false }
insert.bookmark_selection(marked.doc,"Intro")==text_selection(pos([0,0],0),pos([0,0],0))
insert.bookmark_selection(doc,"missing")==null
files.restore(files.archive(marked.doc,"Bookmarks"))^.doc==marked.doc
let imported = files.import_html_document(files.html_document(marked.doc,"Bookmarks")^)^
insert.bookmarks(imported)==insert.bookmarks(marked.doc)
let link = model.execute(second,"insert.link",{href:"#word-bookmark-Intro",label:"Read the introduction"})^
contains(files.html_document(link.doc,"Links")^,"href=\"#word-bookmark-Intro\"")
let named = model.open_editor(node('doc',[node_attrs('p',[{name:'id',value:"original-target"}],[text("Keep my ID")])]))
model.execute(named,"insert.bookmark",{name:"New"}) ^ { true } ~ { false }
let unicode_title = join([for (i in 1 to 200) "秋"],"")
doc_text(model.execute(editor,"insert.coverPage",{title:unicode_title,subtitle:"",author:""})^.doc.content[0])==unicode_title
let unicode_chars = join([for (i in 1 to 64) "Ω"],"")
starts_with(doc_text(model.execute(editor,"insert.symbol",{char:unicode_chars})^.doc),unicode_chars)
model.execute(editor,"insert.symbol",{char:"\u0001"}) ^ { true } ~ { false }
