// One controller serves child-widget events and root keyboard handlers.
// emit() targets ancestors, so root handlers call this controller directly.
import dom
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_editor
import session: lambda.edit.session
import fields: lambda.edit.files
import document: .mod_document
import model: .mod_model
import storage: .mod_io
import page: .mod_page
import insert: .mod_insert
import controller: lambda.edit.controller

pub fn initial() => {editor:model.initial(),file_state:storage.new_session(document.sample(),"The Open Studio Handbook"),
    tab:"Home",dialog:null,pending:"",status:"Ready to write",query:"",replace_text:"",match_case:false,match_index:-1,
    zoom:1.0,nav:true,read_only:false,focused:false,review:false,show_resolved:false}
pub let mounted = controller.mount
pub pn field(node,id) { fields.field_value(node,id) }
pub let focus_document = controller.focus
pub let is_control = controller.is_control
pub fn primary(evt) => evt.metaKey == true or evt.ctrlKey == true
pub pn adopt(st,evt,changes_only=false) {
    let accepted = controller.adopt(st.editor,evt)
    if (changes_only and not accepted.changed) { return st }
    return {*:st,editor:accepted.editor}
}
let replaced_editor = controller.replace
pub pn finish(st,node) {
    // Bind only once an editing action owns a selection. View-only changes
    // before the first caret placement have no model selection to publish.
    controller.restore(st.editor,not st.read_only and st.dialog==null)
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
    let next = model.execute(editor,id,params) ^ { return {*:st,editor:editor,status:^.message,
        dialog:if (st.dialog==null) null else {*:st.dialog,error:^.message}} }
    focus_document(node)
    return {*:st,editor:model.clean(next),dialog:null,status:model.spec(id).label,
        review:st.review or starts_with(id,"review.")}
}
pn dialog_model(st,id,values,params,node) {
    return model_command({*:st,dialog:{*:st.dialog,values:values}},id,params,node)
}
pn dialog_command(st,action,node) {
    if (action == "cancel") { focus_document(node); return {*:st,dialog:null,pending:""} }
    if (action == "discard") { return continued(st,st.pending,node) }
    if (action == "save") { return command({*:st,dialog:null},"file.save",{},node) }
    if (action == "insert_table") {
        let values = {rows:field(node,"table-rows"),cols:field(node,"table-columns"),header:field(node,"table-header")}
        return dialog_model(st,"insert.table",values,{rows:float(values.rows),cols:float(values.cols),header:values.header=="true"},node)
    }
    if (action == "page_setup") {
        let values = {paper:field(node,"page-paper"),orientation:field(node,"page-orientation"),top:field(node,"page-top"),
            right:field(node,"page-right"),bottom:field(node,"page-bottom"),left:field(node,"page-left")}
        return dialog_model(st,"layout.pageSetup",values,{*:values,top:float(values.top),right:float(values.right),
            bottom:float(values.bottom),left:float(values.left)},node)
    }
    if (action == "comment") {
        let values = {author:field(node,"comment-author"),text:field(node,"comment-text")}
        return dialog_model(st,"review.addComment",values,values,node)
    }
    if (action == "cover") {
        let values = {title:field(node,"cover-title"),subtitle:field(node,"cover-subtitle"),author:field(node,"cover-author")}
        return dialog_model(st,"insert.coverPage",values,values,node)
    }
    if (action == "bookmark") {
        let values = {name:field(node,"bookmark-name")}
        return dialog_model(st,"insert.bookmark",values,values,node)
    }
    if (action == "symbol") {
        let values = {char:field(node,"symbol-text")}
        return dialog_model(st,"insert.symbol",values,values,node)
    }
    if (action == "internal_link") {
        let name = field(node,"link-bookmark")
        let values = {href:insert.bookmark_href(name),label:field(node,"link-label")}
        let target = [for (b in insert.bookmarks(st.editor.doc) where b.name==name) b][0]
        if (target==null) { return {*:st,dialog:{*:st.dialog,values:values,error:"Choose an existing bookmark."}} }
        return dialog_model(st,"insert.link",values,values,node)
    }
    if (action == "paragraph") {
        let values = {align:field(node,"paragraph-align"),lines:field(node,"paragraph-lines"),before:field(node,"paragraph-before"),
            after:field(node,"paragraph-after"),indent:field(node,"paragraph-indent"),first:field(node,"paragraph-first")}
        return dialog_model(st,"para.set",values,{align:values.align,lines:float(values.lines),before:float(values.before),
            after:float(values.after),indent:float(values.indent),first:float(values.first)},node)
    }
    if (action == "picture") {
        let values = {path:field(node,"picture-path"),alt:field(node,"picture-alt"),width:field(node,"picture-width"),height:field(node,"picture-height")}
        let editing = st.dialog.kind=="picture_settings"
        let source = if (editing) "" else storage.picture_source(trim(values.path)) ^ {
            return {*:st,dialog:{*:st.dialog,values:values,error:^.message},status:^.message}
        }
        return dialog_model(st,if (editing) "picture.size" else "insert.picture",values,
            {src:source,alt:values.alt,width:float(values.width),height:if (trim(values.height)=="") 0 else float(values.height)},node)
    }
    if (action == "link") {
        let href = trim(field(node,"link-address"))
        let label = field(node,"link-label")
        if (not (starts_with(href,"https://") or starts_with(href,"http://") or starts_with(href,"mailto:") or starts_with(href,"#"))) {
            return {*:st,dialog:{*:st.dialog,values:{href:href,label:label},error:"Enter an http, https, mailto, or fragment address."}}
        }
        return dialog_model(st,"insert.link",{href:href,label:label},{href:href,title:"",label:label},node)
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
    if (id == "ui.tab") { return {*:st,tab:params.tab,review:st.review or params.tab=="Review"} }
    if (id == "ui.cover") {
        if (st.read_only) { return st }
        return {*:st,dialog:{kind:"cover",values:{title:st.file_state.envelope.title,subtitle:"Document subtitle",author:"Me"}}}
    }
    if (id == "ui.bookmarks") {
        return {*:st,dialog:{kind:"bookmarks",values:{name:insert.bookmark_name(model.current_block(st.editor).node) or ""}}}
    }
    if (id == "ui.symbol") {
        if (st.read_only) { return st }
        return {*:st,dialog:{kind:"symbol",values:{char:"©"}}}
    }
    if (id == "ui.chooseSymbol") {
        if (st.dialog==null or st.dialog.kind!="symbol") { return st }
        return {*:st,dialog:{*:st.dialog,values:{char:params.char},error:null}}
    }
    if (id == "ui.bookmarkText" or id == "ui.followLink") {
        let href = attrs_get(model.current_node(st.editor,'a').node.attrs,'href') or ""
        let name = if (id=="ui.bookmarkText") params.name else insert.link_bookmark(href)
        let selection = insert.bookmark_selection(st.editor.doc,name)
        if (selection==null) { return {*:st,status:"This bookmark is no longer in the document."} }
        focus_document(node)
        let target = dom.get_element_by_id(dom.root_node(node),insert.bookmark_id(name))
        if (target!=null) dom.scroll_into_view(target)
        return {*:st,dialog:null,editor:edit_set_selection(mounted(st.editor,node),selection),status:"Bookmark: " ++ name}
    }
    if (id == "ui.comments") { return {*:st,review:not st.review} }
    if (id == "ui.resolvedComments") { return {*:st,show_resolved:not st.show_resolved} }
    if (id == "ui.comment") {
        if (st.read_only) { return st }
        if (not model.can_comment(st.editor)) { return {*:st,status:"Select the text you want to comment on."} }
        return {*:st,dialog:{kind:"comment",values:{author:"Me",text:""}}}
    }
    if (id == "ui.commentText" or id == "ui.nextComment" or id == "ui.previousComment") {
        let target_id = if (id=="ui.commentText") params.id else model.next_comment_id(st.editor,st.show_resolved,id=="ui.nextComment")
        let selection = model.comment_selection(st.editor,target_id)
        if (selection==null) { return {*:st,review:true,status:"The commented text is no longer in the document."} }
        focus_document(node)
        let anchor = dom.query_selector(dom.root_node(node),"#edit-surface [data-word-comment~=\"" ++ target_id ++ "\"]")
        if (anchor!=null) dom.scroll_into_view(anchor)
        return {*:st,review:true,editor:edit_set_selection(mounted(st.editor,node),selection)}
    }
    if (id == "ui.matchCase") { return {*:st,match_case:not st.match_case,match_index:-1} }
    if (id == "ui.clearFind") { return {*:st,query:"",match_index:-1} }
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
    if (contains(["ui.link","ui.table","ui.picture","ui.pictureSettings","ui.paragraph","ui.pageSetup"],id)) {
        if (st.read_only) { return st }
        if (id == "ui.link") {
            let link = model.current_node(st.editor,'a')
            return {*:st,dialog:{kind:"link",editing:link!=null,values:{href:attrs_get(link.node.attrs,'href') or "",label:doc_text(link.node) or ""}}}
        }
        if (id == "ui.table") { return {*:st,dialog:{kind:"table",values:{rows:3,cols:3,header:"true"}}} }
        if (id == "ui.paragraph") { return {*:st,dialog:{kind:"paragraph",values:model.paragraph_settings(st.editor)}} }
        if (id == "ui.pageSetup") { return {*:st,dialog:{kind:"page_setup",values:page.settings(st.editor.doc)}} }
        let image = model.current_node(st.editor,'img')
        return {*:st,dialog:{kind:if (id == "ui.pictureSettings") "picture_settings" else "picture",
            values:{path:"",alt:attrs_get(image.node.attrs,'alt') or "",width:attrs_get(image.node.attrs,'width') or 320,
                height:attrs_get(image.node.attrs,'height') or ""}}}
    }
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
        let hits = model.search(st.editor.doc,st.query,st.match_case)
        if (len(hits)==0) { return {*:st,status:"No matching text"} }
        let index = (st.match_index + (if (id == "edit.findNext") 1 else -1) + len(hits)) % len(hits)
        focus_document(node)
        return {*:st,editor:edit_set_selection(mounted(st.editor,node),hits[index]),match_index:index,
            status:"Match " ++ string(index+1) ++ " of " ++ string(len(hits))}
    }
    if (st.read_only) { return st }
    if (id == "edit.replace") {
        let hits = model.search(st.editor.doc,st.query,st.match_case)
        if (len(hits)==0) { return {*:st,status:"No matching text"} }
        let selected = [for (hit in hits where hit == st.editor.selection) hit][0]
        return model_command({*:st,editor:edit_set_selection(st.editor,selected or hits[0])},"text.insert",{data:st.replace_text},node)
    }
    let payload = if (id == "edit.replaceAll") {find:st.query,replace:st.replace_text,match_case:st.match_case} else params
    return model_command(st,id,payload,node)
}
