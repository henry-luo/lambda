import dom
import ime: lambda.dom.ime
import model: lambda.ime.chinese.dictionary

fn node(body,id) => dom.query_selector(body,"#" ++ id)
fn field(body,id) => dom.get_state(node(body,id),"value")
pn text(body,id,value) { dom.set_text_content(node(body,id),value) }
pn disabled(body,id,value) {
    if (value) dom.set_attribute(node(body,id),"disabled","")
    else dom.remove_attribute(node(body,id),"disabled")
}
pub fn opened(body) bool => dom.has_attribute(node(body,"dictionary-dialog"),"open")
pub fn initial() map => {filter:"",cursor:null,history:[],result:null}

fn path(body,profile,directory) => {
    let override = dom.get_attribute(body,"data-ime-custom-file");
    if (override != null) override
    else if (ends_with(directory or "","-bible-combined")) "temp/ime_chinese/custom-" ++ profile ++ ".json"
    else null
}

// fixture/host directory overrides stay isolated unless they supply a customization path.
pub pn activate(body) any^ {
    let current = dom.ime_session(body);
    if (current == null) { return true }
    let file = path(body,current.profile,current.directory);
    if (current.demo_dictionary != null and current.demo_dictionary.path == file) { return true }
    let base = if (current.demo_dictionary == null) current.engine else current.demo_dictionary.base
    let edits = if (file != null and exists(file)) model.validate(input(file,"json")^,base)^
        else model.empty(current.profile)
    let engine = model.restore(base,edits)^;
    dom.set_ime_session(body,{*:current,engine:engine,demo_dictionary:{path:file,edits:edits,base:base}})
    return true
}

pn present(body,st,result) map^ {
    let current = dom.ime_session(body)
    let rows = [for (item in result.items) <tr <td class:"dictionary-word",item.word>
        <td replace(item.reading,"'"," ")>
        <td <button type:"button",'data-dictionary-action':"delete",'data-word':item.word,
            'data-reading':item.reading,'aria-label':"Delete " ++ item.word,"Delete">>>];
    dom.set_inner_html(node(body,"dictionary-rows"),join([for (row in rows) format(row,'xml')^],""))
    disabled(body,"dictionary-prev",len(st.history) == 0)
    disabled(body,"dictionary-next",result.next == null)
    text(body,"dictionary-page","Page " ++ string(len(st.history) + 1) ++
        (if (len(result.items) == 0) " · No matching words" else " · " ++ string(len(result.items)) ++ " entries"))
    text(body,"dictionary-profile",if (current.profile == "zh-Hant") "繁體 dictionary" else "简体 dictionary")
    text(body,"dictionary-storage",if (current.demo_dictionary.path == null) "Changes apply to this document."
        else "Changes are saved locally and restored next time.")
    return {*:st,result:result}
}

pn render(body,st) map^ {
    let result = model.browse(dom.ime_session(body).engine,st.filter,st.cursor)^;
    return present(body,st,result)^
}

pub pn close(body) {
    node(body,"dictionary-dialog").close("")
    let previous = dom.get_attribute(node(body,"dictionary-dialog"),"data-previous-ime");
    if (previous == "inherit") dom.remove_attribute(body,"data-ime")
    else dom.set_attribute(body,"data-ime",previous)
    ime.suspend_popup(body,false)
    dom.focus_set(node(body,"dictionary-open"),false)
}

pn show(body) {
    let previous = dom.get_attribute(body,"data-ime");
    dom.set_attribute(node(body,"dictionary-dialog"),"data-previous-ime",if (previous == null) "inherit" else previous)
    dom.set_attribute(body,"data-ime","off")
    ime.suspend_popup(body,true)
    node(body,"dictionary-dialog").show_modal()
    dom.focus_set(node(body,"dictionary-search"),false)
}

pub pn command(body,st,action,target) map^ {
    if (action == "close") { close(body); return st }
    if (action == "open") {
        // a dialog takes keyboard focus, so finish the old provisional range first.
        ime.focus_out(body)^
        let fuzzy = dom.get_attribute(body,"data-ime-fuzzy")
        let size = dom.get_attribute(body,"data-ime-page-size");
        ime.configure(body,{fuzzy:if (fuzzy == null) "none" else if (fuzzy in ["none","all"]) fuzzy else split(fuzzy,","),
            page_size:if (size == null) 7 else int(size)^})^
        activate(body)^
        dom.set_state(node(body,"dictionary-search"),"value","")
        let next = render(body,initial())^;
        text(body,"dictionary-message","")
        show(body)
        return next
    }
    if (action == "search") {
        let next = {*:initial(),filter:field(body,"dictionary-search")}
        var result = {items:[],next:null}
        var message = "";
        model.browse(dom.ime_session(body).engine,next.filter) ^ { message = ^.message } ~ { result = ~ }
        // a rejected query owns an empty page; retaining the previous cursor would expose stale results.
        let rendered = present(body,next,result)^;
        text(body,"dictionary-message",message)
        return rendered
    }
    if (action == "next" and st.result.next != null) {
        return render(body,{*:st,cursor:st.result.next,history:[*st.history,st.cursor]})^
    }
    if (action == "prev" and len(st.history) > 0) {
        return render(body,{*:st,cursor:st.history[len(st.history) - 1],
            history:take(st.history,len(st.history) - 1)})^
    }
    if (action == "add" or action == "delete") {
        let current = dom.ime_session(body)
        let word = if (action == "add") field(body,"dictionary-word") else dom.get_attribute(target,"data-word")
        let reading = if (action == "add") field(body,"dictionary-reading") else dom.get_attribute(target,"data-reading")
        let edits = model.change(current.demo_dictionary.edits,current.engine,word,reading,action == "add")^
        let entry = edits.entries[len(edits.entries) - 1]
        // replay the final edits on the base so re-adding a deleted word restores its original weight.
        let engine = model.restore(current.demo_dictionary.base,edits)^
        let session = model.forget(current.session,entry[0],entry[1])^
        let file = current.demo_dictionary.path;
        if (file != null) {
            io.mkdir("temp/ime_chinese")^
            // atomic persistence must succeed before the live dictionary changes.
            output(edits,file,{format:"json",atomic:true}) ^ { raise error("Could not save dictionary changes.") }
        }
        dom.set_ime_session(body,{*:current,engine:engine,session:session,
            demo_dictionary:{path:file,edits:edits,base:current.demo_dictionary.base}})
        text(body,"dictionary-message",(if (action == "add") "Added " else "Deleted ") ++ entry[0] ++ ".")
        // a fresh first page avoids retaining byte cursors after table edits.
        return render(body,{*:initial(),filter:st.filter})^
    }
    return st
}

