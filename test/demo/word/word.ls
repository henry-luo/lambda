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

let BASE = url_resolve(resources.entry_uri()^,".")
let CSS = input(url_resolve(BASE,"word.css"),'text')^

edit <word_app> state st:app.initial() {
    let dirty = session.is_dirty(st.file_state,st.editor.doc)
    let words = model.word_count(st.editor.doc);
    <body id:"word", class:if (st.focused) "focus-mode" else "", ["data-dirty"]:string(dirty),
        ["data-revision"]:string(st.editor.model_revision), ["data-tab"]:st.tab, ["data-readonly"]:string(st.read_only),
        <header class:"titlebar",
            <div class:"brand", <span class:"brand-mark", "W"> <span "Word"> <span class:"brand-divider"> <span class:"brand-detail", "A LAMBDA WORKSPACE">>
            <div class:"document-title", st.file_state.envelope.title; <span class:"save-indicator", if (dirty) "  •  Unsaved" else "  •  Saved">>
            <div class:"title-actions", ui.button("save","file.save","Save","↥"); ui.button("export","file.export","Export","↗")>
        >
        <div class:"ribbon-shell",
            <nav class:"tabs", ["aria-label"]:"Ribbon tabs",
                ui.button("new","file.new","New"); ui.button("open","file.open","Open");
                <span class:"tab-divider">;
                *[for (name in ["Home","Insert","View"]) ui.button("tab-" ++ lower(name),"ui.tab",name,null,{tab:name},st.tab==name,false,"tab")];
                <div class:"tab-spacer">
                ui.button("sample","file.sample","Studio example")>
            ui.ribbon(st.editor,st.tab,st.nav,st.read_only)
        >
        <main class:"workspace",
            <div class:"navigation-slot", style:if (st.nav and not st.focused) "display:block" else "display:none",
                ui.navigation(st.editor,st.query,st.match_index)>
            <section id:"stage", class:"stage", ["aria-label"]:"Document workspace",
                <div class:"canvas-heading", <span "DRAFT / " ++ (if (st.read_only) "READING" else "WRITING")> <span "A4 · Continuous canvas">>
                <div class:"ruler", style:"width:" ++ string(794*st.zoom) ++ "px",
                    *[for (n in 0 to 16) <span string(n)>]>
                <div id:"paper-wrap", class:"paper-wrap", style:"width:" ++ string(794*st.zoom) ++ "px; min-height:" ++ string(1123*st.zoom) ++ "px;",
                    surface.surface(st.editor,st.zoom,st.read_only)>
                <div class:"canvas-footer", "A little room for your next idea.">
            >
        >
        <footer class:"statusbar",
            <div class:"status-left", <span class:"status-dot"> <span id:"status", role:"status", st.status> <span class:"status-divider"> <span id:"word-count", string(words) ++ " words">>
            <div class:"status-right",
                ui.button("exit-focus","view.focusMode",if (st.focused) "Exit focus" else "Focus");
                <span "A4"> ui.button("status-zoom-out","view.zoomOut","−");
                <span id:"zoom-label", string(round(st.zoom*100)) ++ "%"> ui.button("status-zoom-in","view.zoomIn","+")>
        >
        ui.dialog(st.dialog,st.file_state)
    >
}
on beforeinput() { return 'pass' }
on editaction(evt) {
    if (st.read_only) { return edit_result.decline(true,false,"read-only",0) }
    if (evt.input_type == "copy") { return edit_result.model_applied(false,false,false,"",st.editor.selection,st.editor.model_revision) }
    let editor = app.mounted(st.editor,evt.target)
    let run = edit_handle_dom_action(editor,evt)
    st = {*:st,editor:model.clean(run.editor),status:if (run.result.failure != null) "This edit is unavailable here." else "Writing"}
    dom.set_close_guard(evt.target,session.is_dirty(st.file_state,st.editor.doc))
    return run.result
}
on selectionchange(evt) {
    if (app.is_control(evt.target) or (evt.source_selection==null and evt.source_pos==null)) { return }
    let editor = app.mounted(st.editor,evt.target)
    let accepted = edit_accept_dom_selection(editor,evt)
    if (accepted.changed) {
        st = {*:st,editor:model.clean(accepted.editor)}
        edit_restore_selection(st.editor)
    }
}
on word_command(req) {
    st = app.adopt(st,req.event)
    st = app.command(st,req.id,req.params,req.node)
    app.finish(st,req.node)
}
on edit_dialog(req) {
    st = app.command(st,"dialog." ++ req.action,{},req.node)
    app.finish(st,req.node)
}
on change(evt) {
    if (dom.node_type(evt.target)!=1) { return }
    let id = dom.get_attribute(evt.target,"data-command")
    if (id != null) {
        let key = dom.get_attribute(evt.target,"data-param")
        let raw = dom.get_state(evt.target,"value")
        let value = if (contains(["size","value"],key)) float(raw) else raw
        st = app.command(app.adopt(st,evt),id,{[key]:value},evt.target)
        app.finish(st,evt.target)
    }
}
on input(evt) {
    if (dom.node_type(evt.target)==1 and dom.get_attribute(evt.target,"id")=="find-query") {
        st = {*:st,query:string(dom.get_state(evt.target,"value") or ""),match_index:-1}
    }
}
on keydown(evt) {
    let key = lower(string(evt.key))
    let id = if (evt.key=="Escape") "ui.escape"
        else if (app.primary(evt) and key=="s") (if (evt.shiftKey) "file.saveAs" else "file.save")
        else if (app.primary(evt) and key=="f") "ui.find"
        else if (app.primary(evt) and st.dialog==null and not app.is_control(evt.target)) model.shortcut(key,evt.shiftKey==true)
        else null
    if (id==null) { return 'pass' }
    st = app.command(app.adopt(st,evt),id,{},evt.target)
    app.finish(st,evt.target)
    return 'prevent-default'
}
on closerequest(evt) {
    st = app.command(st,"file.close",{},evt.target)
    app.finish(st,evt.target)
    return 'prevent-default'
}

<html lang:"en", <head <meta charset:"utf-8"> <title "Word — Lambda workspace"> <style CSS ++ surface.document_css>>
    apply(<word_app>,{mode:"edit"})>
