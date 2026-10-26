// IME-O1/O2: document policy calls the shared reducer; acquisition happens on enable/focus.
import dom
import radiant
import ime: lambda.dom.ime
import chinese: lambda.ime.chinese
import dictionary_ui: .mod_dictionary_ui

fn demo_directory(profile) string => "lmd/package/ime/chinese/data/" ++ profile ++ "-bible-combined"

fn ime_enabled(body,status) bool {
    let override = dom.get_attribute(body,"data-ime");
    if (override == null) status.enabled else override == "chinese"
}

fn fuzzy_enabled(body) bool {
    let value = dom.get_attribute(body,"data-ime-fuzzy");
    value != null and value != "" and value != "none"
}

fn selected_pairs(body) array {
    let value = if (fuzzy_enabled(body)) dom.get_attribute(body,"data-ime-fuzzy")
        else dom.get_attribute(body,"data-ime-fuzzy-pairs");
    if (value == null or value == "all") [for (pair in chinese.fuzzy_pairs) pair.name]
    else if (value == "" or value == "none") [] else split(value,",")
}

fn pair_id(name) string => "fuzzy-pair-" ++ replace(name,"=","-")

pn text(body,id,value) {
    let node = dom.query_selector(body,"#" ++ id);
    if (dom.text_content(node) != value) dom.set_text_content(node,value)
}

pn position_options(body) {
    let panel = dom.query_selector(body,"#fuzzy-panel");
    if (not dom.matches(panel,":popover-open")) { return }
    let anchor = dom.bounding_box(dom.query_selector(body,"#fuzzy-options"))
    let viewport = dom.viewport_size(body)
    let width = max(1,min(400,viewport.width - 16))
    let below = max(0,viewport.height - anchor.y - anchor.height - 14)
    let above = max(0,anchor.y - 14)
    let height = max(1,min(520,max(below,above)));
    dom.style_set_property(panel,"width",string(width) ++ "px")
    dom.style_set_property(panel,"max-height",string(height) ++ "px")
    let box = dom.bounding_box(panel)
    let top = if (below >= box.height) anchor.y + anchor.height + 6
        else max(8,anchor.y - box.height - 6);
    dom.style_set_property(panel,"top",string(top) ++ "px")
    dom.style_set_property(panel,"left",string(max(8,min(anchor.x,viewport.width - width - 8))) ++ "px")
}

pn options_open(body,opened) {
    let panel = dom.query_selector(body,"#fuzzy-panel");
    if (opened) {
        ime.suspend_popup(body,true)
        panel.show_popover()
        position_options(body)
    } else {
        panel.hide_popover()
        ime.suspend_popup(body,false)
    }
}

pn configure_fuzzy(body,pairs,enabled) any^ {
    if (enabled or fuzzy_enabled(body)) {
        let size = dom.get_attribute(body,"data-ime-page-size");
        // S16.4.2v2: the empty value in a procedure branch must be explicit.
        ime.configure(body,{fuzzy:if (enabled) pairs else "none",
            *:(if (size == null) ({}) else {page_size:int(size)^})})^
    }
    // retain the selected pairs independently of the master on/off switch (IME13).
    dom.set_attribute(body,"data-ime-fuzzy-pairs",join(pairs,","))
    true
}

