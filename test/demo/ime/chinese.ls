// IME-O1/O2: document policy calls the shared reducer; acquisition happens on enable/focus.
import dom
import radiant
import ime: lambda.dom.ime

view <ime_button> { <button id:~.id, ~.label> }
on mousedown(evt) { 'prevent-default' }
on click(evt) {
    let body = dom.query_selector(dom.document_element(evt.target),"body")
    let command = ~.id;
    if (command == "fuzzy") { ime.configure(body,{fuzzy:["z=zh"],page_size:2})^ }
    else if (command == "exact") { ime.configure(body,{fuzzy:"none",page_size:2})^ }
    else if (command == "hans" or command == "hant") {
        dom.set_attribute(body,"data-ime-profile",if (command == "hant") "zh-Hant" else "zh-Hans")
        ime.focus_in(body)^
    }
    else if (command == "inherit") {
        dom.remove_attribute(body,"data-ime")
        radiant.set_chinese_ime(body,true)
    } else if (command == "window-off") { radiant.set_chinese_ime(body,false) }
    else if (command == "window-on") { radiant.set_chinese_ime(body,true) }
    else if (command == "document-off") { ime.enable(body,false)^ }
    else if (command == "document-on") { ime.enable(body,true)^ }
    'pass'
}

<html <head <meta charset:"utf-8"> <title "Chinese pinyin input">
    <style "body{font:17px sans-serif;margin:32px;background:#f4f6fa;color:#263042}h1{margin-bottom:8px}p{max-width:680px;line-height:1.5}button{padding:8px 12px;margin:0 6px 8px 0;border:1px solid #a5afc0;background:white;border-radius:5px}label{display:block;margin-top:18px}input,textarea,[contenteditable]{display:block;box-sizing:border-box;width:620px;max-width:100%;padding:10px;border:1px solid #9ba8bc;background:white;font:20px sans-serif}textarea{height:90px}[contenteditable]{min-height:70px}.controls{margin-top:22px}">>
<body 'data-ime':"chinese",'data-ime-page-size':"2",
    <h1 "中文 · Pinyin">
    <p "Type beijing or zhongguo. Choose with Space, a number, or the native popup. Arrows move the highlight; − / = change pages. Tap Shift or click the popup header for English.">
    <p "Choose 简体 or 繁體 with the profile buttons. Use a Latin OS input source for the bundled engine. An active OS composition takes priority.">
    <div class:"controls",apply(<ime_button id:"fuzzy",label:"z ↔ zh on">)
        apply(<ime_button id:"exact",label:"Exact pinyin">)
        apply(<ime_button id:"hans",label:"简体">)
        apply(<ime_button id:"hant",label:"繁體">)
        apply(<ime_button id:"inherit",label:"Use window setting">)
        apply(<ime_button id:"window-off",label:"Window off">)
        apply(<ime_button id:"window-on",label:"Window on">)
        apply(<ime_button id:"document-off",label:"Document off">)
        apply(<ime_button id:"document-on",label:"Document on">)>
    <label "Single line" <input id:"input",placeholder:"beijing">>
    <label "Textarea" <textarea id:"textarea">>
    <label "Contenteditable" <div id:"rich",contenteditable:"true",tabindex:"0">>
    <p "Selections are learned for this document. Saving learned words and preferences is an explicit host action.">>>
