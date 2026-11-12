import m: .mod_model
import f: .mod_formula
import formula_common: .mod_formula_common
import window: .mod_window
import conditional: .mod_conditional
import layout: .mod_grid_layout
import virtual: lambda.ui.core.virtual_grid
import grid_view: lambda.ui.core.virtual_grid_view
import ribbon: lambda.ui.workspace.ribbon
import icons: lambda.ui.workspace.ribbon_icons
import text_format: lambda.ui.workspace.text_format
import workspace_theme: lambda.ui.workspace.tokens
import workspace_dialog: lambda.ui.workspace.dialog

let OFFICE_THEME = workspace_theme.variables({primary:"#217346",text:"#1d1d1f",text_secondary:"#6e6e73",
    border:"#dce3dd",background:"#f5f7f5",surface:"#ffffff",font_family:"-apple-system, 'Helvetica Neue', Arial, sans-serif"})^

let EMPTY_CELL_STYLE={}
fn suffix(st) => st.pane_suffix or ""

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
            <span class:"ribbon-tip","SUM · IF · VLOOKUP · ROUND · COUNTIF\nUse cell references and ranges, like =SUM(C5:E5)">])])>
    else <div class:"editor-ribbon",
        ribbon.group("Show",[ribbon.row([tool("gridlines","Gridlines","table_gridlines",st.grid,false,"home-large"),
            tool("show-formulas","Show Formulas","code",st.formulas,false,"home-large")])]);
        ribbon.group("Zoom",[ribbon.row([tool("zoom-out","Zoom Out","shrink_font",false,false,"home-large"),
            tool("zoom-reset","100%","autofit",false,false,"home-large"),tool("zoom-in","Zoom In","grow_font",false,false,"home-large")])])>
}
fn trim_decimal(text) any^ => if (ends_with(text,"0")) trim_decimal(slice(text,0,len(text)-1))
    else if (ends_with(text,".")) slice(text,0,len(text)-1) else text
// Excel's 1900 calendar includes a fictitious leap day at serial 60.
fn excel_date(value,date1904) any^ {
    let days=int(floor(value));
    if (not date1904 and days==60) "1900-02-29"
    else {
        let epoch=if (date1904) -2082844800000 else -2209075200000
        let offset=if (not date1904 and days>60) days-1 else days
        let rendered=string(date(datetime(epoch+offset*86400000)^)^);
        slice(rendered,2,len(rendered)-1)
    }
}
fn two_digits(value) => (if (value<10) "0" else "") ++ string(value)
// A spreadsheet time is the fractional part of a day, rounded to seconds.
fn excel_time(value) any^ {
    let seconds=int(floor((value-floor(value))*86400+0.5))%86400;
    two_digits(seconds div 3600) ++ ":" ++ two_digits((seconds div 60)%60) ++ ":" ++ two_digits(seconds%60)
}
pub fn formatted(value,style) any^ => if (value is bool) (if (value) "TRUE" else "FALSE")
    else if (not (value is number)) string(value)
    else if (style.format=="date" or style.format=="date1904") excel_date(value,style.format=="date1904")^
    else if (style.format=="time") excel_time(value)^
    else if (style.format=="datetime" or style.format=="datetime1904")
        excel_date(value+0.5/86400,style.format=="datetime1904")^ ++ " " ++ excel_time(value)^
    else if (style.format=="currency") "$" ++ formula_common.fixed(value)
    else if (style.format=="percent") formula_common.fixed(value*100,1) ++ "%"
    else if (style.format=="number") formula_common.fixed(value)
    else if (value==floor(value)) string(int(value)) else trim_decimal(formula_common.fixed(value,6))
// Default cell declarations live in CSS; keep a stable shape for empty cells.
fn cell_style(style,zoom) => (if (style.fill) "background-color:" ++ style.fill ++ ";" else "") ++
    (if (style.color) "color:" ++ style.color ++ ";" else "") ++
    (if (style.font) "font-family:" ++ style.font ++ ";" else "") ++
    (if (style.size) "font-size:" ++ string(style.size*zoom) ++ "px;" else "") ++
    (if (style.bold) "font-weight:700;" else "") ++ (if (style.italic) "font-style:italic;" else "") ++
    (if (style.underline) "text-decoration:underline;" else "") ++
    (if (style.align) "text-align:" ++ style.align ++ ";" else "") ++
    (if (style.wrap) "white-space:normal;" else "") ++ (if (style.border) "border:1px solid #6d8275;" else "")
