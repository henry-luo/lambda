// S12.1.3 / D7.2.5: event handlers select model commands and shared UA policy.
import dom
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import controller: lambda.edit.controller
import session: lambda.edit.session
import model: .mod_model
import sample: .mod_sample
import storage: .mod_io

pub fn initial() => {*:model.initial(sample.notebook()),menu:"",context:null,picker_value:"",picker_error:"",
    last_color:model.default_colors.color,last_highlight:model.default_colors.highlight}
pub pn open_context(st,evt,x=null,y=null) {
    if (st.dialog!=null) { return st }
    let row = dom.closest(evt.target,".note-row")
    let surface = dom.closest(evt.target,"#edit-surface")
    if (row==null and surface==null) { return st }
    // a context-menu hit names a pointer position, not a replacement for the existing text range.
    let has_range = st.editor.selection.kind=='node' or (st.editor.selection.kind=='text' and st.editor.selection.anchor!=st.editor.selection.head)
    let next = if (row==null) (if (has_range) st else adopt(st,evt))
        else command(st,"note.select",{id:dom.get_attribute(row,"data-note")},evt.target)
    return {*:next,menu:"context",context:{kind:if (row==null) "text" else "note",
        x:if (x==null) evt.clientX else x,y:if (y==null) evt.clientY else y}}
}
pub fn primary(evt) => evt.metaKey==true or evt.ctrlKey==true
pub pn stamp() { return format(now(),"YYYY-MM-DD HH:mm") }
pub pn adopt(st,evt) {
    if (st.selected=="" or st.dialog!=null) { return st }
    if (model.selected_image(st.editor)!=null and dom.closest(evt.target,"#edit-surface")==null) { return st }
    let accepted = controller.adopt(st.editor,evt)
    return if (accepted.changed) {*:st,editor:model.clean(accepted.editor)} else st
}
pub pn finish(st,node,restore=false) {
    dom.set_close_guard(node,model.dirty(st))
    dom.set_window_title(node,(if (model.dirty(st)) "● " else "") ++ "Notes")
    controller.restore(st.editor,restore and st.dialog==null and st.selected!="")
}
fn file_dialog(kind,path="") => {kind:kind,title:if (kind=="save") "Save Notebook" else "Open Notebook",
    message:if (kind=="save") "Keep all your notes in a local .note.json notebook." else "Choose a local .note.json notebook.",
    value:path,confirm:if (kind=="save") "Save" else "Open",error:""}
