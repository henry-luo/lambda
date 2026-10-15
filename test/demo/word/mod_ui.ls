// Thin WordCraft-style ribbon: every interactive item emits a command ID.
import lambda.editor.mod_editor
import lambda.editor.mod_step
import model: .mod_model
import document: .mod_document
import files: lambda.edit.files

view <word_button> {
    <button id:~.id, class:"tool " ++ (~.class or ""), title:~.title or ~.label,
        ["aria-label"]:~.title or ~.label, ["aria-pressed"]:string(~.pressed or false),
        *:if (~.disabled) {disabled:""} else {},
        <span class:"tool-icon", ~.icon or ~.label>
        if (~.icon != null) <span class:"tool-label", ~.label> else null>
}
on mousedown() { return 'prevent-default' }
on click(evt) { emit("word_command",{id:~.command,params:~.params or {},node:evt.target,event:evt}) }
pub fn button(id,command,label,icon=null,params={},pressed=false,disabled=false,css="") =>
    apply(<word_button id:id, command:command, label:label, icon:icon, params:params,
        pressed:pressed, disabled:disabled, class:css>)
pub fn command(id,label=null,icon=null,params={},pressed=false,disabled=false) =>
    button(id,id,label or model.spec(id).label,icon,params,pressed,disabled)
fn mark_button(editor,id,label) => command(id,label,null,{},edit_mark_active(editor,model.spec(id).mark))
fn group(label,items) => <div class:"ribbon-group group-" ++ lower(label), <div class:"group-tools", *items> <div class:"group-label", label>>
fn selector(id,command,values,current,css="",param="value") =>
    <select id:id, class:"ribbon-select " ++ css, ["data-command"]:command, ["data-param"]:param,
        ["aria-label"]:model.spec(command).label,
        *[for (entry in values) <option value:string(entry.value), *:if (string(entry.value)==string(current)) {selected:""} else {}, entry.label>]>

pub fn ribbon(editor,tab,nav,read_only) {
    let block = model.current_block(editor)
    let style = attrs_get(block.node.attrs,'data-word-style') or "Normal"
    let align = model.css_get(attrs_get(block.node.attrs,'style'),"text-align") or "left";
    if (tab == "Home") <div class:"ribbon-groups",
        group("History",[command("edit.undo","Undo","↶",{},false,not edit_can_undo(editor)),
            command("edit.redo","Redo","↷",{},false,not edit_can_redo(editor))])
        group("Font",[
            <div class:"font-selects",
                selector("font-family","format.font",[for (s in ["Georgia","Arial","Times New Roman","Courier New"]) {value:s,label:s}],
                    model.char_value(editor,"font-family","Georgia"),"font-family","name")
                selector("font-size","format.size",[for (s in [8,9,10,11,12,14,16,18,20,24,28,36,48,72]) {value:s,label:string(s)}],
                    replace(model.char_value(editor,"font-size","11pt"),"pt",""),"font-size","size")>,
            <div class:"font-tools",
                mark_button(editor,"format.bold","B"); mark_button(editor,"format.italic","I")
                mark_button(editor,"format.underline","U"); mark_button(editor,"format.strikethrough","S̶")
                mark_button(editor,"format.subscript","x₂"); mark_button(editor,"format.superscript","x²")
                command("format.highlight","Highlight",null,{color:"#f9e896"})>,
            <div class:"palette", <span "Text color">;
                *[for (color in ["#24334b","#2864ad","#b43d47","#28745d"])
                    apply(<word_button command:"format.color",params:{color:color},label:"Text color " ++ color,
                        icon:"●",class:"swatch swatch-" ++ slice(color,1)>) ]>])
        group("Paragraph",[
            <div class:"paragraph-tools",
                command("para.bullets","Bullets","• ≡"); command("para.numbering","Numbering","1 ≡")
                command("para.outdent","Outdent","←"); command("para.indent","Indent","→")>,
            <div class:"alignment-tools",
                *[for (choice in [{id:"para.alignLeft",icon:"≡",value:"left"},{id:"para.alignCenter",icon:"≡",value:"center"},
                    {id:"para.alignRight",icon:"≡",value:"right"},{id:"para.justify",icon:"☰",value:"justify"}])
                    command(choice.id,null,choice.icon,{},align==choice.value)]>,
            selector("line-spacing","para.lineSpacing",[for (s in [1,1.15,1.5,2]) {value:s,label:string(s) ++ " lines"}],
                model.css_get(attrs_get(block.node.attrs,'style'),"line-height") or "1.5","spacing")])
        group("Styles",[<div class:"style-gallery",
            *[for (s in take(document.styles,4)) button("style-" ++ s.id,"para.style",s.label,null,{style:s.id},s.id==style,false,"style-card style-" ++ s.id)]>,
            selector("paragraph-style","para.style",[for (s in document.styles) {value:s.id,label:s.label}],style,"style-selector","style")])
    >
    else if (tab == "Insert") <div class:"ribbon-groups",
        group("Table",[command("insert.table","Table","▦",{rows:3,cols:3,header:true}),
            command("table.insertRowBelow","Add row","+",{},false,not edit_inside(editor,'table')),
            command("table.insertColumnRight","Add column","+",{},false,not edit_inside(editor,'table')),
            command("table.deleteRow","Delete row",null,{},false,not edit_inside(editor,'table')),
            command("table.deleteColumn","Delete column",null,{},false,not edit_inside(editor,'table'))])
        group("Document",[button("insert-link","ui.link","Link","↗"),command("insert.horizontalRule","Rule","—")]);
        <div class:"ribbon-note", <strong "Make room for the details."> <span "Insert a table, add a link, or separate your next idea.">>
    >
    else <div class:"ribbon-groups",
        group("Show",[button("view-navigation","view.navigationPane","Navigation","☷",{},nav),
            button("view-reading","view.readMode","Read mode","▤",{},read_only),button("view-focus","view.focusMode","Focus","□")])
        group("Zoom",[button("zoom-out","view.zoomOut","Zoom out","−"),button("zoom-reset","view.zoomReset","100%","⊙"),
            button("zoom-in","view.zoomIn","Zoom in","+")]);
        <div class:"ribbon-note", <strong "A little space to think."> <span "Choose your view. Your document stays yours.">>
    >
}