fn cell_view(st,sheet,row,col,bounds,fragment=null,sticky="") any^ {
    // A visible cell uses the same address for lookup, identity and accessibility.
    let address=f.address(row,col)
    let source=if (fragment==null) {row:row,col:col,rows:1,cols:1} else fragment
    let source_address=f.address(source.row,source.col)
    let cell=f.indexed_cell(sheet,source_address)
    let raw=cell.raw or ""
    let computed=st.cache.values[source_address]
    let value=if (computed==null) "" else computed
    let style=conditional.style(sheet,source.row,source.col,value,cell.style or EMPTY_CELL_STYLE)^
    let active=st.row==source.row and st.col==source.col
    let selected=virtual.contains(bounds,row,col)
    let corner=row==bounds.bottom and col==bounds.right and not st.editing
    let formula=starts_with(raw,"=")
    // Empty sparse cells have no text to format or spill into the next cell.
    // Only the anchor fragment paints content when a merge crosses a pane or window boundary.
    let text=if (fragment!=null and fragment.show==false) "" else if (st.formulas and formula) raw else if (value=="") "" else formatted(value,style)
    let cell_css=sticky ++ (if (len(style)==0) "" else cell_style(style,st.zoom))
    let spill=text!="" and not (value is number) and not formula and not style.wrap and (f.cell(sheet,row,col+1).raw or "")=="";
    <td id:"cell-" ++ address ++ suffix(st),role:"gridcell",'aria-colindex':string(col+1),'data-row':string(source.row),'data-col':string(source.col),'data-raw':raw,
        *:if (source.rows>1) {rowspan:string(source.rows)} else {},*:if (source.cols>1) {colspan:string(source.cols)} else {},
        'aria-selected':string(selected),'aria-label':address ++ (if (text!="") ": " ++ text else ""),
        class:trim((if (active) "active " else "") ++ (if (selected) "selected " else "") ++ (if (corner) "selection-corner " else "") ++
            (if (value is number) "numeric " else "") ++ (if (starts_with(string(value),"#")) "cell-error" else "")),
        style:cell_css,
        if (text!="") <span class:if (spill) "cell-text spill" else "cell-text",text> else null;
        if (formula) <span class:"formula-indicator",title:raw> else null;
        if (corner and suffix(st)=="") <button id:"selection-handle",type:"button",tabindex:"-1",'data-selection-handle':"true",
            'aria-label':"Drag to extend selection",title:"Drag to extend selection"> else null;
        if (active and st.editing and suffix(st)=="") <input id:"cell-editor",'aria-label':"Edit " ++ address,value:st.draft,autocomplete:"off",spellcheck:"false"> else null>
}
fn grid_row(st,sheet,bounds,r,cells,sticky="") => <tr id:"row-" ++ string(r+1) ++ suffix(st),'aria-rowindex':string(r+1),
    class:if (sheet.row_heights[string(r)]!=null or m.resizing(st,"row",r)) "sized-row" else "",
    style:"height:" ++ string(m.used_dimension(st,"row",r)*st.zoom) ++ "px;--row-size:" ++ string(m.used_dimension(st,"row",r)*st.zoom) ++ "px;",
    <th style:sticky,role:"rowheader",'data-rowheader':string(r),class:if (r>=bounds.top and r<=bounds.bottom) "heading-selected" else "",string(r+1);
        resize_handle(st,"row",r)>;
    *cells>
fn ordinary_grid(st,viewport) any^ {
    let sheet=m.indexed(st)
    let bounds=m.bounds(st)
    let axes=window.axes(st)^
    let visible=window.visible(axes,viewport)^
    let rows=visible.rows
    let cols=visible.columns
    let left=cols.before>0
    let right=cols.after>0
    let body=grid_view.body(rows,cols,(r,cells) => grid_row(st,sheet,bounds,r,cells),
        (r,c) any^ => cell_view(st,sheet,r,c,bounds)^)^;
    <div id:"grid-scroll" ++ suffix(st),'data-sheet-pane':st.pane_name or "",class:if (st.grid) "grid-scroll" else "grid-scroll hide-grid",
        <table id:"sheet-grid" ++ suffix(st),class:"sheet-grid",role:"grid",tabindex:"0",'aria-label':sheet.title ++ " spreadsheet",'aria-rowcount':string(sheet.rows),'aria-colcount':string(sheet.cols),
            *:if (st.row>=rows.first and st.row<rows.last and st.col>=cols.first and st.col<cols.last)
                {'aria-activedescendant':"cell-" ++ f.address(st.row,st.col) ++ suffix(st)} else {},
            'data-first-row':string(rows.first),'data-last-row':string(rows.last),
            'data-first-column':string(cols.first),'data-last-column':string(cols.last),
            style:"width:" ++ string(44+virtual.extent(axes.columns)^) ++ "px;font-size:" ++ string(12*st.zoom) ++ "px;",
            <colgroup <col style:"width:44px">;
                if (left) <col style:"width:" ++ string(cols.before) ++ "px"> else null;
                if (cols.first<cols.last) for (c in cols.first to (cols.last-1))
                    <col id:"column-" ++ f.column(c) ++ suffix(st),style:"width:" ++ string(m.used_dimension(st,"column",c)*st.zoom) ++ "px"> else null;
                if (right) <col style:"width:" ++ string(cols.after) ++ "px"> else null>
            <thead <tr <th class:"corner",'data-select':"all",title:"Select all cells">;
                if (left) <th class:"grid-gap",'aria-hidden':"true"> else null;
                if (cols.first<cols.last) for (c in cols.first to (cols.last-1))
                    <th role:"columnheader",'data-column':string(c),'aria-colindex':string(c+1),class:if (c>=bounds.left and c<=bounds.right) "heading-selected" else "",f.column(c);
                        resize_handle(st,"column",c)> else null;
                if (right) <th class:"grid-gap",'aria-hidden':"true"> else null>>
            body>>
}