pn continued(st,action,node) {
    if (action=="close") {
        dom.set_close_guard(node,false)
        dom.request_window_close(node)
        return {*:st,dialog:null,pending:""}
    }
    return {*:st,dialog:file_dialog("open"),pending:"",menu:""}
}
pn save_book(st,path,node,overwrite=false) {
    if (starts_with(path,"temp/note/")) { io.mkdir("temp/note") ^ { null } }
    let book = model.snapshot(st)
    let result = session.save(storage.file_state(st)^,book,path,overwrite)
    let next = if (result.ok) {*:st,path:result.session.path,disk_text:result.session.disk_text,saved:book,
        dialog:null,status:result.status,menu:""} else {*:st,status:result.status,
        dialog:if (result.conflict!=null) {kind:"overwrite",title:if (result.conflict=='changed') "Notebook Changed" else "Replace Notebook?",
            message:result.status,value:path,confirm:"Replace",error:""}
            else file_dialog("save",path) |> {*:~,error:result.status}};
    if (result.ok and st.pending!="") { return continued(next,st.pending,node) }
    return next
}
pn dialog_command(st,id,node) {
    if (id=="dialog.cancel") { controller.focus(node); return {*:st,dialog:null,pending:""} }
    if (id=="dialog.discard") { return continued(st,st.pending,node) }
    let d = st.dialog
    if (d.kind=="unsaved") { return command({*:st,dialog:null},"file.save",{},node) }
    if (d.kind=="purge") { return {*:model.purge(st),dialog:null,status:"Note deleted permanently"} }
    if (d.kind=="folder") {
        let next = model.add_folder(st,d.value) ^ { return {*:st,dialog:{*:d,error:^.message}} }
        return {*:next,dialog:null,status:"Folder created"}
    }
    let path = trim(d.value or "")
    if (path=="") { return {*:st,dialog:{*:d,error:"Enter a notebook path."}} }
    if (d.kind=="open") {
        let loaded = storage.read_book(path) ^ { return {*:st,dialog:{*:d,error:^.message}} }
        controller.focus(node)
        return {*:st,*:loaded,menu:"",context:null,picker_error:"",sidebar:st.sidebar,editor:controller.replace(st.editor,loaded.editor)}
    }
    return save_book(st,path,node,d.kind=="overwrite")
}
pn edit_command(st,id,params,node) {
    if (st.selected=="" or model.current(st).deleted) { return st }
    let editor = controller.mount(st.editor,node)
    let inputs = {bold:"formatBold",italic:"formatItalic",underline:"formatUnderline",undo:"historyUndo",redo:"historyRedo",
        bullets:"insertUnorderedList",table:"insertTable",style:"formatBlock"}
    let action = slice(id,5)
    var next = editor
    if (id=="edit.color" or id=="edit.highlight") {
        next = model.format_color(editor,action,params.color) ^ { return {*:st,picker_error:^.message} }
    }
    else if (id=="edit.checklist") { next = model.checklist(editor) }
    else if (id=="edit.task") { next = model.toggle_task(editor,params.path) }
    else {
        let run = controller.request(editor,inputs[action],if (action=="table") {rows:3,cols:2,header:false} else params)
        next = run.editor
    }
    controller.focus(node)
    return {*:model.update_editor(st,next,stamp()),menu:"",status:"On My Mac",picker_error:"",
        last_color:if (action=="color") model.color_value(next,"color") else st.last_color,
        last_highlight:if (action=="highlight") model.color_value(next,"highlight") else st.last_highlight}
}
pub pn command(st,id,params,node) {
    if (starts_with(id,"dialog.")) { return dialog_command(st,id,node) }
    if (st.dialog!=null) { return st }
    if (starts_with(id,"edit.")) { return edit_command(st,id,params,node) }
    if (id=="note.new") {
        controller.focus(node)
        return {*:model.create(st,stamp()),menu:"",status:"New note"}
    }
    if (id=="image.resize") {
        let next = model.resize_image(st.editor,params.path,params.width,params.height) ^ { return {*:st,status:^.message} }
        controller.focus(node)
        return {*:model.update_editor(st,next,stamp()),menu:"",status:"Image resized"}
    }
    if (id=="note.select") { return {*:model.select(st,params.id),menu:""} }
    if (id=="folder.select") { return {*:model.scope(st,params.folder),menu:""} }
    if (id=="sidebar.toggle") { return {*:st,sidebar:not st.sidebar} }
    if (id=="list.sort") { return {*:st,sort:if (st.sort=="updated") "title" else "updated"} }
    if (starts_with(id,"menu.")) {
        let kind = slice(id,5)
        return {*:st,menu:if (st.menu==kind) "" else kind,picker_error:"",
            picker_value:if (kind=="color" or kind=="highlight") model.color_value(st.editor,kind) else st.picker_value}
    }
    if (id=="picker.apply") { return edit_command(st,"edit." ++ st.menu,{color:st.picker_value},node) }
    if (id=="folder.new") { return {*:st,menu:"",dialog:{kind:"folder",title:"New Folder",message:"A little space for related ideas.",value:"",confirm:"Create",error:""}} }
    if (id=="note.pin" and st.selected!="") { return model.change_note(st,{pinned:not model.current(st).pinned}) }
    if (id=="note.move") { return {*:model.move(st,params.folder),menu:"",status:"Note moved"} }
    if (id=="note.restore") { return {*:model.change_note(st,{deleted:false}),status:"Note restored"} }
    if (id=="note.delete" and st.selected!="") {
        if (model.current(st).deleted) { return {*:st,dialog:{kind:"purge",title:"Delete Note Permanently?",
            message:"This note will be removed from your notebook. This cannot be undone.",confirm:"Delete",error:""}} }
        return {*:model.change_note(st,{deleted:true}),status:"Moved to Recently Deleted"}
    }
    if (id=="file.saveAs" or id=="file.save") {
        if (id=="file.saveAs" or st.path=="") { return {*:st,menu:"",dialog:file_dialog("save",if (st.path=="") "temp/note/Notebook.note.json" else st.path)} }
        return save_book(st,st.path,node)
    }
    if (id=="file.open" or id=="file.close") {
        if (model.dirty(st)) { return {*:st,menu:"",pending:if (id=="file.close") "close" else "open",
            dialog:{kind:"unsaved",title:"Save Your Notes?",message:"Your notebook has changes that have not been saved.",confirm:"Save…",error:""}} }
        return continued(st,if (id=="file.close") "close" else "open",node)
    }
    return st
}
pub pn control_change(st,req) {
    if (req.action=="dialog.value") { return {*:st,dialog:{*:st.dialog,value:req.value,error:""}} }
    if (st.dialog!=null) { return st }
    if (req.action=="picker.value") { return {*:st,picker_value:req.value,picker_error:""} }
    if (req.action=="search") { return model.reconcile({*:st,query:req.value,menu:""}) }
    if (req.action=="note.move") { return command(st,"note.move",{folder:req.value},req.node) }
    return st
}
