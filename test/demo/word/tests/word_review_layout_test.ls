import model: ~~.mod_model
import page: ~~.mod_page
import review: ~~.mod_review
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let doc = node('doc',[node('p',[text("Hello world")])])
let editor = model.open_editor(doc)
page.settings(doc) == page.defaults
page.geometry(doc) == {width:794,height:1123,top:64,right:72,bottom:72,left:72}
let setup = {paper:"Letter",orientation:"landscape",top:12.7,right:12.7,bottom:12.7,left:12.7}
let landscape = model.execute(editor,"layout.pageSetup",setup)^
page.settings(landscape.doc) == setup
page.geometry(landscape.doc).width == 1056
page.geometry(landscape.doc).height == 816
model.execute(landscape,"edit.undo")^.doc == doc
model.execute(landscape,"layout.pageSetup",{paper:"A5",orientation:"portrait",left:80,right:80}) ^ { true } ~ { false }
model.execute(editor,"layout.pageSetup",{paper:"unknown"}) ^ { true } ~ { false }
let caret = edit_set_selection(landscape,text_selection(pos([0,0],5),pos([0,0],5)))
let broken = model.execute(caret,"insert.pageBreak")^
len(broken.doc.content)==2
attrs_get(broken.doc.content[1].attrs,'data-word-page-break') == "true"
model.plain_text(broken.doc) == "Hello\n world"
model.execute(broken,"edit.undo")^.doc == landscape.doc
contains(files.html_document(broken.doc,"Pages")^,"279.4mm 215.9mm")
files.import_html_document(files.html_document(broken.doc,"Pages")^)^.attrs == broken.doc.attrs

let selected = edit_set_selection(editor,text_selection(pos([0,0],0),pos([0,0],5)))
model.can_comment(selected)
not model.can_comment(editor)
let commented = model.execute(selected,"review.addComment",{author:"Alice",text:" Clarify this "})^
review.comments(commented.doc) == [{id:"c1",author:"Alice",text:"Clarify this",quote:"Hello",resolved:false}]
model.comment_selection(commented,"c1") == commented.selection
review.has_anchor(commented.doc.content[0].content[0].marks,"c1")
not review.has_anchor(commented.doc.content[0].content[1].marks,"c1")
model.execute(commented,"edit.undo")^.doc == doc
let bold = model.execute(commented,"format.bold")^
let cleared = model.execute(bold,"format.clear")^
review.has_anchor(cleared.doc.content[0].content[0].marks,"c1")
not has_mark(cleared.doc.content[0].content[0].marks,'strong')
let overlap = model.execute(commented,"review.addComment",{author:"Bob",text:"Second opinion"})^
len(review.comments(overlap.doc))==2
review.ids(overlap.doc.content[0].content[0].marks) == ["c1","c2"]
let resolved = model.execute(overlap,"review.resolveComment",{id:"c1",resolved:true})^
review.comments(resolved.doc)[0].resolved
contains(review.css(resolved.doc),":not([data-word-comment~=\"c2\"])")
let deleted = model.execute(resolved,"review.deleteComment",{id:"c1"})^
len(review.comments(deleted.doc))==1
review.ids(deleted.doc.content[0].content[0].marks) == ["c2"]
model.execute(deleted,"edit.undo")^.doc == resolved.doc
files.restore(files.archive(resolved.doc,"Comments"))^.doc == resolved.doc
let imported = files.import_html_document(files.html_document(resolved.doc,"Comments")^ )^
review.comments(imported) == review.comments(resolved.doc)
review.has_anchor(model.leaves(imported)[0].leaf.marks,"c2")
let typed = model.execute(commented,"text.insert",{data:"Hi"})^
model.plain_text(typed.doc) == "Hi world"
model.comment_selection(typed,"c1") != null
model.execute(editor,"review.addComment",{text:"No selection"}) ^ { true } ~ { false }
model.execute(commented,"review.addComment",{text:"  "}) ^ { true } ~ { false }
model.execute(commented,"review.deleteComment",{id:"missing"}) ^ { true } ~ { false }
let empty = node('doc',[node('p',[])])
model.execute(model.open_editor(empty),"text.insert",{data:"Start again"})^.doc.content[0].content[0].text == "Start again"
files.restore(files.archive(empty,"Empty paragraph"))^.doc == empty
let unanchored = edit_set_selection(overlap,text_selection(pos([0,1],0),pos([0,1],0)))
model.next_comment_id(unanchored,false,true) == "c1"
model.next_comment_id(unanchored,false,false) == "c2"
model.next_comment_id(resolved,false,true) == "c2"
model.next_comment_id(editor,false,true) == null