// Build only frozen prefixes and the current scroll windows; gaps retain geometry.
fn layout_cell(st,sheet,bounds,axes,rows,cols,r,c,rf,cf) any^ {
    let fragment=layout.fragment(sheet,r,c,if (rf) rows.fixed_first else rows.scroll_first,
        if (rf) rows.fixed_end else rows.scroll_end,if (cf) cols.fixed_first else cols.scroll_first,
        if (cf) cols.fixed_end else cols.scroll_end)^;
    if (not fragment.render) null else cell_view(st,sheet,r,c,bounds,fragment,layout.sticky(rf,cf,r,c,axes)^)^
}
fn layout_row(st,sheet,bounds,axes,rows,cols,r,rf) any^ {
    let cells=[*[for (c in cols.fixed) layout_cell(st,sheet,bounds,axes,rows,cols,r,c,rf,true)^],
        if (cols.gap>0) <td class:"grid-gap",'aria-hidden':"true"> else null,
        *[for (c in cols.scroll) layout_cell(st,sheet,bounds,axes,rows,cols,r,c,rf,false)^],
        if (cols.after>0) <td class:"grid-gap",'aria-hidden':"true"> else null]
    let head=if (rf) "position:sticky;left:0;top:" ++ string(26+virtual.offset(axes.rows,r)^) ++ "px;z-index:7;" else "";
    grid_row(st,sheet,bounds,r,cells,head)
}
fn column_header(st,bounds,axes,c,fixed) any^ =>
    <th role:"columnheader",'data-column':string(c),'aria-colindex':string(c+1),
        style:if (fixed) "position:sticky;left:" ++ string(44+virtual.offset(axes.columns,c)^) ++ "px;z-index:6;" else "",
        class:if (c>=bounds.left and c<=bounds.right) "heading-selected" else "",f.column(c);resize_handle(st,"column",c)>
