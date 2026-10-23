// Native Lambda/Radiant port of WordCraft's command-driven writing workspace.
// S12.1.3 / D7.2.5: handlers own state; the body only projects the model.
import dom
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import edit_result: lambda.dom.edit_result
import session: lambda.edit.session
import resources: ~~.mod_resources
import model: .mod_model
import ui: .mod_ui
import surface: .mod_surface
import app: .mod_app
import page: .mod_page
import review: .mod_review
import controller: lambda.edit.controller
import image: lambda.editor.mod_image
import changes: .mod_changes
import handles: .mod_handles
import context: ~~.mod_context_menu

let BASE = url_resolve(resources.entry_uri()^,".")
let CSS = input(url_resolve(BASE,"word.css"),'text')^

edit <word_app> state st:app.initial() {
    let dirty = session.is_dirty(st.file_state,st.editor.doc)
    let words = model.word_count(st.editor.doc);
    let in_table = model.table_context(st.editor) != null
    let tab = if (st.tab=="Table" and not in_table) "Home" else st.tab;
    let paper = page.settings(st.editor.doc)
    let page_count = len(page.sheets(st.editor.doc))
    let geometry = page.geometry(st.editor.doc);
    <body id:"word", class:trim((if (st.focused) "focus-mode " else "") ++ (if (st.show_changes) "" else "hide-changes")), ["data-dirty"]:string(dirty),
        ["data-revision"]:string(st.editor.model_revision), ["data-tab"]:tab, ["data-readonly"]:string(st.read_only),
        <style review.css(st.editor.doc)>
        <header class:"titlebar",
            <div class:"brand", <span class:"brand-mark", ui.icon("document",24)> <span "Word"> <span class:"brand-divider"> <span class:"brand-detail", "LAMBDA">>
            <div class:"document-title", st.file_state.envelope.title; <span class:"save-indicator", if (dirty) "  •  Unsaved" else "  •  Saved">>
            <div class:"title-actions", ui.button("save","file.save","Save","save"); ui.button("export","file.export","Export","export")>
        >
        <div class:"ribbon-shell",
            <nav class:"tabs", ["aria-label"]:"Ribbon tabs",
                ui.button("new","file.new","New"); ui.button("open","file.open","Open");
                <span class:"tab-divider">;
                *[for (name in ["Home","Insert","Layout","Review","View",*if (in_table) ["Table"] else []]) ui.button("tab-" ++ lower(name),"ui.tab",name,null,{tab:name},tab==name,false,"tab")];
                <div class:"tab-spacer">
                ui.button("sample","file.sample","Studio example")>
            ui.ribbon(st.editor,tab,st.nav,st.read_only,st.show_changes)
        >
        <main class:"workspace",
            <div class:"navigation-slot", style:if (st.nav and not st.focused) "display:block" else "display:none",
                ui.navigation(st.editor,st.query,st.match_index,st.match_case,st.replace_text)>
            <section id:"stage", class:"stage", ["aria-label"]:"Document workspace",
                <div class:"canvas-heading", <span "DRAFT / " ++ (if (st.read_only) "READING" else "WRITING")> <span paper.paper ++ " · " ++ paper.orientation ++ " · " ++ string(page_count) ++ (if (page_count==1) " page" else " pages")>>
                <div class:"ruler", style:"width:" ++ string(geometry.width*st.zoom) ++ "px",
                    *[for (n in 0 to 16) <span string(n)>]>
                <div id:"paper-wrap", class:"paper-wrap", style:"width:" ++ string(geometry.width*st.zoom) ++ "px; min-height:" ++ string(geometry.height*st.zoom) ++ "px;",
                    surface.surface(st.editor,st.zoom,st.read_only)
                    if (st.read_only or st.dialog!=null) null else handles.tools(st.editor,st.zoom)>
                <div class:"canvas-footer", "A little room for your next idea.">
            >
            if (st.review and not st.focused) ui.comments(st.editor,st.show_resolved,st.read_only) else null
            if (st.revisions and not st.focused) ui.revisions(st.editor,st.read_only) else null
        >
        <footer class:"statusbar",
            <div class:"status-left", <span class:"status-dot"> <span id:"status", role:"status", st.status> <span class:"status-divider"> <span id:"word-count", string(words) ++ " words">>
            <div class:"status-right",
                ui.button("exit-focus","view.focusMode",if (st.focused) "Exit focus" else "Focus");
                <span id:"paper-label",paper.paper> ui.button("status-zoom-out","view.zoomOut","Zoom out","minus",{},false,false,"icon-only");
                <span id:"zoom-label", string(round(st.zoom*100)) ++ "%"> ui.button("status-zoom-in","view.zoomIn","Zoom in","plus",{},false,false,"icon-only")>
        >
        ui.context_menu(st)
        ui.dialog(st.dialog,st.file_state,st.editor,st.read_only)
    >
}
on beforeinput() { return 'pass' }
on word_object_position(evt) { handles.position(st.editor,evt.node or evt.target,st.zoom) }
on word_context_ready(evt) {
    if (st.context!=null) context.focus(evt.target,"word-context-menu")
}
on word_select_all(evt) {
    let host = controller.surface(evt.target)
    dom.selection_select_all_children(dom.document_selection(host),host)
}
on dblclick(evt) {
    if (st.read_only) { return 'pass' }
    let story = dom.closest(evt.target,".word-story")
    if (story!=null) {
        st = app.command(st,"ui.headerFooter",{},evt.target)
        app.finish(st,evt.target)
        return 'prevent-default'
    }
    let path = image.hit(st.editor,evt.target,controller.surface(evt.target))
    if (path!=null) {
        st = {*:st,editor:image.select_image(app.mounted(st.editor,evt.target),path)}
        st = app.command(st,"ui.pictureSettings",{},evt.target)
        app.finish(st,evt.target)
        return 'prevent-default'
    }
    return 'pass'
}
on mousedown(evt) {
    // contextmenu precedes mousedown; keep its saved range out of native hit testing.
    if (evt.button==1 and st.context!=null and dom.closest(evt.target,"#edit-surface")!=null) { return 'prevent-default' }
    if (st.context!=null and dom.closest(evt.target,"#word-context-menu")==null) { st = {*:st,context:null} }
    let path = if (evt.button==0 and not st.read_only and st.dialog==null)
        image.hit(st.editor,evt.target,controller.surface(evt.target)) else null
    if (path==null) { return 'pass' }
    // Claim the atom before native text hit testing can replace its node selection.
    st = {*:st,editor:image.select_image(app.mounted(st.editor,evt.target),path)}
    app.focus_document(evt.target)
    app.finish(st,evt.target)
    return 'prevent-default'
}
on contextmenu(evt) {
    if (st.dialog!=null or dom.closest(evt.target,"#edit-surface")==null) { return 'pass' }
    let path = image.hit(st.editor,evt.target,controller.surface(evt.target))
    if (path!=null) { st = {*:st,editor:image.select_image(app.mounted(st.editor,evt.target),path)} }
    // right-clicking a selected range must not adopt the pointer's collapsed caret.
    if (st.read_only or not context.has_selection(st.editor.selection)) { st = app.adopt(st,evt) }
    // a read-mode menu retains plain selection text while its chrome is rendered.
    let copied_text = dom.selection_to_string(dom.document_selection(evt.target))
    st = {*:st,context:{x:evt.clientX,y:evt.clientY,copy_text:copied_text}}
    app.finish(st,evt.target)
    dom.request_frame(dom.get_element_by_id(dom.root_node(evt.target),"word"),"word_context_ready")
    return 'prevent-default'
}
on editaction(evt) {
    // pn handlers discard success values (S7.6.7v4); inspect this returned outcome explicitly.
    let run = controller.handle(st.editor,evt,st.read_only)
    if (run is error) {
        st = {*:st,status:run.message}
        return {*:edit_result.model_applied(false,false,false,"",st.editor.selection,st.editor.model_revision),failure:run.message}
    }
    if (st.read_only or evt.input_type=="copy") { return run.result }
    st = {*:st,editor:model.clean(run.editor),status:if (not (run.result.failure is null)) "This edit is unavailable here." else "Writing"}
    dom.set_close_guard(evt.target,session.is_dirty(st.file_state,st.editor.doc))
    return run.result
}
on selectionchange(evt) {
    if (st.read_only or st.context!=null) { return 'pass' }
    let adopted = app.adopt(st,evt,true)
    if (adopted.editor!=st.editor) {
        st = {*:adopted,editor:model.clean(adopted.editor)}
        controller.restore(st.editor)
    }
}
on word_command(req) {
    let menu = st.context
    st = {*:if (st.read_only) st else app.adopt(st,req.event),context:null}
    if (starts_with(req.id,"clipboard.")) {
        if (st.read_only and req.id=="clipboard.copy") {
            dom.clipboard_write(menu.copy_text or "")
            return 'handled'
        }
        if (req.id=="clipboard.selectAll") {
            if (st.read_only) {
                // static ranges bind to the live surface after the menu's removal.
                dom.request_frame(dom.get_element_by_id(dom.root_node(req.node),"word"),"word_select_all")
                return 'handled'
            }
            st = {*:st,editor:edit_set_selection(app.mounted(st.editor,req.node),all_selection())}
            app.focus_document(req.node)
            app.finish(st,req.node)
            return 'handled'
        }
        app.finish(st,req.node)
        if (not st.read_only) { app.focus_document(req.node) }
        let host = controller.surface(req.node)
        let command = {cut:"deleteByCut",copy:"copy",paste:"insertFromPaste",delete:"deleteContentForward"}[slice(req.id,10)]
        dom.keyboard_command(host,command)
        return 'handled'
    }
    st = app.command(st,req.id,req.params,req.node)
    app.finish(st,req.node)
}
on edit_dialog(req) {
    st = app.command(st,"dialog." ++ req.action,{},req.node)
    app.finish(st,req.node)
}
on control_change(req) {
    if (req.action=="find.query") { st = {*:st,query:req.value,match_index:-1} }
    else if (req.action=="find.replacement") { st = {*:st,replace_text:req.value} }
    else {
        st = app.command(app.adopt(st,req.event),req.action,req.params,req.node)
        app.finish(st,req.node)
    }
}
on keydown(evt) {
    if (st.context!=null) {
        if (contains(["ArrowUp","ArrowDown","Home","End"],evt.key)) {
            context.navigate(evt.target,"word-context-menu",evt.key)
            return 'prevent-default'
        }
        if (contains(["Escape","Tab"],evt.key)) {
            st = {*:st,context:null}
            app.focus_document(evt.target)
            app.finish(st,evt.target)
            return 'prevent-default'
        }
    }
    if (st.dialog==null and (evt.key=="ContextMenu" or (evt.key=="F10" and evt.shiftKey)) and
        dom.closest(evt.target,"#edit-surface")!=null) {
        let box = dom.bounding_box(evt.target)
        st = {*:app.adopt(st,evt),context:{x:box.x+20,y:box.y+20,
            copy_text:dom.selection_to_string(dom.document_selection(evt.target))}}
        app.finish(st,evt.target)
        dom.request_frame(dom.get_element_by_id(dom.root_node(evt.target),"word"),"word_context_ready")
        return 'prevent-default'
    }
    let key = lower(string(evt.key))
    let adopted = app.adopt(st,evt)
    let id = if (evt.key=="Escape") "ui.escape"
        else if (app.primary(evt) and key=="s") (if (evt.shiftKey) "file.saveAs" else "file.save")
        else if (app.primary(evt) and key=="f") "ui.find"
        else if (app.primary(evt) and key=="k" and st.dialog==null and not app.is_control(evt.target)) "ui.link"
        else if (app.primary(evt) and evt.altKey and key=="m" and st.dialog==null and not app.is_control(evt.target)) "ui.comment"
        else if (app.primary(evt) and evt.key=="Enter" and st.dialog==null and not app.is_control(evt.target)) "insert.pageBreak"
        else if (evt.key=="Tab" and st.dialog==null and not st.read_only and not app.is_control(evt.target) and model.table_context(adopted.editor)!=null)
            (if (evt.shiftKey) "table.previousCell" else "table.nextCell")
        else if (evt.key=="Tab" and st.dialog==null and not st.read_only and not app.is_control(evt.target) and model.current_node(adopted.editor,'li')!=null)
            (if (evt.shiftKey) "para.outdent" else "para.indent")
        else if (app.primary(evt) and key==" " and st.dialog==null and not app.is_control(evt.target)) "format.clear"
        else if (app.primary(evt) and st.dialog==null and not app.is_control(evt.target)) model.shortcut(key,evt.shiftKey==true)
        else null
    if (id==null) { return 'pass' }
    st = app.command({*:adopted,context:null},id,{},evt.target)
    app.finish(st,evt.target)
    return 'prevent-default'
}
on closerequest(evt) {
    st = app.command(st,"file.close",{},evt.target)
    app.finish(st,evt.target)
    return 'prevent-default'
}

<html lang:"en", <head <meta charset:"utf-8"> <title "Word — Lambda workspace"> <style CSS ++ surface.document_css ++ changes.css>>
    apply(<word_app>,{mode:"edit"})>