pn update_controls(body) {
    let status = dom.ime_status(body)
    let override = dom.get_attribute(body,"data-ime")
    let inherited = override == null
    let enabled = ime_enabled(body,status)
    let fuzzy_on = fuzzy_enabled(body)
    let pairs = selected_pairs(body)
    let traditional = dom.get_attribute(body,"data-ime-profile") == "zh-Hant";
    for (entry in [
        {id:"fuzzy",selected:fuzzy_on},
        {id:"hans",selected:not traditional},{id:"hant",selected:traditional},
        {id:"inherit",selected:inherited},
        {id:"window-toggle",selected:status.enabled},
        {id:"document-toggle",selected:enabled}
    ]) {
        let button = dom.query_selector(body,"#" ++ entry.id)
        let pressed = string(entry.selected);
        if (dom.get_attribute(button,"aria-pressed") != pressed)
            dom.set_attribute(button,"aria-pressed",pressed)
    }
    text(body,"fuzzy",if (fuzzy_on) "Fuzzy pinyin on" else "Fuzzy pinyin off")
    text(body,"window-toggle",if (status.enabled) "Window on" else "Window off")
    text(body,"document-toggle",if (enabled) "Document on" else "Document off")
    let toggle = dom.query_selector(body,"#fuzzy");
    if (len(pairs) == 0) dom.set_attribute(toggle,"disabled","")
    else if (dom.has_attribute(toggle,"disabled")) dom.remove_attribute(toggle,"disabled")
    for (pair in chinese.fuzzy_pairs) {
        let field = dom.query_selector(body,"#" ++ pair_id(pair.name))
        let checked = pair.name in pairs;
        if (dom.get_state(field,"checked") != checked) dom.set_state(field,"checked",checked)
    }
    let panel = dom.query_selector(body,"#fuzzy-panel");
    dom.set_attribute(dom.query_selector(body,"#fuzzy-options"),"aria-expanded",
        string(dom.matches(panel,":popover-open")))
    text(body,"fuzzy-count",string(len(pairs)) ++ " selected" ++
        (if (len(pairs) == 0) " · Select a pair to enable fuzzy pinyin." else ""))
    let message = "Input method " ++ (if (enabled) "on" else "off") ++
        (if (inherited) " · window setting" else " · document override") ++
        (if (traditional) " · 繁體" else " · 简体") ++
        (if (fuzzy_on) " · Fuzzy pinyin on (" ++ string(len(pairs)) ++ " pairs)" else " · Fuzzy pinyin off");
    text(body,"ime-status",message)
}

pn apply_control(body,command) any^ {
    if (command == "fuzzy") {
        let pairs = selected_pairs(body);
        configure_fuzzy(body,pairs,not fuzzy_enabled(body) and len(pairs) > 0)^
    }
    else if (command == "fuzzy-options") {
        let panel = dom.query_selector(body,"#fuzzy-panel");
        options_open(body,not dom.matches(panel,":popover-open"))
    }
    else if (command == "fuzzy-done") { options_open(body,false) }
    else if (command == "fuzzy-all" or command == "fuzzy-none") {
        let pairs = if (command == "fuzzy-all") [for (pair in chinese.fuzzy_pairs) pair.name] else [];
        configure_fuzzy(body,pairs,fuzzy_enabled(body) and len(pairs) > 0)^
    }
    else if (starts_with(command,"pair:")) {
        let name = slice(command,5)
        let current = selected_pairs(body)
        let checked = dom.get_state(dom.query_selector(body,"#" ++ pair_id(name)),"checked")
        let pairs = [for (pair in chinese.fuzzy_pairs where
            (if (pair.name == name) checked else pair.name in current)) pair.name];
        configure_fuzzy(body,pairs,fuzzy_enabled(body) and len(pairs) > 0)^
    }
    else if (command == "hans" or command == "hant") {
        let profile = if (command == "hant") "zh-Hant" else "zh-Hans"
        let directory = dom.get_attribute(body,"data-ime-directory");
        // switch the default combined pack with its script, retaining explicit host/fixture overrides.
        if (directory in [demo_directory("zh-Hans"),demo_directory("zh-Hant")])
            dom.set_attribute(body,"data-ime-directory",demo_directory(profile))
        dom.set_attribute(body,"data-ime-profile",profile)
        ime.focus_in(body)^
    }
    else if (command in ["inherit","window-toggle"]) {
        // choosing window controls also relinquishes the demo's document override (IME-O2).
        dom.remove_attribute(body,"data-ime")
        if (command == "inherit") { ime.focus_in(body)^ }
        else { radiant.set_chinese_ime(body,not dom.ime_status(body).enabled) }
    }
    else if (command == "document-toggle") { ime.enable(body,not ime_enabled(body,dom.ime_status(body)))^ };
    true
}

pn run_control(body,command) {
    var failure = null;
    apply_control(body,command) ^ { failure = ^ }
    dictionary_ui.activate(body) ^ { failure = ^ }
    update_controls(body)
    if (failure is error) text(body,"ime-status",failure.message)
}