fn layout_grid(st,viewport) any^ {
    let sheet=m.indexed(st)
    let bounds=m.bounds(st)
    let axes=window.axes(st)^
    let visible=window.visible(axes,viewport)^
    let frozen=sheet.frozen or {rows:0,cols:0}
    let rows=layout.segments(sheet,"row",axes.rows,visible.rows,frozen.rows)^
    let cols=layout.segments(sheet,"column",axes.columns,visible.columns,frozen.cols)^
    let span=1+len(cols.fixed)+len(cols.scroll)+(if (cols.gap>0) 1 else 0)+(if (cols.after>0) 1 else 0);
    <div id:"grid-scroll" ++ suffix(st),'data-sheet-pane':st.pane_name or "",class:if (st.grid) "grid-scroll" else "grid-scroll hide-grid",
        <table id:"sheet-grid" ++ suffix(st),class:"sheet-grid",role:"grid",tabindex:"0",'aria-label':sheet.title ++ " spreadsheet",'aria-rowcount':string(sheet.rows),'aria-colcount':string(sheet.cols),
            *:if ((contains(rows.fixed,st.row) or contains(rows.scroll,st.row)) and
                (contains(cols.fixed,st.col) or contains(cols.scroll,st.col)))
                {'aria-activedescendant':"cell-" ++ f.address(st.row,st.col) ++ suffix(st)} else {},
            'data-first-row':string(visible.rows.first),'data-last-row':string(visible.rows.last),
            'data-first-column':string(visible.columns.first),'data-last-column':string(visible.columns.last),
            style:"width:" ++ string(44+virtual.extent(axes.columns)^) ++ "px;font-size:" ++ string(12*st.zoom) ++ "px;",
            <colgroup <col style:"width:44px">;
                for (c in cols.fixed) <col id:"column-" ++ f.column(c) ++ suffix(st),style:"width:" ++ string(m.used_dimension(st,"column",c)*st.zoom) ++ "px">;
                if (cols.gap>0) <col style:"width:" ++ string(cols.gap) ++ "px"> else null;
                for (c in cols.scroll) <col id:"column-" ++ f.column(c) ++ suffix(st),style:"width:" ++ string(m.used_dimension(st,"column",c)*st.zoom) ++ "px">;
                if (cols.after>0) <col style:"width:" ++ string(cols.after) ++ "px"> else null>
            <thead <tr <th class:"corner",'data-select':"all",title:"Select all cells">;
                for (c in cols.fixed) column_header(st,bounds,axes,c,true)^;
                if (cols.gap>0) <th class:"grid-gap",'aria-hidden':"true"> else null;
                for (c in cols.scroll) column_header(st,bounds,axes,c,false)^;
                if (cols.after>0) <th class:"grid-gap",'aria-hidden':"true"> else null>>
            <tbody
                for (r in rows.fixed) layout_row(st,sheet,bounds,axes,rows,cols,r,true)^;
                if (rows.gap>0) <tr class:"grid-spacer",'aria-hidden':"true",<td colspan:string(span),style:"height:" ++ string(rows.gap) ++ "px">> else null;
                for (r in rows.scroll) layout_row(st,sheet,bounds,axes,rows,cols,r,false)^;
                if (rows.after>0) <tr class:"grid-spacer",'aria-hidden':"true",<td colspan:string(span),style:"height:" ++ string(rows.after) ++ "px">> else null>>>
}
fn grid_content(st,viewport) any^ {
    let sheet=m.current(st);
    if (len(sheet.merges or [])==0 and len(sheet.hidden_rows or [])==0 and len(sheet.hidden_columns or [])==0 and
        (sheet.frozen==null or (sheet.frozen.rows==0 and sheet.frozen.cols==0))) ordinary_grid(st,viewport)^
    else layout_grid(st,viewport)^
}

fn split_divider(st,axis,location,value) {
    let views=st.split_views
    let extent=if (axis=="x") (views.topLeft.width or 0)+(views.topRight.width or 0)+6
        else (views.topLeft.height or 0)+(views.bottomLeft.height or 0)+6;
    <div id:"split-divider-" ++ axis ++ "-" ++ location,
        class:"split-divider split-divider-" ++ axis,role:"separator",tabindex:"0",'data-split-resize':axis,
        'aria-label':if (axis=="x") "Resize vertical split" else "Resize horizontal split",
        'aria-orientation':if (axis=="x") "vertical" else "horizontal",'aria-valuemin':"50",
        'aria-valuemax':string(round(max(value,extent-56))),'aria-valuenow':string(round(value))>
}
fn split_pane(st,name,extra) any^ {
    let active=name==st.pane_active
    let viewport=st.split_views[name] or {x:0.0,y:0.0,width:0.0,height:0.0}
    let pane_state={*:st,pane_name:name,pane_suffix:if (active) "" else "--" ++ name};
    <div class:"split-pane " ++ extra,grid_content(pane_state,viewport)^>
}
fn grid(st,viewport) any^ {
    let split=m.current(st).split;
    if (split==null) grid_content(st,viewport)^ else {
        let x=if (st.split_drag!=null and st.split_drag.axis=="x") st.split_drag.value else split.x/15
        let y=if (st.split_drag!=null and st.split_drag.axis=="y") st.split_drag.value else split.y/15;
        <div class:"split-frame",style:"--split-width:" ++ string(x) ++ "px;",'data-split-x':string(split.x),'data-split-y':string(split.y),
            <div class:"split-row " ++ (if (split.y>0) "split-top" else "split-whole"),
                style:if (split.y>0) "height:" ++ string(y) ++ "px;" else "",
                split_pane(st,"topLeft",if (split.x>0) "split-left" else "split-whole")^;
                if (split.x>0) split_divider(st,"x","top",x) else null;
                if (split.x>0) split_pane(st,"topRight","split-right")^ else null>
            if (split.y>0) split_divider(st,"y","middle",y) else null;
            if (split.y>0) <div class:"split-row split-bottom",
                split_pane(st,"bottomLeft",if (split.x>0) "split-left" else "split-whole")^;
                if (split.x>0) split_divider(st,"x","bottom",x) else null;
                if (split.x>0) split_pane(st,"bottomRight","split-right")^ else null> else null>
    }
}

