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
import focused: .mod_context
import tables: .mod_table
import ruler: .mod_ruler
import mini: .mod_selection_toolbar
import views: .mod_view
import navigation: .mod_navigation
import references: .mod_references
import shape_format: .mod_shape_format
import equation: .mod_equation
import equation_edit: .mod_equation_edit

let BASE = url_resolve(resources.entry_uri()^,".")
let CSS = input(url_resolve(BASE,"word.css"),'text')^

// keep render-only calculations out of the handler activation scope.
fn workspace(st) {
    let dirty = session.is_dirty(st.file_state,st.editor.doc)
    let words = model.word_count(st.editor.doc);
    let in_table = model.table_context(st.editor) != null
    let in_object = image.selected(st.editor)!=null
    let in_shape = shape_format.describe(image.selected(st.editor).node)!=null
    let in_equation = equation.settings(image.selected(st.editor).node)!=null
    let tab = if ((contains(["Table","Table Design"],st.tab) and not in_table) or (st.tab=="Object Format" and not in_object) or
        (st.tab=="Shape Format" and not in_shape) or (st.tab=="Equation" and not in_equation)) "Home" else st.tab;
    let paper = page.settings(st.editor.doc)
    let page_count = len(page.sheets(st.editor.doc))
    let layout = if (st.read_only) "print" else st.layout;
    let comment_css = review.css(st.editor.doc);
    <body id:"word", class:trim((if (st.focused) "focus-mode " else "") ++
        (if (st.nav and st.search_mode!=null) "search-open " else "") ++ (if (layout=="web") "web-layout " else "") ++
        (if (st.show_changes) "" else "hide-changes")), ["data-dirty"]:string(dirty), ["data-view"]:views.active(st.layout,st.read_only),
        ["data-revision"]:string(st.editor.model_revision), ["data-tab"]:tab, ["data-readonly"]:string(st.read_only),
        ["data-table-gridlines"]:string(st.table_gridlines),
        ["data-ribbon-group-labels"]:string(st.show_ribbon_group_labels),
        if (comment_css!="") <style comment_css> else null;
        <header class:"titlebar",
            <div class:"brand", <span class:"brand-mark", ui.icon("document",24)> <span "Word"> <span class:"brand-divider"> <span class:"brand-detail", "LAMBDA">>
            <div class:"document-title", st.file_state.envelope.title; <span class:"save-indicator", if (dirty) "  •  Unsaved" else "  •  Saved">>
            <div class:"title-actions", ui.button("save","file.save","Save","save"); ui.button("export","file.export","Export",ui.icon("export",16))>
        >
        <div class:"ribbon-shell",
            <nav class:"tabs", ["aria-label"]:"Ribbon tabs",
                ui.button("new","file.new","New"); ui.button("open","file.open","Open");
                <span class:"tab-divider">;
                *[for (name in ["Home","Insert","Layout","References","Review","View",*if (in_table) ["Table Design","Table"] else [],
                    *if (in_shape) ["Shape Format"] else [],*if (in_equation) ["Equation"] else [],*if (in_object) ["Object Format"] else []])
                    ui.button("tab-" ++ replace(lower(name)," ","-"),"ui.tab",if (name=="Table") "Table Layout" else name,null,{tab:name},tab==name,false,"tab")];
                <div class:"tab-spacer">
                ui.button("sample","file.sample","Studio example")>
            ui.ribbon(st.editor,tab,st.nav,st.read_only,st.show_changes,st.context.menu,st.fonts,st.layout,st.focused,st.table_pen,st.table_gridlines,st.object_picks,st.shape_draft,st.show_ribbon_group_labels)
        >
        <main class:"workspace",
            <div class:"navigation-slot", style:if (st.nav and not st.focused) "display:block" else "display:none",
                ui.navigation(st.editor,st.query,st.match_index,st.match_case,st.replace_text,st.search_mode,st.read_only,st.nav_tab)>
            ruler.workspace(st.editor,st.zoom,st.read_only,st.dialog!=null,
                    <section id:"stage",class:"stage",["aria-label"]:"Document pages",
                        handles.workspace(st.editor,st.zoom,st.read_only,st.dialog!=null,surface.surface(st.editor,st.zoom,st.read_only,layout),layout);
                        <div class:"canvas-footer", "A little room for your next idea.">>)
            if (st.review and not st.focused) ui.comments(st.editor,st.show_resolved,st.read_only) else null
            if (st.revisions and not st.focused) ui.revisions(st.editor,st.read_only) else null
            if (st.selection_pane and not st.focused) ui.selection_pane(st) else null
            if (st.shape_pane and not st.focused) ui.shape_pane(st) else null
            if (equation_edit.active(st) and not st.focused) ui.equation_pane(st) else null
        >
        <footer class:"statusbar",
            <div class:"status-left", <span class:"status-dot"> <span id:"status", role:"status", st.status> <span class:"status-divider"> <span id:"word-count", string(words) ++ " words">
                <span class:"status-divider"> <span id:"page-count-label",string(page_count) ++ (if (page_count==1) " page" else " pages")>>
            <div class:"status-right",
                ui.button("exit-focus","view.focusMode",if (st.focused) "Exit focus" else "Focus","focus",{},st.focused);
                <div id:"document-views",class:"document-views",role:"group",["aria-label"]:"Document views",
                    *ui.view_buttons(st.layout,st.read_only,true)>;
                <span id:"document-mode", "Draft · " ++ (if (st.read_only) "Reading" else "Writing")>
                <span class:"status-divider"> <span id:"paper-label",paper.paper ++ " · " ++ paper.orientation>
                ui.button("status-zoom-out","view.zoomOut","Zoom out","minus",{},false,false,"icon-only");
                <span id:"zoom-label", string(round(st.zoom*100)) ++ "%"> ui.button("status-zoom-in","view.zoomIn","Zoom in","plus",{},false,false,"icon-only")>
        >
        ui.selection_toolbar(st)
        ui.context_menu(st)
        ui.dialog(st.dialog,st.file_state,st.editor,st.read_only,st.fonts)
    >
}
edit <word_app> state st:app.initial() { workspace(st) }
on beforeinput() { return 'pass' }
on word_toolbar_ready(evt) {
    if (not ruler.dragging(evt.target)) mini.position(evt.target)
    return 'pass'
}
on word_object_select(req) {
    st = {*:st,object_picks:[],editor:image.select_image(app.mounted(st.editor,req.node),req.path)}
    app.focus_document(req.node)
    app.finish(st,req.node)
}
on word_table_select(req) {
    st = {*:st,context:null,editor:{*:edit_set_selection(app.mounted(st.editor,req.node),req.selection),
        table_range:req.range},status:"Table selection"}
    app.focus_document(req.node)
    app.finish(st,req.node)
}
on word_context_ready(evt) {
    if (st.context!=null) context.focus(evt.target,"word-context-menu")
}
on word_search_ready(evt) {
    let id = if (st.search_mode=="replace" and st.query!="") "replace-text" else "find-query"
    dom.focus_set(dom.get_element_by_id(dom.root_node(evt.target),id),false)
}
on word_navigation_tab_ready(evt) {
    dom.focus_set(dom.get_element_by_id(dom.root_node(evt.target),"nav-tab-" ++ st.nav_tab),false)
}
on word_navigation_ready(evt) {
    // wait for the new pane and the restored source range before scrolling, including in Read Mode.
    if (st.read_only) navigation.select_readonly(st.editor,controller.surface(evt.target))
    else controller.restore(tables.native_editor(st.editor))
    let root = dom.root_node(evt.target)
    let bounds = dom.selection_boundaries(dom.document_selection(evt.target))
    let target = if (st.nav_target.kind=="object") image.image_element(st.editor,controller.surface(evt.target))
        else if (st.nav_target.kind=="page") dom.query_selector(root,"#edit-surface .word-page[data-page=\"" ++ string(st.nav_target.page) ++ "\"]")
        else if (st.nav_target.kind=="heading") dom.query_selector_all(controller.surface(evt.target),"h1,h2,h3,h4,h5,h6")[st.nav_target.index]
        else if (dom.node_type(bounds.focus_node)==3) dom.parent_node(bounds.focus_node) else bounds.focus_node
    if (dom.contains(controller.surface(evt.target),target))
        dom.scroll_into_view_options(target,{block:if (st.nav_target.kind=="page") "start" else "center",inline:"nearest"})
}
on word_select_all(evt) {
    let host = controller.surface(evt.target)
    dom.selection_select_all_children(dom.document_selection(host),host)
}
on word_ruler_geometry_ready(evt) { ruler.position(st.editor,evt.target,st.zoom); return 'pass' }
on word_equation_ready(evt) { equation_edit.focus(st,evt.target); return 'pass' }
on word_ruler_end(evt) { app.refresh_toolbar(st,evt.target); return 'pass' }
on word_viewport_changed(evt) {
    ruler.position(st.editor,evt.target,st.zoom)
    if (st.layout=="web" and not st.read_only) {
        handles.fit_surface(evt.target)
        handles.position(st.editor,evt.target,st.zoom)
    }
    if (mini.visible(st) and not ruler.dragging(evt.target)) mini.refresh(evt.target)
}
on resize(evt) {
    if (st.layout=="web" and not st.read_only)
        dom.request_frame(dom.get_element_by_id(dom.root_node(evt.target),"word"),"word_viewport_changed")
    if (mini.visible(st)) mini.refresh(evt.target)
    return 'pass'
}
on dblclick(evt) {
    if (st.read_only) { return 'pass' }
    if (dom.closest(evt.target,"[data-word-citation]")!=null) {
        st = app.command(app.adopt(st,evt),"ui.editCitation",{},evt.target)
        app.finish(st,evt.target)
        return 'prevent-default'
    }
    let story = dom.closest(evt.target,".word-story")
    if (story!=null) {
        st = app.command(st,"ui.headerFooter",{},evt.target)
        app.finish(st,evt.target)
        return 'prevent-default'
    }
    let path = handles.hit(st.editor,evt.target)
    if (path!=null) {
        st = {*:st,editor:image.select_image(app.mounted(st.editor,evt.target),path)}
        st = app.command(st,if (equation.settings(image.selected(st.editor).node)!=null) "ui.equationEdit" else "ui.pictureSettings",{},evt.target)
        app.finish(st,evt.target)
        return 'prevent-default'
    }
    return 'pass'
}
on mousedown(evt) {
    // the ruler owns this gesture; redrawing the shell here would retire its capture target.
    if (dom.closest(evt.target,".ruler-control")!=null) { return 'prevent-default' }
    // changing shell state without a popup would reset a just-started object gesture.
    if (mini.visible(st) and dom.closest(evt.target,"#selection-toolbar,#edit-surface")==null) { st = app.hide_toolbar(st) }
    // contextmenu precedes mousedown; keep its saved range out of native hit testing.
    if (evt.button==1 and st.context!=null and dom.closest(evt.target,"#edit-surface,#object-handles")!=null) { return 'prevent-default' }
    if (st.context!=null and dom.closest(evt.target,"#word-context-menu,.ribbon-menu-trigger")==null) { st = {*:st,context:null} }
    return 'pass'
}
on contextmenu(evt) {
    if (st.dialog!=null or dom.closest(evt.target,"#edit-surface,#object-handles")==null) { return 'pass' }
    let path = handles.hit(st.editor,evt.target)
    let story = focused.story(evt.target)
    let editor = app.mounted(app.adopt(st,evt).editor,evt.target)
    let keep_cells = tables.structural(editor) and any([for (entry in tables.selected(editor))
        path_is_prefix(entry.path,evt.source_pos.path)])
    let target = if (keep_cells) editor else if (path!=null) image.select_image(editor,path)
        else if (story==null) focused.at_pointer(editor,evt.source_pos,false) else editor
    // the hit position targets this item; the old range survives only a click inside it.
    st = {*:st,editor:if (keep_cells or path!=null) target
        else if (story==null) focused.at_pointer(editor,evt.source_pos) else editor}
    // a read-mode menu retains plain selection text while its chrome is rendered.
    let copied_text = dom.selection_to_string(dom.document_selection(evt.target))
    st = {*:app.hide_toolbar(st),context:{x:evt.clientX,y:evt.clientY,copy_text:copied_text,story:story,item_selection:target.selection,
        cell_path:tables.hit(st.editor,evt.target)}}
    app.finish(st,evt.target)
    dom.request_frame(dom.get_element_by_id(dom.root_node(evt.target),"word"),"word_context_ready")
    return 'prevent-default'
}
on editaction(evt) {
    // pn handlers discard success values (S7.6.7v4); inspect this returned outcome explicitly.
    let run = tables.handle(st.editor,evt,st.read_only,if (app.selected_all(st,evt.target)) all_selection() else null)
    if (run is error) {
        st = {*:st,status:run.message}
        return {*:edit_result.model_applied(false,false,false,"",st.editor.selection,st.editor.model_revision),failure:run.message}
    }
    if (st.read_only or evt.input_type=="copy") { return run.result }
    st = {*:st,object_picks:[],editor:model.clean(run.editor),status:if (not (run.result.failure is null)) "This edit is unavailable here." else "Writing"}
    dom.set_close_guard(evt.target,session.is_dirty(st.file_state,st.editor.doc))
    return run.result
}
on selectionchange(evt) {
    let next = app.selection_changed(st,evt)
    if (next!=st) { st = next }
}
on word_selectionchange(req) {
    let next = app.selection_changed(st,req.event)
    if (next!=st) { st = next }
}
on keyup(evt) {
    // native navigation completes after keydown; promote its final source range.
    if (st.dialog==null and not tables.structural(st.editor) and not app.is_control(evt.target) and
        contains(["ArrowUp","ArrowDown","ArrowLeft","ArrowRight","Home","End","PageUp","PageDown"],evt.key)) {
        st = app.selection_changed(st,evt)
        if (evt.shiftKey and st.mini_hidden and dom.closest(evt.target,"#edit-surface")!=null) {
            st = {*:st,mini_hidden:false}
            app.finish(st,evt.target)
        }
    }
    return 'pass'
}
on word_command(req) {
    if (dom.closest(req.node,"#selection-toolbar")==null) { st = app.hide_toolbar(st) }
    let menu = st.context
    if (req.id=="ui.editingMenu") {
        st = app.command(if (menu!=null or st.read_only) st else app.adopt(st,req.event),req.id,req.params,req.node)
        app.finish(st,req.node)
        return 'handled'
    }
    // deletion already has a source selection; a clipboard round trip can lose block atoms.
    let id = if (req.id=="clipboard.delete") (if (tables.structural(st.editor)) "table.clear" else "text.delete") else req.id
    // menu commands keep the source target captured when the menu opened.
    st = {*:if (st.read_only or menu!=null) st else app.adopt(st,req.event),context:null}
    if (req.selection!=null and not st.read_only) {
        st = {*:st,editor:edit_set_selection(st.editor,req.selection)}
    }
    if (starts_with(id,"clipboard.")) {
        if (st.read_only and id=="clipboard.copy") {
            dom.clipboard_write(menu.copy_text or "")
            return 'handled'
        }
        if (id=="clipboard.selectAll") {
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
        let command = {cut:"deleteByCut",copy:"copy",paste:"insertFromPaste"}[slice(id,10)]
        dom.keyboard_command(host,command)
        return 'handled'
    }
    st = app.command(st,id,req.params,req.node,req.ruler_focus!=null)
    app.finish(st,req.node)
    if (req.ruler_focus!=null) {
        st = {*:st,ruler_focus:req.ruler_focus}
        dom.request_frame(dom.get_element_by_id(dom.document_element(req.node),"word"),"word_ruler_focus_ready")
    }
}
on word_ruler_focus_ready(evt) {
    dom.focus_set(dom.get_element_by_id(dom.root_node(evt.target),st.ruler_focus),false)
    return 'pass'
}
on edit_dialog(req) {
    st = app.command(st,"dialog." ++ req.action,{},req.node)
    app.finish(st,req.node)
}
on control_change(req) {
    if (dom.closest(req.node,"#selection-toolbar")==null) { st = app.hide_toolbar(st) }
    if (req.action=="find.query") { st = {*:st,query:req.value,match_index:-1,nav_tab:"results"} }
    else if (req.action=="find.replacement") { st = {*:st,replace_text:req.value} }
    else {
        st = app.command(app.adopt(st,req.event),req.action,req.params,req.node)
        // live size edits retain the control caret; restoring the document range steals its focus.
        app.finish(st,req.node,not contains(["ui.tableSize","ui.shapeField","ui.equationDraft"],req.action))
    }
}
on keydown(evt) {
    if (app.primary(evt) and evt.key=="Enter" and dom.closest(evt.target,"#equation-editor-pane")!=null) {
        st = app.command(st,"ui.equationApply",{},evt.target)
        app.finish(st,evt.target)
        return 'prevent-default'
    }
    // a child widget that claimed the key owns its command and focus restoration.
    if (evt.defaultPrevented==true and dom.closest(evt.target,"#ruler-workspace")!=null) { return 'pass' }
    if (evt.key=="Escape" and mini.visible(st)) {
        st = app.hide_toolbar(st)
        app.focus_document(evt.target)
        app.finish(st,evt.target)
        return 'prevent-default'
    }
    if (evt.key=="Tab" and dom.closest(evt.target,"#selection-toolbar,.ruler-control")!=null) { return 'pass' }
    if (st.context==null and st.dialog==null and tables.structural(st.editor) and
        (evt.key=="Escape" or contains(["ArrowUp","ArrowDown","ArrowLeft","ArrowRight"],evt.key))) {
        if (evt.shiftKey and evt.key!="Escape") {
            st = {*:st,editor:tables.extend(st.editor,evt.key)}
            app.finish(st,evt.target)
            return 'prevent-default'
        }
        st = {*:st,editor:edit_set_selection(st.editor,tables.native_editor(st.editor).selection)}
        app.finish(st,evt.target)
        if (evt.key=="Escape") { return 'prevent-default' }
        return 'pass'
    }
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
            copy_text:dom.selection_to_string(dom.document_selection(evt.target)),story:focused.story(evt.target)}}
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