pub fn navigation(editor,query,index) {
    let hits = model.search(editor.doc,query);
    <aside id:"navigation", class:"navigation",
        <div class:"pane-heading", "Navigation" <span class:"pane-caption", "OUTLINE & FIND">>
        <div class:"search-field", <span "⌕"> <input id:"find-query", type:"text", placeholder:"Find in document", value:query, ["aria-label"]:"Find in document">>
        <div class:"search-summary", id:"search-summary", if (query=="") "A map for your ideas" else string(len(hits)) ++ " matches">
        if (query != "") <div class:"find-controls",
            button("find-prev","edit.findPrevious","Previous","↑"); button("find-next","edit.findNext","Next","↓");
            <input id:"replace-text", type:"text", placeholder:"Replace with", ["aria-label"]:"Replace with">
            button("replace-one","edit.replace","Replace"); button("replace-all","edit.replaceAll","Replace all")>
        else null;
        <div class:"outline", *[for (i,heading in model.headings(editor.doc))
            button("heading-" ++ string(i),"ui.heading",heading.label,null,{path:heading.path},false,false,"outline-item level-" ++ heading.level)]>
        <div class:"navigation-footer", <span class:"small-label", "YOUR WORKSPACE"> <p "A blank page. A shared idea. Something worth writing.">>
    >
}

pub fn dialog(dialog,file) => if (dialog == null) null
    else if (dialog.kind == "unsaved") files.dialog_frame("unsaved","Keep your changes?","Save this document before continuing.",[],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("discard","Discard changes",false),files.dialog_button("save","Save",true)])
    else if (dialog.kind == "exists" or dialog.kind == "changed") files.dialog_frame(dialog.kind,"Replace the existing file?",
        if (dialog.kind == "changed") "The file changed on disk. Replacing it will overwrite that version." else dialog.path ++ " already exists.",[],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("overwrite","Replace",true)])
    else if (dialog.kind == "link") files.dialog_frame("link","Insert a link","Link the selected text, or insert a new label.",
        [files.field("link-address","","https://example.com"),files.field("link-label","","Link label (optional)")],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("link","Insert link",true)])
    else files.dialog_frame(dialog.kind,
        if (dialog.kind == "open") "Open document" else if (dialog.kind == "export") "Export document" else "Save editable document",
        if (dialog.kind == "open") "Open .word.json, HTML, Markdown, text, or a supported DOCX. Imports create an editable copy."
        else if (dialog.kind == "export") "Choose .html, .txt, or .pdf. PDF uses A4 pages."
        else "The .word.json format preserves the document and its formatting.",
        [files.field("word-file-path",dialog.path,if (dialog.kind == "open") "path/to/document" else "temp/word/document.word.json")],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("confirm",if (dialog.kind == "open") "Open" else if (dialog.kind == "export") "Export" else "Save",true)])
