// Thin WordCraft-style ribbon: every interactive item emits a command ID.
import lambda.editor.mod_editor
import lambda.editor.mod_step
import model: .mod_model
import document: .mod_document
import files: lambda.edit.files
import page: .mod_page
import review: .mod_review
import insert: .mod_insert
import controls: lambda.ui.controls

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
    apply(<control.select id:id,class:"ribbon-select " ++ css,label:model.spec(command).label,
        options:values,value:current,on_change:{action:command,param:param}>)

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
                command("format.highlight","Highlight",null,{color:"#f9e896"}); command("format.clear","Clear","A×")>,
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
            <div class:"paragraph-settings-row",
                selector("line-spacing","para.lineSpacing",[for (s in [1,1.15,1.5,2]) {value:s,label:string(s) ++ " lines"}],
                    model.css_get(attrs_get(block.node.attrs,'style'),"line-height") or "1.5","spacing")
                button("paragraph-settings","ui.paragraph","Paragraph…","↗",{},false,read_only,"paragraph-dialog")>])
        group("Styles",[<div class:"style-gallery",
            *[for (s in take(document.styles,4)) button("style-" ++ s.id,"para.style",s.label,null,{style:s.id},s.id==style,false,"style-card style-" ++ s.id)]>,
            selector("paragraph-style","para.style",[for (s in document.styles) {value:s.id,label:s.label}],style,"style-selector","style")])
    >
    else if (tab == "Insert") <div class:"ribbon-groups insert-ribbon",
        group("Pages",[button("insert-cover","ui.cover","Cover page…","▤",{},false,read_only),
            command("insert.blankPage","Blank page","□",{},false,read_only),
            button("insert-page-break","insert.pageBreak","Page break","↵",{},false,read_only)])
        group("Tables",[button("insert.table","ui.table","Table…","▦",{},false,read_only)])
        group("Pictures",[button("insert-picture","ui.picture","Picture…","▧",{},false,read_only),
            button("picture-settings","ui.pictureSettings","Picture settings…",null,{},false,read_only or model.current_node(editor,'img')==null)])
        group("Links",[button("insert-link","ui.link",if (model.current_node(editor,'a')==null) "Link…" else "Edit link…","↗",{},false,read_only),
            command("insert.removeLink","Remove link",null,{},false,read_only or model.current_node(editor,'a')==null),
            button("insert-bookmark","ui.bookmarks","Bookmark…","⚑")])
        group("Comments",[button("insert-comment","ui.comment","Comment","▣",{},false,read_only or not model.can_comment(editor))])
        group("Text & Symbols",[button("insert-symbol","ui.symbol","Symbol…","Ω",{},false,read_only),
            command("insert.horizontalRule","Rule","—",{},false,read_only),
            button("follow-link","ui.followLink","Go to bookmark","↗",{},false,
                insert.link_bookmark(attrs_get(model.current_node(editor,'a').node.attrs,'href'))==null)])
    >
    else if (tab == "Review") <div class:"ribbon-groups",
        group("Comments",[button("new-comment","ui.comment","New comment","+",{},false,read_only or not model.can_comment(editor)),
            button("show-comments","ui.comments","Comments pane","☷"),button("show-resolved","ui.resolvedComments","Show resolved")])
        group("Navigate",[button("previous-comment","ui.previousComment","Previous comment","↑"),button("next-comment","ui.nextComment","Next comment","↓")]);
        <div class:"ribbon-note",<strong "Keep the conversation with the text."> <span "Select text to leave a comment. Resolve it when the edit is complete.">>
    >
    else if (tab == "Layout") <div class:"ribbon-groups",
        group("Page",[button("page-setup","ui.pageSetup","Page setup…","▤",{},false,read_only),
            selector("paper-size","layout.pageSetup",[for (p in page.papers) {value:p.id,label:p.id}],page.settings(editor.doc).paper,"","paper")])
        group("Orientation",[*[for (choice in ["portrait","landscape"]) button("orientation-" ++ choice,"layout.pageSetup",choice,null,{orientation:choice},
            page.settings(editor.doc).orientation==choice,read_only)]])
        group("Margins",[button("margins-normal","layout.pageSetup","Normal",null,{top:25.4,right:25.4,bottom:25.4,left:25.4},false,read_only),
            button("margins-narrow","layout.pageSetup","Narrow",null,{top:12.7,right:12.7,bottom:12.7,left:12.7},false,read_only)])
        group("Breaks",[command("insert.pageBreak","Page break","↵",{},false,read_only),button("layout-paragraph","ui.paragraph","Paragraph…",null,{},false,read_only)])
    >
    else if (tab == "Table") <div class:"ribbon-groups",
        group("Rows",[command("table.insertRowAbove","Above","↑"),command("table.insertRowBelow","Below","↓"),command("table.deleteRow","Delete row")])
        group("Columns",[command("table.insertColumnLeft","Left","←"),command("table.insertColumnRight","Right","→"),command("table.deleteColumn","Delete column")])
        group("Table style",[selector("table-style","table.style",[for (s in document.table_styles) {value:s.id,label:s.label}],
            attrs_get(model.current_node(editor,'table').node.attrs,'data-word-table-style') or "grid","","style"),
            command("table.deleteTable","Delete table")])
        group("Cell alignment",[selector("cell-alignment","table.cellAlign",[
            {value:"topLeft",label:"Top left"},{value:"topCenter",label:"Top center"},{value:"topRight",label:"Top right"},
            {value:"centerLeft",label:"Middle left"},{value:"center",label:"Middle center"},{value:"centerRight",label:"Middle right"},
            {value:"bottomLeft",label:"Bottom left"},{value:"bottomCenter",label:"Bottom center"},{value:"bottomRight",label:"Bottom right"}],model.cell_alignment(editor))])
    >
    else <div class:"ribbon-groups",
        group("Show",[button("view-navigation","view.navigationPane","Navigation","☷",{},nav),
            button("view-reading","view.readMode","Read mode","▤",{},read_only),button("view-focus","view.focusMode","Focus","□")])
        group("Zoom",[button("zoom-out","view.zoomOut","Zoom out","−"),button("zoom-reset","view.zoomReset","100%","⊙"),
            button("zoom-in","view.zoomIn","Zoom in","+")]);
        <div class:"ribbon-note", <strong "A little space to think."> <span "Choose your view. Your document stays yours.">>
    >
}

