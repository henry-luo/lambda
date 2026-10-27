import m: .mod_model
import f: .mod_formula
import ribbon: ~~.mod_ribbon
import icons: ~~.word.mod_ribbon_icons
import text_format: ~~.mod_text_format

pub fn button(id,label,graphic=null,pressed=false,disabled=false,cls="") => <button id:id,type:"button",class:"tool " ++ cls,
    'data-action':id,title:label,'aria-label':label,'aria-pressed':string(pressed),*:if (disabled) {disabled:""} else {},
    if (graphic!=null) <span class:"tool-icon",icons.icon(graphic,17)> else null;
    <span class:"tool-label",label>>
fn tool(id,label,graphic,pressed=false,disabled=false,cls="icon-only",action=null) => {
    let node=ribbon.tool(id,label,graphic,cls,action);
    <button *:map(node),'aria-pressed':string(pressed),*:if (disabled) {disabled:""} else {},*content(node)>
}
fn selector(id,label,choices,value) => <select id:id,'aria-label':label,'data-property':id,
    for (choice in choices) <option value:string(choice),*:if (string(value)==string(choice)) {selected:""} else {},string(choice)>>
fn palette(id,label,choices,value) => <select id:id,'aria-label':label,title:label,'data-property':id,
    for (choice in choices) <option value:choice.value,*:if (choice.value==value) {selected:""} else {},choice.label>>
pub fn toolbar(st) {
    let style=m.active(st).style or {}
    if (st.tab=="Home") <div class:"editor-ribbon",
        ribbon.group("Undo",[ribbon.row([tool("undo","Undo","undo",false,len(st.undo)==0),tool("redo","Redo","redo",false,len(st.redo)==0)]),
            ribbon.row([tool("home-save","Save","save",false,false,"icon-only","save"),tool("clear","Clear contents","clear")])]);
        ribbon.group("Clipboard",[ribbon.row([tool("paste","Paste","paste",false,false,"home-large"),
            ribbon.stack([tool("cut","Cut","cut",false,false,""),tool("copy","Copy","copy",false,false,"")])])]);
        ribbon.group("Font",[ribbon.row([selector("font","Font family",["Arial","Helvetica Neue","Georgia","Courier New"],style.font or "Arial"),
            selector("size","Font size",[10,11,12,14,16,18,20,24],style.size or 12)]),
            ribbon.row([tool("bold","Bold","bold",style.bold==true),tool("italic","Italic","italic",style.italic==true),
                tool("underline","Underline","underline",style.underline==true),
                palette("fill","Cell fill",[{value:"transparent",label:"No fill"},*text_format.shading_colors],style.fill or "transparent"),
                palette("color","Font color",text_format.font_colors,style.color or text_format.ink)])]);
        ribbon.group("Alignment",[ribbon.row([tool("align-left","Align left","align_left",style.align=="left"),
            tool("align-center","Center","align_center",style.align=="center"),tool("align-right","Align right","align_right",style.align=="right")]),
            ribbon.row([tool("wrap","Wrap text","text_box",style.wrap==true,false,""),tool("border","Borders","border_all",style.border==true)])]);
        ribbon.group("Number",[selector("number-format","Number format",["general","number","currency","percent"],style.format or "general"),
            ribbon.row([button("currency","Currency",null,style.format=="currency"),button("percent","Percent",null,style.format=="percent")])]);
        ribbon.group("Editing",[ribbon.row([tool("autosum","AutoSum","equation",false,false,"home-large"),
            ribbon.stack([tool("sort-asc","Sort A to Z","sort",false,false,""),tool("sort-desc","Sort Z to A","sort",false,false,"")])])])>
    else if (st.tab=="Insert") <div class:"editor-ribbon",
        ribbon.group("Workbook",[ribbon.row([tool("insert-sheet","New Sheet","blank_page",false,false,"home-large","add-sheet"),tool("rename-sheet","Rename Sheet","edit_shape",false,false,"home-large")])]);
        ribbon.group("Rows",[ribbon.row([tool("add-rows","Add 10 Rows","insert_below",false,false,"home-large")])]);
        ribbon.group("Functions",[ribbon.row([tool("autosum","AutoSum","equation",false,false,"home-large"),
            <span class:"ribbon-tip","SUM · AVERAGE · MIN · MAX · COUNT\nUse cell references and ranges, like =SUM(C5:E5)">])])>
    else <div class:"editor-ribbon",
        ribbon.group("Show",[ribbon.row([tool("gridlines","Gridlines","table_gridlines",st.grid,false,"home-large"),
            tool("show-formulas","Show Formulas","code",st.formulas,false,"home-large")])]);
        ribbon.group("Zoom",[ribbon.row([tool("zoom-out","Zoom Out","shrink_font",false,false,"home-large"),
            tool("zoom-reset","100%","autofit",false,false,"home-large"),tool("zoom-in","Zoom In","grow_font",false,false,"home-large")])])>
}
// integer pieces avoid the general numeric formatter's significant-digit rounding.
fn fixed(value,places=2) => {
    let scale=10**places
    let rounded=int(round(abs(value)*scale))
    let whole=int(floor(rounded/scale))
    let fraction=string(rounded%scale);
    (if (value<0) "-" else "") ++ string(whole) ++
        (if (places==0) "" else "." ++ join(fill(max(0,places-len(fraction)),"0"),"") ++ fraction)
}
fn trim_decimal(text) => if (ends_with(text,"0")) trim_decimal(slice(text,0,len(text)-1))
    else if (ends_with(text,".")) slice(text,0,len(text)-1) else text