fn resize_handle(st,axis,index) {
    let label=(if (axis=="column") "column " ++ f.column(index) else "row " ++ string(index+1))
    let limits=m.dimension_limits(axis);
    <span id:"resize-" ++ axis ++ "-" ++ string(index) ++ suffix(st),class:axis ++ "-resizer",role:"separator",tabindex:"0",
        'data-sheet-resize':axis,'data-index':string(index),'aria-label':"Resize " ++ label,title:"Drag to resize " ++ label,
        'aria-orientation':if (axis=="column") "vertical" else "horizontal",
        'aria-valuemin':string(limits.min),'aria-valuemax':string(limits.max),'aria-valuenow':string(m.used_dimension(st,axis,index))>
}
pub fn dialog(st) any^ => if (st.dialog=="") null else workspace_dialog.modal(
    {id:"file-dialog",labelled_by:"dialog-title",backdrop_class:"dialog-backdrop"},[
        <h2 id:"dialog-title",{open:"Open workbook",save:"Save workbook",export:"Export CSV",rename:"Rename sheet",replace:"Unsaved changes"}[st.dialog]>,
        if (st.dialog=="replace") <p "Save your workbook before replacing it?"> else
            <label <span if (st.dialog=="rename") "Sheet name" else "File path">;
                <input id:"file-path",value:if (st.dialog=="rename") m.current(st).title else st.dialog_path,'aria-label':if (st.dialog=="rename") "Sheet name" else "File path",placeholder:if (st.dialog=="export") "budget.csv" else if (st.dialog=="open") "workbook.xlsx or workbook.sheet.json" else "workbook.sheet.json">>,
        <p class:"dialog-hint",if (st.dialog=="export") "Exports the active sheet’s calculated values as CSV." else if (st.dialog=="rename") "Give this worksheet a short, descriptive name." else if (st.dialog=="replace") "Your current edits have not been saved." else if (st.dialog=="open") "Open an .xlsx or an editable .sheet.json workbook. XLSX imports save as .sheet.json." else "Editable .sheet.json files preserve every sheet, formula and format.">,
        if (st.error!=null) <p class:"dialog-error",role:"alert",st.error> else null,
        <div class:"dialog-actions",button("dialog-cancel","Cancel");
            if (st.dialog=="replace") button("dialog-discard","Discard changes") else null;
            button("dialog-confirm",if (st.dialog=="replace") "Save first" else if (st.dialog=="open") "Open" else if (st.dialog=="rename") "Rename" else "Save",null,false,false,"primary")>])^
pub fn workspace(st) any^ {
    let dirty=st.book!=st.saved
    let stats=m.stats(st);
    <body id:"sheet",class:"workspace-office",style:OFFICE_THEME,'data-dirty':string(dirty),'data-cell':f.address(st.row,st.col),'data-sheets':string(len(st.book.sheets)),'data-tab':st.tab,
        'data-selecting':string(st.selection_drag!=null),'data-resizing':if (st.resize_drag==null) "" else st.resize_drag.axis,
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
        grid(st,st.viewport)^;
        <div class:"sheet-tabs",<span class:"sheet-nav",icons.icon("table",15)>;
            for (i,s in st.book.sheets) <button type:"button",id:"sheet-tab-" ++ string(i),'data-sheet':string(i),class:if (st.sheet==i) "sheet-tab current" else "sheet-tab",'aria-selected':string(st.sheet==i),s.title>;
            button("add-sheet","New sheet","plus",false,false,"icon-only");<span class:"tab-spacer">;
            <span class:"sheet-hint","Drag or Shift-click to select · Double-click to edit">>
        <footer class:"statusbar",<div class:"status-left",<span class:"status-dot"> <span id:"status",role:"status",st.status>
            <span class:"status-divider"> <span id:"selection-label",f.address(m.bounds(st).top,m.bounds(st).left) ++
                (if (st.anchor.row==st.row and st.anchor.col==st.col) "" else ":" ++ f.address(m.bounds(st).bottom,m.bounds(st).right))>>
            <div class:"status-right",<span id:"selection-stats","Average: " ++ formatted(stats.average,{}) ++ "    Count: " ++ string(stats.count) ++ "    Sum: " ++ formatted(stats.sum,{})>
                <span class:"status-divider">;button("status-zoom-out","Zoom out","minus",false,false,"icon-only");
                <span id:"zoom-label",string(round(st.zoom*100)) ++ "%">;button("status-zoom-in","Zoom in","plus",false,false,"icon-only")>>
        dialog(st)^>
}