pub fn navigation(editor,query,index,match_case=false,replacement="") {
    let hits = model.search(editor.doc,query,match_case);
    <aside id:"navigation", class:"navigation",
        <div class:"pane-heading", "Navigation" <span class:"pane-caption", "OUTLINE & FIND">>
        <div class:"search-field", <span "⌕"> apply(<control.input id:"find-query",placeholder:"Find in document",
            value:query,label:"Find in document",on_change:"find.query">)>
        <div class:"search-summary", id:"search-summary", if (query=="") "A map for your ideas" else string(len(hits)) ++ " matches">
        if (query != "") <div class:"find-controls",
            button("find-match-case","ui.matchCase","Match case",null,{},match_case); button("find-clear","ui.clearFind","Clear");
            button("find-prev","edit.findPrevious","Previous","↑"); button("find-next","edit.findNext","Next","↓");
            apply(<control.input id:"replace-text",placeholder:"Replace with",value:replacement,
                label:"Replace with",on_change:"find.replacement">)
            button("replace-one","edit.replace","Replace"); button("replace-all","edit.replaceAll","Replace all")>
        else null;
        <div class:"outline", *[for (i,heading in model.headings(editor.doc))
            button("heading-" ++ string(i),"ui.heading",heading.label,null,{path:heading.path},false,false,"outline-item level-" ++ heading.level)]>
        <div class:"navigation-footer", <span class:"small-label", "YOUR WORKSPACE"> <p "A blank page. A shared idea. Something worth writing.">>
    >
}

pub fn comments(editor,show_resolved,read_only) {
    let entries = review.comments(editor.doc)
    let visible = [for (c in entries where show_resolved or not c.resolved) c];
    <aside id:"comments-pane",class:"review-pane", ["aria-label"]:"Document comments",
        <div class:"review-heading", <strong "Comments"> button("close-comments","ui.comments","Close","×")>
        <p class:"review-summary",string(len([for (c in entries where not c.resolved) c])) ++ " open comments">
        if (len(visible)==0) <p class:"review-empty","Select some text and choose New comment in the Review tab."> else null;
        *[for (c in visible) <article id:"comment-" ++ c.id,class:"comment-card" ++ (if (c.resolved) " resolved" else ""),["data-comment-id"]:c.id,
            <div class:"comment-author", c.author; <span if (c.resolved) "Resolved" else "Open">>
            <blockquote class:"comment-quote",c.quote>
            <p class:"comment-text",c.text>
            <div class:"comment-actions",
                button("comment-go-" ++ c.id,"ui.commentText","Go to text",null,{id:c.id});
                button("comment-resolve-" ++ c.id,"review.resolveComment",if (c.resolved) "Reopen" else "Resolve",null,{id:c.id,resolved:not c.resolved},false,read_only);
                button("comment-delete-" ++ c.id,"review.deleteComment","Delete",null,{id:c.id},false,read_only)>
        >]
    >
}

