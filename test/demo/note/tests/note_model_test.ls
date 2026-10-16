import m: ~~.mod_model
import sample: ~~.mod_sample
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_step
import lambda.editor.mod_source_pos

let st = m.initial(sample.notebook());
len(m.visible(st))==7
st.selected=="weekend"
not m.dirty(st)
m.count(st,"personal")==2 and m.count(st,"trash")==0
m.title(m.current(st))=="A slower kind of weekend"
contains(m.preview(m.current(st)),"Saturday")
m.visible({*:st,query:"  NOODLES "})[0].id=="kyoto"
len(m.visible({*:st,query:"no-such-note"}))==0
m.visible({*:st,sort:"title"})[0].id=="weekend"
let travel = m.scope(st,"travel");
travel.selected=="kyoto" and travel.query==""
not m.dirty(travel)
m.navigate(st,1).selected=="ideas"
let added = m.create(travel,"2026-10-16 11:00");
added.selected=="note-20" and m.current(added).folder=="travel"
added.sequence==21 and m.title(m.current(added))=="New Note"
m.dirty(added)
let typing = edit_exec(added.editor,edit_cmd_insert_text("A new thought"))
let written = m.update_editor(added,typing,"2026-10-16 11:01");
m.title(m.current(written))=="A new thought"
m.current(written).updated=="2026-10-16 11:01"
let switched = m.select(m.scope(written,"all"),"weekend")
let returned = m.select(switched,"note-20");
returned.editor.doc==written.editor.doc and edit_can_undo(returned.editor)
let undone = edit_exec(returned.editor,edit_cmd_history_undo());
m.title(m.current(m.update_editor(returned,undone,"2026-10-16 11:02")))=="New Note"
let moved = m.move(written,"work");
moved.selected=="kyoto" and m.count(moved,"work")==3
let deleted = m.change_note(st,{deleted:true})
let trash = m.scope(deleted,"trash");
m.count(deleted,"all")==6 and m.count(deleted,"trash")==1
trash.selected=="weekend"
m.count(m.change_note(trash,{deleted:false}),"all")==7
len(m.purge(trash).notes)==6
len(m.purge(st).notes)==7
let folder = m.add_folder(st,"  Recipes  ")^;
folder.folder=="folder-20" and len(folder.folders)==5 and folder.selected=="";
(m.add_folder(st,"Notes") ^ { null })==null;
(m.add_folder(st," ") ^ { null })==null;
let checked = m.toggle_task(st.editor,[3,1]);
attrs_get(node_at(checked.doc,[3,1]).attrs,'checked')==true
edit_exec(checked,edit_cmd_history_undo()).doc==st.editor.doc
let task = m.checklist(m.open_editor(node('doc',[m.paragraph("Bring coffee")])));
task.doc.content[0].tag=='ul'
attrs_get(task.doc.content[0].content[0].attrs,'checked')==false
edit_exec(task,edit_cmd_history_undo()).doc==node('doc',[m.paragraph("Bring coffee")])
