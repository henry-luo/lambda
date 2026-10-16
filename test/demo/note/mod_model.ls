// S1.4 / S12.1.3: notebook transitions are values; handlers own publication.
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import commands: lambda.editor.mod_commands
import html: lambda.edit.html
import controller: lambda.edit.controller

pub fn paragraph(words) => node('p',[text(words)])
pub fn blank() => node('doc',[paragraph("")])
pub fn open_editor(doc) => edit_open(doc,html.schema,null)
pub fn clean(editor) => {*:editor,events:[]}
fn detached(editor) => {*:clean(editor),mounted:false,preset:null,surface_handle:null,native_selection_revision:0}
pub fn note(id,folder,doc,updated,pinned=false) =>
    {id:id,folder:folder,editor:open_editor(doc),updated:updated,pinned:pinned,deleted:false}
pub fn current(st) => [for (n in st.notes where n.id==st.selected) n][0]
pub fn lines(n) => [for (block in n.editor.doc.content) trim(doc_text(block))]
pub fn title(n) => lines(n)[0] or "New Note"
pub fn preview(n) => slice(join([for (s in drop(lines(n),1) where s!="") s]," "),0,130) or "No additional text"
pub fn folder_name(st,id) => [for (f in st.folders where f.id==id) f.name][0] or "Notes"
pub fn scope_name(st) => if (st.folder=="all") "All Notes" else if (st.folder=="trash") "Recently Deleted" else folder_name(st,st.folder)
pub fn in_scope(n,folder) => if (folder=="trash") n.deleted else not n.deleted and (folder=="all" or n.folder==folder)
pub fn count(st,folder) => len([for (n in st.notes where in_scope(n,folder)) n])
pub fn visible(st) {
    let query = lower(trim(st.query))
    let rows = [for (n in st.notes where in_scope(n,st.folder) and
        (query=="" or contains(lower(join(lines(n)," ")),query))) n];
    let ordered = sort(rows,{dir:if (st.sort=="title") 'asc' else 'desc',
        by:(n)=>if (st.sort=="title") lower(title(n)) else n.updated});
    [*[for (n in ordered where n.pinned) n],*[for (n in ordered where not n.pinned) n]]
}
pub fn snapshot(st) => {folders:st.folders,notes:[for (n in st.notes)
    {id:n.id,folder:n.folder,doc:n.editor.doc,updated:n.updated,pinned:n.pinned,deleted:n.deleted}],sequence:st.sequence}
pub fn dirty(st) => snapshot(st)!=st.saved
pub fn select(st,id) {
    let chosen = [for (n in st.notes where n.id==id) n][0];
    if (chosen==null) {*:st,selected:"",editor:controller.replace(st.editor,open_editor(blank()))}
    else if (id==st.selected) st
    else {*:st,selected:id,editor:controller.replace(st.editor,chosen.editor)}
}
pub fn reconcile(st) {
    let rows = visible(st);
    if (contains([for (n in rows) n.id],st.selected)) st else select(st,rows[0].id or "")
}
pub fn scope(st,folder) => reconcile({*:st,folder:folder,query:""})
pub fn update_editor(st,editor,stamp) {
    let changed = editor.doc!=st.editor.doc;
    {*:st,editor:clean(editor),notes:[for (n in st.notes)
        if (n.id==st.selected) {*:n,editor:detached(editor),updated:if (changed) stamp else n.updated} else n]}
}
pub fn change_note(st,values) => reconcile({*:st,notes:[for (n in st.notes) if (n.id==st.selected) {*:n,*:values} else n]})
pub fn create(st,stamp) {
    let id = "note-" ++ string(st.sequence)
    let folder = if (contains(["all","trash"],st.folder)) "notes" else st.folder
    let next = {*:st,notes:[note(id,folder,blank(),stamp),*st.notes],sequence:st.sequence+1,folder:folder,query:""};
    select(next,id)
}
pub fn add_folder(st,label) map^ {
    let name = trim(label);
    if (name=="") raise error("Enter a folder name.")
    else if (len(name)>60) raise error("Use a folder name of at most 60 characters.")
    else if (any([for (f in st.folders) lower(f.name)==lower(name)])) raise error("That folder already exists.")
    else {
        let id = "folder-" ++ string(st.sequence);
        scope({*:st,folders:[*st.folders,{id:id,name:name}],sequence:st.sequence+1},id)
    }
}
pub fn move(st,folder) => if (not contains([for (f in st.folders) f.id],folder)) st else change_note(st,{folder:folder})
pub fn purge(st) => reconcile({*:st,notes:[for (n in st.notes where n.id!=st.selected or not n.deleted) n]})
pub fn navigate(st,delta) {
    let rows = visible(st)
    let at = [for (i,n in rows where n.id==st.selected) i][0] or 0;
    select(st,rows[max(0,min(len(rows)-1,at+delta))].id or "")
}
pub fn selection_path(editor) => if (editor.selection.kind=='text') editor.selection.head.path else editor.selection.path or []
pub fn task_path(editor,path=null) => commands.ancestor_tag(editor.doc,path or selection_path(editor),'li')
pub fn tasks(doc,path=[]) => if (not is_node(doc)) [] else [
    *if (doc.tag=='li' and attrs_get(doc.attrs,'checked')!=null) [{path:path,checked:attrs_get(doc.attrs,'checked')}] else [],
    *[for (i,child in doc.content) for (task in tasks(child,[*path,i])) task]]
pub fn toggle_task(editor,path=null) {
    let target = path or task_path(editor)
    let item = node_at(editor.doc,target);
    if (target==null or item.tag!='li' or attrs_get(item.attrs,'checked')==null) editor
    else edit_apply(editor,tx_step(tx_begin(editor.doc,editor.selection),
        step_set_attr(target,'checked',not attrs_get(item.attrs,'checked'))))
}
pub fn checklist(editor) {
    let inside = edit_inside(editor,'ul')
    let tx = if (inside) tx_begin(editor.doc,editor.selection) else commands.cmd_wrap_list(editor,'bullet');
    if (tx==null) editor else {
        let selected = {*:editor,doc:tx.doc_after,selection:tx.sel_after}
        let path = commands.ancestor_tag(selected.doc,selection_path(selected),'ul')
        let items = node_at(selected.doc,path)
        let remove = all([for (item in items.content) attrs_get(item.attrs,'checked')!=null]);
        // wrapping and task attributes form one undoable command.
        if (path==null) editor else edit_apply(editor,checklist_steps(tx,items,path,0,if (remove) null else false))
    }
}
fn checklist_steps(tx,items,path,i,value) => if (i>=len(items.content)) tx else
    checklist_steps(tx_step(tx,step_set_attr([*path,i],'checked',value)),items,path,i+1,value)
pub fn initial(book) {
    let notes = [for (n in book.notes) note(n.id,n.folder,n.doc,n.updated,n.pinned)]
    let st = {folders:book.folders,notes:[for (i,n in notes) {*:n,deleted:book.notes[i].deleted or false}],sequence:book.sequence,
        selected:"",editor:open_editor(blank()),folder:"all",query:"",sort:"updated",sidebar:true,
        dialog:null,pending:"",status:"On My Mac",path:"",disk_text:null,saved:null};
    let selected = reconcile(st);
    {*:selected,saved:snapshot(selected)}
}
