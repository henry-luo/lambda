// One controller serves child-widget events and root keyboard handlers.
// emit() targets ancestors, so root handlers call this controller directly.
import dom
import lambda.editor.mod_editor
import session: lambda.edit.session
import fields: lambda.edit.files
import document: .mod_document
import model: .mod_model
import storage: .mod_io

pub fn initial() => {editor:model.initial(),file_state:storage.new_session(document.sample(),"The Open Studio Handbook"),
    tab:"Home",dialog:null,pending:"",status:"Ready to write",query:"",match_index:-1,
    zoom:1.0,nav:true,read_only:false,focused:false}
pub pn mounted(editor,node) {
    if (editor.mounted) editor else edit_mount(editor,dom.get_element_by_id(dom.root_node(node),"edit-surface"),"html5_subset")
}
pub pn field(node,id) { fields.field_value(node,id) }
pub pn focus_document(node) { fields.focus_surface(node) }
pub pn is_control(node) { dom.node_type(node)==1 and dom.matches(node,"input,textarea,select") }
pub fn primary(evt) => evt.metaKey == true or evt.ctrlKey == true
// A click can move the native caret without selectionchange. Adopt the action's
// source snapshot before a toolbar/keyboard command consumes the selection.
pub fn adopt(st,evt) => if (evt.source_selection==null and evt.source_pos==null) st
    else {*:st,editor:edit_accept_dom_selection(st.editor,evt).editor}