pub pn dispatch(body,st,action,target) map {
    var next = st;
    command(body,st,action,target) ^ {
        let message = ^.message;
        // load and validation failures must be readable even when opening did not finish.
        if (action == "open" and not opened(body)) {
            dom.set_inner_html(node(body,"dictionary-rows"),"")
            show(body)
        }
        text(body,"dictionary-message",message)
    } ~ { next = ~ }
    return next
}

pub fn dialog() => <dialog id:"dictionary-dialog",'data-ime':"off",'aria-labelledby':"dictionary-title",
    <div class:"dictionary-heading",<div <h2 id:"dictionary-title","Customize dictionary">
        <span id:"dictionary-profile">>
        <button type:"button",'data-dictionary-action':"close",'aria-label':"Close dictionary","Close">>
    <p id:"dictionary-storage",class:"dictionary-help">
    <div class:"dictionary-search-row",<label for:"dictionary-search","Search by word or pinyin"
        <input id:"dictionary-search",placeholder:"e.g. 北京 or bei jing">>
        <button id:"dictionary-find",type:"button",'data-dictionary-action':"search","Search">>
    <div class:"dictionary-list",<table <thead <tr <th "Word"><th "Pinyin"><th "">>>
        <tbody id:"dictionary-rows">>>
    <div class:"dictionary-paging",<button id:"dictionary-prev",type:"button",'data-dictionary-action':"prev","Previous">
        <span id:"dictionary-page">
        <button id:"dictionary-next",type:"button",'data-dictionary-action':"next","Next">>
    <div class:"dictionary-add",<h3 "Add a word">
        <p class:"dictionary-help","Paste or use your OS input method to enter 1–9 Chinese characters. Give one toneless pinyin syllable per character, separated by spaces.">
        <div class:"dictionary-add-fields",<label for:"dictionary-word","Chinese word"
            <input id:"dictionary-word",placeholder:"e.g. 恩典">>
            <label for:"dictionary-reading","Pinyin"
                <input id:"dictionary-reading",placeholder:"e.g. en dian">>
            <button id:"dictionary-add",type:"button",'data-dictionary-action':"add","Add word">>>
    <p id:"dictionary-message",role:"status",'aria-live':"polite">>

pub let css = "#dictionary-dialog{box-sizing:border-box;width:700px;max-width:calc(100vw - 32px);max-height:calc(100vh - 32px);overflow:auto;border:1px solid #a5afc0;border-radius:16px;padding:24px;background:#fff;color:#263042;box-shadow:0 16px 48px #0003;font:15px sans-serif}#dictionary-dialog:not([open]){display:none}#dictionary-dialog::backdrop{background:#1b294c66}.dictionary-heading{display:flex;align-items:center;justify-content:space-between;gap:16px}.dictionary-heading h2{font-size:23px;margin:0 0 4px}.dictionary-heading span{color:#667386}.dictionary-heading button{margin:0}.dictionary-help{font-size:13px;color:#576477;line-height:1.5;margin:10px 0}.dictionary-search-row{display:flex;align-items:end;gap:12px;margin:16px 0 12px}.dictionary-search-row label{flex:1;margin:0}.dictionary-search-row button{margin:0}.dictionary-search-row input,.dictionary-add-fields input{width:100%;font:16px sans-serif;padding:8px;margin-top:6px}.dictionary-list{height:258px;overflow:auto;border:1px solid #d7dce5;border-radius:8px}.dictionary-list table{border-collapse:collapse;width:100%;font-size:14px}.dictionary-list th{text-align:left;color:#667386;background:#f4f6fa}.dictionary-list td,.dictionary-list th{padding:7px 12px;border-bottom:1px solid #e8ebf0}.dictionary-list .dictionary-word{font-size:18px}.dictionary-list td:last-child{width:65px}.dictionary-list button{padding:4px 8px;margin:0;color:#a32836;border-color:#e3c4c9;font-size:12px}.dictionary-paging{display:flex;align-items:center;justify-content:space-between;margin-top:12px}.dictionary-paging button{margin:0}.dictionary-paging span{font-size:13px;color:#667386}.dictionary-add{border-top:1px solid #d7dce5;margin-top:18px;padding-top:14px}.dictionary-add h3{font-size:17px;margin:0}.dictionary-add-fields{display:flex;align-items:end;gap:12px}.dictionary-add-fields label{flex:1;min-width:0;margin:0}.dictionary-add-fields button{margin:0;background:#1677ff;color:white;border-color:#1677ff}#dictionary-message{font-size:13px;margin:12px 0 0;min-height:20px}"
