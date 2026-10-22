import model: ~~.mod_model
import changes: ~~.mod_changes
import files: ~~.mod_io
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos

let original = node('doc',[node('p',[text("Hello world")])])
let editor = model.open_editor(original)
let enabled = model.execute(editor,"review.trackChanges",{enabled:true,author:"Alex"})^
changes.settings(enabled.doc)=={enabled:true,author:"Alex"}
let selected = edit_set_selection(enabled,text_selection(pos([0,0],6),pos([0,0],11)))
let replaced = model.execute(selected,"text.insert",{data:"team"})^
model.plain_text(replaced.doc)=="Hello team"
doc_text(replaced.doc)=="Hello worldteam"
changes.entries(replaced.doc)==[{id:"r1",author:"Alex",inserted:"team",removed:"world"}]
model.execute(replaced,"edit.undo")^.doc==enabled.doc
model.execute(model.execute(replaced,"edit.undo")^,"edit.redo")^.doc==replaced.doc
let rejected = model.execute(replaced,"review.reject",{id:"r1"})^
model.plain_text(rejected.doc)=="Hello world"
len(changes.entries(rejected.doc))==0
let accepted = model.execute(replaced,"review.accept",{id:"r1"})^
model.plain_text(accepted.doc)=="Hello team"
len(changes.entries(accepted.doc))==0
model.execute(accepted,"edit.undo")^.doc==replaced.doc
let continued = model.execute(replaced,"text.insert",{data:"!"})^
model.plain_text(continued.doc)=="Hello team!"
len(changes.entries(continued.doc))==1
changes.entries(continued.doc)[0].inserted=="team!"
let erased = model.execute(continued,"text.backspace")^
model.plain_text(erased.doc)=="Hello team"
changes.entries(erased.doc)[0].inserted=="team"
let second_caret = edit_set_selection(replaced,text_selection(pos([0,0],0),pos([0,0],0)))
let second = model.execute(second_caret,"text.insert",{data:"Dear "})^
len(changes.entries(second.doc))==2
model.plain_text(second.doc)=="Dear Hello team"
model.plain_text(model.execute(second,"review.reject",{id:"r1"})^.doc)=="Dear Hello world"
model.plain_text(model.execute(second,"review.rejectAll")^.doc)=="Hello world"
model.plain_text(model.execute(second,"review.acceptAll")^.doc)=="Dear Hello team"
let removed = model.execute(edit_set_selection(enabled,text_selection(pos([0,0],0),pos([0,0],5))),"text.backspace")^
model.plain_text(removed.doc)==" world"
changes.entries(removed.doc)[0].removed=="Hello"
model.plain_text(model.execute(removed,"review.rejectAll")^.doc)=="Hello world"
let unicode = model.execute(enabled,"text.insert",{data:"秋Ω"})^
model.plain_text(unicode.doc)=="秋ΩHello world"
changes.entries(unicode.doc)[0].inserted=="秋Ω"
model.execute(enabled,"text.enter") ^ { true } ~ { false }
model.execute(enabled,"insert.table",{rows:1,cols:1,header:false}) ^ { true } ~ { false }
files.restore(files.archive(second.doc,"Tracked"))^.doc==second.doc
changes.entries(files.import_html_document(files.html_document(second.doc,"Tracked")^)^)==changes.entries(second.doc)
model.execute(second,"review.reject",{id:"missing"}) ^ { true } ~ { false }
let off = model.execute(second,"review.trackChanges",{enabled:false})^
len(changes.entries(model.execute(off,"text.insert",{data:"plain"})^.doc))==2
// A rejected insertion must leave a usable caret, and typing runs must coalesce.
let empty = model.execute(model.open_editor(node('doc',[node('p',[text("")])])),"review.trackChanges",{enabled:true,author:"Alex"})^
let only = model.execute(empty,"text.insert",{data:"New"})^
let empty_again = model.execute(only,"review.rejectAll")^
model.plain_text(empty_again.doc)==""
model.plain_text(model.execute(empty_again,"text.insert",{data:"Again"})^.doc)=="Again"
fn typing(ed,n) => if (n==0) ed else typing(model.execute(ed,"text.insert",{data:"a"})^,n-1)
let typed = typing(empty,20)
len(changes.entries(typed.doc))==1
len(node_at(typed.doc,[0]).content)==1
changes.entries(typed.doc)[0].inserted==join([for (i in 1 to 20) "a"],"")
let original_marks = [mark('strong',true)]
let styled = model.execute(model.open_editor(node('doc',[node('p',[text_marked("One",original_marks),text(" two")])])),"review.trackChanges",{enabled:true,author:"Lee"})^
let amended = model.execute(edit_set_selection(styled,text_selection(pos([0,0],0),pos([0,1],4))),"text.insert",{data:"Three"})^
model.plain_text(amended.doc)=="Three"
doc_text(model.execute(amended,"review.rejectAll")^.doc)=="One two"
node_at(model.execute(amended,"review.rejectAll")^.doc,[0,0]).marks==original_marks
let emoji = model.execute(model.open_editor(node('doc',[node('p',[text("秋🌱 café")])])),"review.trackChanges",{enabled:true,author:"Me"})^
let emoji_edit = model.execute(edit_set_selection(emoji,text_selection(pos([0,0],1),pos([0,0],2))),"text.insert",{data:"☀"})^
model.plain_text(emoji_edit.doc)=="秋☀ café"
changes.entries(emoji_edit.doc)[0].removed=="🌱"
let cell = model.execute(model.open_editor(node('doc',[node('table',[node('tr',[node('td',[text("Cell")])])])])),"review.trackChanges",{enabled:true,author:"Alex"})^
model.plain_text(model.execute(cell,"text.insert",{data:"New "})^.doc)=="New Cell"
changes.entries(model.execute(cell,"text.insert",{data:"New "})^.doc)[0].inserted=="New "