pn replaced_editor(previous,next) {
    // Files share one logical surface. Keep its handle and monotone revision
    // so completion resolves the new DOM rather than retaining the old host.
    return {*:next,mounted:previous.mounted,preset:previous.preset,surface_handle:previous.surface_handle,
        model_revision:previous.model_revision+1,native_selection_revision:previous.native_selection_revision}
}
pub pn finish(st,node) {
    // Bind only once an editing action owns a selection. View-only changes
    // before the first caret placement have no model selection to publish.
    if (st.editor.mounted and not st.read_only and st.dialog==null) edit_restore_selection(st.editor)
    let dirty = session.is_dirty(st.file_state,st.editor.doc)
    dom.set_close_guard(node,dirty)
    dom.set_window_title(node,(if (dirty) "● " else "") ++ st.file_state.envelope.title ++ " — Word")
}
pn continued(st,id,node) {
    if (id == "file.open") { return {*:st,pending:"",dialog:{kind:"open",path:""}} }
    if (id == "file.close") {
        dom.set_close_guard(node,false)
        dom.request_window_close(node)
        return {*:st,pending:"",dialog:null}
    }
    let doc = if (id == "file.new") document.blank() else document.sample()
    return {*:st,editor:replaced_editor(st.editor,model.open_editor(doc)),pending:"",dialog:null,query:"",match_index:-1,
        status:"Ready to write",file_state:storage.new_session(doc,
            if (id == "file.new") "Untitled document" else "The Open Studio Handbook")}
}
pn saved(st,path,node,overwrite=false) {
    if (starts_with(path,"temp/word/")) { io.mkdir("temp/word") ^ { null } }
    let result = session.save(st.file_state,st.editor.doc,path,overwrite)
    let next = {*:st,file_state:result.session,status:result.status,
        dialog:if (result.ok) null else if (result.conflict != null)
            {kind:string(result.conflict),path:path,action:"save"} else st.dialog}
    if (result.ok and st.pending != "") continued(next,st.pending,node) else next
}
pn model_command(st,id,params,node) {
    let editor = mounted(st.editor,node)
    let next = model.execute(editor,id,params) ^ { return {*:st,editor:editor,status:^.message} }
    focus_document(node)
    return {*:st,editor:model.clean(next),status:model.spec(id).label}
}
pn dialog_command(st,action,node) {
    if (action == "cancel") { focus_document(node); return {*:st,dialog:null,pending:""} }
    if (action == "discard") { return continued(st,st.pending,node) }
    if (action == "save") { return command({*:st,dialog:null},"file.save",{},node) }
    if (action == "link") {
        let href = trim(field(node,"link-address"))
        let label = field(node,"link-label")
        if (not (starts_with(href,"https://") or starts_with(href,"http://") or starts_with(href,"mailto:") or starts_with(href,"#"))) {
            return {*:st,status:"Enter an http, https, mailto, or fragment address."}
        }
        return model_command({*:st,dialog:null},"insert.link",{href:href,title:"",label:if (label=="") href else label},node)
    }
    let path = if (action == "overwrite") st.dialog.path else trim(field(node,"word-file-path"))
    let kind = if (action == "overwrite") st.dialog.action else st.dialog.kind
    if (kind == "open") {
        let opened = storage.read_document(path) ^ { return {*:st,status:^.message} }
        focus_document(node)
        return {*:st,editor:replaced_editor(st.editor,opened.editor),file_state:opened.session,status:opened.status,dialog:null,query:"",match_index:-1}
    }
    if (kind == "save") { return saved(st,path,node,action == "overwrite") }
    if (kind == "export") {
        if (exists(path) and action != "overwrite") { return {*:st,dialog:{kind:"exists",path:path,action:"export"}} }
        if (starts_with(path,"temp/word/")) { io.mkdir("temp/word") ^ { null } }
        let exported: any | error = storage.export_document(st.editor.doc,st.file_state.envelope.title,path)
        if (exported is error) { return {*:st,status:exported.message} }
        focus_document(node)
        return {*:st,status:"Exported " ++ path,dialog:null}
    }
    return st
}
pub pn command(st,id,params,node) {
    if (starts_with(id,"dialog.")) { return dialog_command(st,slice(id,7),node) }
    if (id == "ui.tab") { return {*:st,tab:params.tab} }
    if (id == "ui.escape") {
        if (st.dialog!=null) { return dialog_command(st,"cancel",node) }
        return {*:st,focused:false}
    }
    if (id == "ui.find") {
        dom.focus_set(dom.get_element_by_id(dom.root_node(node),"find-query"),false)
        return {*:st,nav:true}
    }
    if (id == "view.navigationPane") { return {*:st,nav:not st.nav} }
    if (id == "view.readMode") { return {*:st,read_only:not st.read_only,status:if (st.read_only) "Ready to write" else "Read mode"} }
    if (id == "view.focusMode") { return {*:st,focused:not st.focused} }
    if (starts_with(id,"view.zoom")) { return {*:st,zoom:if (id == "view.zoomReset") 1.0
        else min(1.5,max(0.5,st.zoom + if (id == "view.zoomIn") 0.1 else -0.1))} }
    if (id == "ui.heading") {
        focus_document(node)
        return {*:st,editor:model.select_heading(mounted(st.editor,node),params.path)}
    }
    if (id == "ui.link") { return {*:st,dialog:{kind:"link"}} }
    if (id == "file.save" or id == "file.saveAs") {
        if (st.file_state.path=="" or id == "file.saveAs") { return {*:st,dialog:{kind:"save",
            path:if (st.file_state.path=="") "temp/word/document.word.json" else st.file_state.path}} }
        return saved(st,st.file_state.path,node)
    }
    if (id == "file.export") { return {*:st,dialog:{kind:"export",path:"temp/word/document.pdf"}} }
    if (contains(["file.new","file.sample","file.open","file.close"],id)) {
        if (session.is_dirty(st.file_state,st.editor.doc)) { return {*:st,pending:id,dialog:{kind:"unsaved"}} }
        return continued(st,id,node)
    }
    if (id == "edit.findNext" or id == "edit.findPrevious") {
        let hits = model.search(st.editor.doc,st.query)
        if (len(hits)==0) { return {*:st,status:"No matching text"} }
        let index = (st.match_index + (if (id == "edit.findNext") 1 else -1) + len(hits)) % len(hits)
        focus_document(node)
        return {*:st,editor:edit_set_selection(mounted(st.editor,node),hits[index]),match_index:index,
            status:"Match " ++ string(index+1) ++ " of " ++ string(len(hits))}
    }
    if (st.read_only) { return st }
    if (id == "edit.replace") {
        let hits = model.search(st.editor.doc,st.query)
        if (len(hits)==0) { return {*:st,status:"No matching text"} }
        let selected = [for (hit in hits where hit == st.editor.selection) hit][0]
        return model_command({*:st,editor:edit_set_selection(st.editor,selected or hits[0])},"text.insert",{data:field(node,"replace-text")},node)
    }
    let payload = if (id == "edit.replaceAll") {find:st.query,replace:field(node,"replace-text")} else params
    return model_command(st,id,payload,node)
}