view <ime_button> { <button type:"button",id:~.id,
    *:(if (~.id == "fuzzy-options") {'aria-expanded':"false",'aria-controls':"fuzzy-panel",
        'aria-label':"Configure fuzzy pinyin",title:"Configure fuzzy pinyin"} else {'aria-pressed':"false"}), ~.label> }
on mousedown(evt) { 'prevent-default' }
on click(evt) {
    run_control(dom.query_selector(dom.document_element(evt.target),"body"),~.id)
    'pass'
}

view <ime_pair> { <label class:"fuzzy-pair",for:pair_id(~.pair.name),
    <input id:pair_id(~.pair.name),type:"checkbox",checked:true>
    <span <span class:"pair-name",~.pair.left ++ " ↔ " ++ ~.pair.right>
        <span class:"pair-example",~.pair.example>>> }
on mousedown(evt) { 'prevent-default' }
on change(evt) {
    run_control(dom.query_selector(dom.document_element(evt.target),"body"),"pair:" ++ ~.pair.name)
    'pass'
}

view <ime_page> state dictionary:dictionary_ui.initial() { ~.rendered }
on load(evt) {
    update_controls(dom.query_selector(dom.document_element(evt.target),"body"))
    'pass'
}
on click(evt) {
    let button = dom.closest(evt.target,"[data-dictionary-action]");
    if (button != null) {
        let body = dom.query_selector(dom.document_element(evt.target),"body");
        dictionary = dictionary_ui.dispatch(body,dictionary,dom.get_attribute(button,"data-dictionary-action"),button)
        update_controls(body)
        return 'handled'
    }
    'pass'
}
on focusin(evt) {
    let body = dom.query_selector(dom.document_element(evt.target),"body");
    if (not dictionary_ui.opened(body)) {
        ime.focus_in(body) ^ { text(body,"ime-status",^.message) }
        dictionary_ui.activate(body) ^ { text(body,"ime-status",^.message) }
    }
    'pass'
}
on mousedown(evt) {
    let body = dom.query_selector(dom.document_element(evt.target),"body")
    let panel = dom.query_selector(body,"#fuzzy-panel");
    if (dom.matches(panel,":popover-open") and dom.closest(evt.target,"#fuzzy-control") == null) {
        options_open(body,false)
        update_controls(body)
    }
    'pass'
}
on keydown(evt) {
    let body = dom.query_selector(dom.document_element(evt.target),"body")
    let panel = dom.query_selector(body,"#fuzzy-panel");
    if (dictionary_ui.opened(body)) {
        if (evt.key == "Escape") {
            dictionary_ui.close(body)
            update_controls(body)
            return 'prevent-default'
        }
        if (evt.key == "Enter" and dom.get_attribute(evt.target,"id") in
            ["dictionary-search","dictionary-word","dictionary-reading"]) {
            dictionary = dictionary_ui.dispatch(body,dictionary,
                if (dom.get_attribute(evt.target,"id") == "dictionary-search") "search" else "add",evt.target)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (evt.key == "Escape" and dom.matches(panel,":popover-open")) {
        options_open(body,false)
        update_controls(body)
        return 'prevent-default'
    }
    'pass'
}
on resize(evt) { position_options(dom.query_selector(dom.document_element(evt.target),"body")) 'pass' }

<html <head <meta charset:"utf-8"> <title "Chinese pinyin input">
    <style "body{font:17px sans-serif;margin:32px;background:#f4f6fa;color:#263042}h1{margin-bottom:8px}p{max-width:680px;line-height:1.5}button{padding:8px 12px;margin:0 6px 8px 0;border:1px solid #a5afc0;background:white;border-radius:5px}button[aria-pressed=true]{background:#1677ff;border-color:#1677ff;color:white}label{display:block;margin-top:18px}input,textarea,[contenteditable]{display:block;box-sizing:border-box;width:620px;max-width:100%;padding:10px;border:1px solid #9ba8bc;background:white;font:20px sans-serif}input:not([type=checkbox]),textarea,[contenteditable]{border-radius:8px}textarea{height:90px}[contenteditable]{min-height:70px}.controls{margin-top:22px}.ime-status{font-size:14px;margin:0 0 12px;color:#576477}.fuzzy-control{display:inline-flex;vertical-align:top;margin:0 6px 8px 0}.fuzzy-control>button{margin:0}#fuzzy{border-radius:5px 0 0 5px}#fuzzy-options{border-left:0;border-radius:0 5px 5px 0;padding:8px 10px}.fuzzy-panel{position:fixed;inset:auto;margin:0;box-sizing:border-box;width:400px;max-height:520px;overflow:auto;padding:16px;border:1px solid #a5afc0;border-radius:8px;background:white;color:#263042;box-shadow:0 8px 24px #0003;z-index:1000}.fuzzy-panel:not(:popover-open){display:none}.fuzzy-panel:popover-open{display:block}.fuzzy-pairs{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px 12px}.fuzzy-pair{display:flex;align-items:center;margin:0;padding:6px 0;font-size:14px;cursor:pointer}.fuzzy-pair input{display:inline-block;width:16px;height:16px;max-width:none;padding:0;margin:0 8px 0 0}.pair-name,.pair-example{display:block}.pair-example{font-size:11px;color:#667386;margin-top:2px}.fuzzy-help{font-size:13px;line-height:1.4;margin:8px 0 12px;color:#576477}.fuzzy-actions{margin-bottom:4px}button:disabled{opacity:0.5;cursor:default}">
    <style dictionary_ui.css>>
    apply(<ime_page rendered:<body id:"ime-demo",'data-ime':"chinese",'data-ime-fuzzy':"all",
    'data-ime-directory':demo_directory("zh-Hans"),
    <h1 "中文 · Pinyin">
    <p "Type beijing or zhongguo. Left jumps back a syllable; Right moves one letter. Down opens all candidates; arrows navigate the menu. Up on the top row or Esc returns to the strip. Choose with Space, Enter in the menu, a number, or a click. Tap Shift for English.">
    <p "Choose 简体 or 繁體 with the profile buttons. Use a Latin OS input source for the bundled engine. An active OS composition takes priority.">
    <div class:"controls",<span id:"fuzzy-control",class:"fuzzy-control",
        apply(<ime_button id:"fuzzy",label:"Fuzzy pinyin on">)
        apply(<ime_button id:"fuzzy-options",label:"v">);
        <div id:"fuzzy-panel",class:"fuzzy-panel",popover:"manual",role:"group",'aria-label':"Fuzzy pinyin options",
            <strong "Fuzzy pinyin options">
            <p class:"fuzzy-help","Choose the sound pairs to use when fuzzy pinyin is on.">
            <div class:"fuzzy-actions",apply(<ime_button id:"fuzzy-all",label:"All">)
                apply(<ime_button id:"fuzzy-none",label:"None">)>
            <div class:"fuzzy-pairs",for (pair in chinese.fuzzy_pairs) apply(<ime_pair pair:pair>)>
            <p id:"fuzzy-count",class:"fuzzy-help","">
            apply(<ime_button id:"fuzzy-done",label:"Done">)>>
        apply(<ime_button id:"hans",label:"简体">)
        apply(<ime_button id:"hant",label:"繁體">)
        apply(<ime_button id:"inherit",label:"Use window setting">)
        apply(<ime_button id:"window-toggle",label:"Window off">)
        apply(<ime_button id:"document-toggle",label:"Document on">);
        <button id:"dictionary-open",type:"button",'data-dictionary-action':"open","Dictionary…">>
    <p id:"ime-status",class:"ime-status",role:"status",'aria-live':"polite", "">
    <label "Single line" <input id:"input",placeholder:"beijing">>
    <label "Textarea" <textarea id:"textarea">>
    <label "Contenteditable" <div id:"rich",contenteditable:"true",tabindex:"0">>
    <p "Use Dictionary to browse words, add your own, or delete entries. Dictionary edits are saved locally; selections are learned for this document.">
    dictionary_ui.dialog()>>)>