pub fn formatted(value,style) => if (not (value is number)) string(value)
    else if (style.format=="currency") "$" ++ fixed(value)
    else if (style.format=="percent") fixed(value*100,1) ++ "%"
    else if (style.format=="number") fixed(value)
    else if (value==floor(value)) string(int(value)) else trim_decimal(fixed(value,6))
fn cell_style(style,numeric,zoom) => "background-color:" ++ (style.fill or "transparent") ++ ";color:" ++ (style.color or "#263b30") ++
    ";font-family:" ++ (style.font or "Arial") ++ ";font-size:" ++ string((style.size or 12)*zoom) ++ "px;font-weight:" ++ (if (style.bold) "700" else "400") ++
    ";font-style:" ++ (if (style.italic) "italic" else "normal") ++ ";text-decoration:" ++ (if (style.underline) "underline" else "none") ++
    ";text-align:" ++ (style.align or (if (numeric) "right" else "left")) ++ ";" ++
    (if (style.wrap) "white-space:normal;" else "") ++ (if (style.border) "border:1px solid #6d8275;" else "")
fn cell_view(st,row,col,values) {
    let cell=f.cell(m.current(st),row,col)
    let raw=cell.raw or ""
    let computed=values[f.address(row,col)]
    let value=if (computed==null) "" else computed
    let style=cell.style or {}
    let active=st.row==row and st.col==col
    let selected=m.selected(st,row,col)
    let formula=starts_with(raw,"=")
    let text=if (st.formulas and formula) raw else formatted(value,style)
    let spill=not (value is number) and not formula and not style.wrap and (f.cell(m.current(st),row,col+1).raw or "")=="";
    <td id:"cell-" ++ f.address(row,col),role:"gridcell",'data-row':string(row),'data-col':string(col),'data-raw':raw,
        'aria-selected':string(selected),'aria-label':f.address(row,col) ++ (if (text!="") ": " ++ text else ""),
        class:trim((if (active) "active " else "") ++ (if (selected) "selected " else "") ++
            (if (starts_with(string(value),"#")) "cell-error" else "")),style:cell_style(style,value is number,st.zoom),
        <span class:if (spill) "cell-text spill" else "cell-text",text>
        if (formula) <span class:"formula-indicator",title:raw> else null;
        if (active and st.editing) <input id:"cell-editor",'aria-label':"Edit " ++ f.address(row,col),value:st.draft,autocomplete:"off",spellcheck:"false"> else null>
}
pub fn grid(st) {
    let sheet=m.current(st)
    let values=map([for (cell in sheet.cells) *[f.address(cell.row,cell.col),f.display(sheet,cell.row,cell.col)]]);
    <div id:"grid-scroll",class:if (st.grid) "grid-scroll" else "grid-scroll hide-grid",
        <table id:"sheet-grid",role:"grid",tabindex:"0",'aria-label':sheet.title ++ " spreadsheet",'aria-rowcount':string(sheet.rows),'aria-colcount':string(sheet.cols),
            'aria-activedescendant':"cell-" ++ f.address(st.row,st.col),style:"width:" ++ string(44+(220+150+(sheet.cols-2)*124)*st.zoom) ++ "px;",
            <colgroup <col style:"width:44px">;for (c in 0 to (sheet.cols-1)) <col style:"width:" ++ string((if (c==0) 220 else if (c==1) 150 else 124)*st.zoom) ++ "px">>
            <thead <tr <th class:"corner",'data-select':"all",title:"Select all cells">;
                for (c in 0 to (sheet.cols-1)) <th role:"columnheader",'data-column':string(c),class:if (c>=m.bounds(st).left and c<=m.bounds(st).right) "heading-selected" else "",f.column(c)>>>
            <tbody for (r in 0 to (sheet.rows-1)) <tr style:"height:" ++ string((if (r==0) 39 else 27)*st.zoom) ++ "px;",
                <th role:"rowheader",'data-rowheader':string(r),class:if (r>=m.bounds(st).top and r<=m.bounds(st).bottom) "heading-selected" else "",string(r+1)>;
                for (c in 0 to (sheet.cols-1)) cell_view(st,r,c,values)>>>>
}
pub fn dialog(st) => if (st.dialog=="") null else <div class:"dialog-backdrop",
    <section id:"file-dialog",role:"dialog",'aria-modal':"true",'aria-labelledby':"dialog-title",
        <h2 id:"dialog-title",{open:"Open workbook",save:"Save workbook",export:"Export CSV",rename:"Rename sheet",replace:"Unsaved changes"}[st.dialog]>
        if (st.dialog=="replace") <p "Save your workbook before replacing it?"> else
            <label <span if (st.dialog=="rename") "Sheet name" else "File path">;
                <input id:"file-path",value:if (st.dialog=="rename") m.current(st).title else st.dialog_path,'aria-label':if (st.dialog=="rename") "Sheet name" else "File path",placeholder:if (st.dialog=="export") "budget.csv" else "workbook.sheet.json">>;
        <p class:"dialog-hint",if (st.dialog=="export") "Exports the active sheet’s calculated values as CSV." else if (st.dialog=="rename") "Give this worksheet a short, descriptive name." else if (st.dialog=="replace") "Your current edits have not been saved." else "Editable .sheet.json files preserve every sheet, formula and format.">
        if (st.error!=null) <p class:"dialog-error",role:"alert",st.error> else null;
        <div class:"dialog-actions",button("dialog-cancel","Cancel");
            if (st.dialog=="replace") button("dialog-discard","Discard changes") else null;
            button("dialog-confirm",if (st.dialog=="replace") "Save first" else if (st.dialog=="open") "Open" else if (st.dialog=="rename") "Rename" else "Save",null,false,false,"primary")>>>