fn dialog_field(id,label,value,numeric=false,min=0,max=2000) => <label class:"dialog-field",
    <span label> <input id:id, class:"edit-dialog-field", type:if (numeric) "number" else "text",
        value:string(value), *:if (numeric) {min:string(min),max:string(max),step:"1"} else {}>>
fn dialog_select(id,label,values,current) => <label class:"dialog-field", <span label>
    <select id:id, class:"edit-dialog-field", ["aria-label"]:label,
        *[for (entry in values) <option value:string(entry.value), *:if (string(entry.value)==string(current)) {selected:""} else {}, entry.label>]>>
fn form_dialog(d:map,title:string,description:string,fields:array,action:string,label:string) => files.dialog_frame(string(d.kind),title,description,
    [if (d.error is null) null else <p id:"dialog-error",role:"alert",d.error>,*fields],
    [files.dialog_button("cancel","Cancel",false),files.dialog_button(action,label,true)])

pub fn dialog(dialog,file,editor=null,read_only=false) => if (dialog == null) null
    else if (dialog.kind == "unsaved") files.dialog_frame("unsaved","Keep your changes?","Save this document before continuing.",[],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("discard","Discard changes",false),files.dialog_button("save","Save",true)])
    else if (dialog.kind == "exists" or dialog.kind == "changed") files.dialog_frame(dialog.kind,"Replace the existing file?",
        if (dialog.kind == "changed") "The file changed on disk. Replacing it will overwrite that version." else dialog.path ++ " already exists.",[],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("overwrite","Replace",true)])
    else if (dialog.kind == "link") form_dialog(dialog,if (dialog.editing) "Edit link" else "Insert a link",
        "Link the selected text, or enter a new display label.",
        [dialog_field("link-address","Address",dialog.values.href),dialog_field("link-label","Display text",dialog.values.label),
            if (len(insert.bookmarks(editor.doc))==0) null else <div class:"internal-link-fields",
                dialog_select("link-bookmark","Place in this document",[for (b in insert.bookmarks(editor.doc)) {value:b.name,label:b.name}],
                    insert.link_bookmark(dialog.values.href) or "")
                files.dialog_button("internal_link","Link to bookmark",false)>],"link",if (dialog.editing) "Apply" else "Insert link")
    else if (dialog.kind == "cover") form_dialog(dialog,"Insert cover page","Add an editable title page at the beginning of your document.",[
        dialog_field("cover-title","Title",dialog.values.title),dialog_field("cover-subtitle","Subtitle",dialog.values.subtitle),
        dialog_field("cover-author","Author",dialog.values.author)],"cover","Insert cover")
    else if (dialog.kind == "bookmarks") files.dialog_frame("bookmarks","Bookmarks",
        "Name the current paragraph, then jump to it or link to it. One bookmark per paragraph; reusing a name moves it.",[
            if (dialog.error==null) null else <p id:"dialog-error",role:"alert",dialog.error>,
            <div class:"bookmark-list", *[for (b in insert.bookmarks(editor.doc)) <div class:"bookmark-row",
                <div class:"bookmark-description",<strong b.name> <span b.label>>
                button("bookmark-go-" ++ b.name,"ui.bookmarkText","Go to",null,{name:b.name})
                button("bookmark-delete-" ++ b.name,"insert.deleteBookmark","Delete",null,{name:b.name},false,read_only)>]
                if (len(insert.bookmarks(editor.doc))==0) <p class:"bookmark-empty","No bookmarks yet."> else null>,
            if (read_only) null else dialog_field("bookmark-name","Name (letters, digits, underscores)",dialog.values.name)],
        [files.dialog_button("cancel","Close",false),if (read_only) null else files.dialog_button("bookmark","Add / replace",true)])
    else if (dialog.kind == "symbol") form_dialog(dialog,"Insert symbol","Choose a character or enter up to 64 printable characters.",[
        <div class:"symbol-gallery", *[for (category_index,category in insert.symbols) <section class:"symbol-category",
            <div class:"symbol-category-title",category.name>
            <div class:"symbol-grid", *[for (i,c in category.chars)
                button("symbol-" ++ string(category_index) ++ "-" ++ string(i),"ui.chooseSymbol",c,null,{char:c},dialog.values.char==c,false,"symbol-key")]>>]>,
        dialog_field("symbol-text","Character(s)",dialog.values.char)],"symbol","Insert")
    else if (dialog.kind == "comment") form_dialog(dialog,"New comment","The comment stays anchored as its text is edited.",[
        dialog_field("comment-author","Author",dialog.values.author),
        <label class:"dialog-field", <span "Comment"> <textarea id:"comment-text",class:"edit-dialog-field comment-input",dialog.values.text>>],"comment","Add comment")
    else if (dialog.kind == "table") form_dialog(dialog,"Insert table","Choose the table size. Tab moves between cells and adds a row at the end.",[
        <div class:"dialog-grid", dialog_field("table-rows","Rows",dialog.values.rows,true,1,32)
            dialog_field("table-columns","Columns",dialog.values.cols,true,1,12)>,
        dialog_select("table-header","First row",[{value:"true",label:"Header row"},{value:"false",label:"Regular cells"}],dialog.values.header)],"insert_table","Insert table")
    else if (dialog.kind == "paragraph") form_dialog(dialog,"Paragraph settings","Apply spacing and indentation to the selected paragraphs.",[
        <div class:"dialog-grid",
            dialog_select("paragraph-align","Alignment",[for (v in ["left","center","right","justify"]) {value:v,label:v}],dialog.values.align)
            dialog_select("paragraph-lines","Line spacing",[for (v in [1,1.15,1.5,2]) {value:v,label:string(v) ++ " lines"}],dialog.values.lines)
            dialog_field("paragraph-before","Space before (pt)",dialog.values.before,true,0,200)
            dialog_field("paragraph-after","Space after (pt)",dialog.values.after,true,0,200)
            dialog_field("paragraph-indent","Left indent (pt)",dialog.values.indent,true,0,200)
            dialog_field("paragraph-first","First line indent (pt)",dialog.values.first,true,-72,200)>],"paragraph","Apply")
    else if (dialog.kind == "page_setup") form_dialog(dialog,"Page setup","These settings also control exported PDF pages.",[
        <div class:"dialog-grid",
            dialog_select("page-paper","Paper size",[for (p in page.papers) {value:p.id,label:p.id}],dialog.values.paper)
            dialog_select("page-orientation","Orientation",[for (v in ["portrait","landscape"]) {value:v,label:v}],dialog.values.orientation)
            dialog_field("page-top","Top margin (mm)",dialog.values.top,true,0,80)
            dialog_field("page-right","Right margin (mm)",dialog.values.right,true,0,80)
            dialog_field("page-bottom","Bottom margin (mm)",dialog.values.bottom,true,0,80)
            dialog_field("page-left","Left margin (mm)",dialog.values.left,true,0,80)>],"page_setup","Apply")
    else if (dialog.kind == "picture" or dialog.kind == "picture_settings") form_dialog(dialog,
        if (dialog.kind == "picture") "Insert picture" else "Picture settings",
        "Pictures are embedded in the document. Leave height blank to keep the original proportions.",[
            if (dialog.kind == "picture") dialog_field("picture-path","Picture file (PNG, JPEG, GIF, WebP, SVG)",dialog.values.path) else null,
            dialog_field("picture-alt","Alternative text",dialog.values.alt),
            <div class:"dialog-grid",dialog_field("picture-width","Width (px)",dialog.values.width,true,16,650)
                dialog_field("picture-height","Height (px, optional)",dialog.values.height,true,16,2000)>],"picture",if (dialog.kind=="picture") "Insert picture" else "Apply")
    else files.dialog_frame(dialog.kind,
        if (dialog.kind == "open") "Open document" else if (dialog.kind == "export") "Export document" else "Save editable document",
        if (dialog.kind == "open") "Open .word.json, HTML, Markdown, text, or a supported DOCX. Imports create an editable copy."
        else if (dialog.kind == "export") "Choose .html, .txt, or .pdf. PDF uses this document's page setup."
        else "The .word.json format preserves the document and its formatting.",
        [files.field("word-file-path",dialog.path,if (dialog.kind == "open") "path/to/document" else "temp/word/document.word.json")],
        [files.dialog_button("cancel","Cancel",false),files.dialog_button("confirm",if (dialog.kind == "open") "Open" else if (dialog.kind == "export") "Export" else "Save",true)])