pub fn workspace(st) {
    let dirty=st.book!=st.saved
    let stats=m.stats(st);
    <body id:"sheet",'data-dirty':string(dirty),'data-cell':f.address(st.row,st.col),'data-sheets':string(len(st.book.sheets)),'data-tab':st.tab,
        <header class:"titlebar",
            <div class:"brand",<span class:"brand-mark",icons.icon("table",30)> <span "Sheet">>
            <div class:"title-stack",
                <div class:"title-line",<input id:"workbook-title",value:st.book.title,'aria-label':"Workbook title"> <span class:"save-indicator",if (dirty) "Unsaved" else "Saved">>
                <nav class:"tabs",'aria-label':"Ribbon tabs",
                    button("new","New");button("open","Open");<span class:"tab-divider">;
                    for (tab in ["Home","Insert","View"]) <button type:"button",class:"tool tab",'data-tab':tab,'aria-pressed':string(st.tab==tab),tab>;
                    <span class:"tab-spacer">;button("sample","Studio example")>>
            <div class:"title-actions",button("save","Save","save");button("export","Export CSV","export")>>
        <div class:"ribbon-shell",toolbar(st)>
        <div class:"formula-bar",
            <input id:"name-box",value:f.address(st.row,st.col),'aria-label':"Cell address",title:"Go to a cell, e.g. B12">
            <span class:"formula-divider">;button("cancel-edit","Cancel edit","close",false,false,"icon-only");
            button("accept-edit","Accept edit","check",false,false,"icon-only");<span class:"fx","ƒx">;
            <input id:"formula-input",value:if (st.editing) st.draft else m.active(st).raw or "",'aria-label':"Formula bar",autocomplete:"off",spellcheck:"false">>
        grid(st);
        <div class:"sheet-tabs",<span class:"sheet-nav",icons.icon("table",15)>;
            for (i,s in st.book.sheets) <button type:"button",id:"sheet-tab-" ++ string(i),'data-sheet':string(i),class:if (st.sheet==i) "sheet-tab current" else "sheet-tab",'aria-selected':string(st.sheet==i),s.title>;
            button("add-sheet","New sheet","plus",false,false,"icon-only");<span class:"tab-spacer">;
            <span class:"sheet-hint","Double-click to edit · Shift-click to select a range">>
        <footer class:"statusbar",<div class:"status-left",<span class:"status-dot"> <span id:"status",role:"status",st.status>
            <span class:"status-divider"> <span id:"selection-label",f.address(m.bounds(st).top,m.bounds(st).left) ++
                (if (st.anchor.row==st.row and st.anchor.col==st.col) "" else ":" ++ f.address(m.bounds(st).bottom,m.bounds(st).right))>>
            <div class:"status-right",<span id:"selection-stats","Average: " ++ formatted(stats.average,{}) ++ "    Count: " ++ string(stats.count) ++ "    Sum: " ++ formatted(stats.sum,{})>
                <span class:"status-divider">;button("status-zoom-out","Zoom out","minus",false,false,"icon-only");
                <span id:"zoom-label",string(round(st.zoom*100)) ++ "%">;button("status-zoom-in","Zoom in","plus",false,false,"icon-only")>>
        dialog(st)>
}
