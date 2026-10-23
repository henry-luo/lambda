// table selections stay in source coordinates; chrome and the native caret are projections (D7.2.5).
import dom
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import lambda.editor.mod_editor
import commands: lambda.editor.mod_commands
import format: lambda.editor.mod_inline_format
import controller: lambda.edit.controller
import html: lambda.edit.html
import outcome: lambda.dom.edit_result
import tree: lambda.dom.tree
import colors: lambda.ui.core.component

pub let border_styles = ["solid","dashed","dotted","double","none"]
pub let border_presets = ["all","outside","inside","top","bottom","left","right","none"]
pub fn cells(doc) => [for (entry in descendants(doc) where contains(['td','th'],entry.node.tag)) entry]
pub fn context(editor) {
    let path = format.selection_path(editor.selection)
    let table = commands.ancestor_tag(editor.doc,path,'table')
    let cell = [for (entry in cells(editor.doc) where path_is_prefix(path,entry.path)) entry][0]
    let target = if (cell!=null) cell.path else path
    if (table==null) null else commands.selected_table_context({*:editor,selection:node_selection(target)})
}
pub fn entries(editor) {
    let ctx = context(editor)
    let rows = [for (entry in descendants(ctx.table,ctx.table_path) where entry.node.tag=='tr' and
        commands.ancestor_tag(editor.doc,entry.path,'table')==ctx.table_path) entry];
    [for (r,row in rows) for (c,cell in row.node.content where contains(['td','th'],cell.tag))
        {path:[*row.path,c],node:cell,row:r,col:c,row_path:row.path}]
}
pub fn structural(editor) => editor.selection.kind=='multi-node' or
    (editor.selection.kind=='node' and contains(['table','thead','tbody','tfoot','tr','td','th'],node_at(editor.doc,editor.selection.path).tag))
pub fn selected(editor) {
    let sel = editor.selection
    let paths = selection_paths(sel)
    let lo = if (sel.kind=='text') pos_min(sel.anchor,sel.head) else null
    let hi = if (sel.kind=='text') pos_max(sel.anchor,sel.head) else null;
    [for (entry in entries(editor) where sel.kind=='all' or
        any([for (path in paths) path_is_prefix(path,entry.path) or path_is_prefix(entry.path,path)]) or
        (sel.kind=='text' and (path_is_prefix(entry.path,lo.path) or
            (path_compare(entry.path,lo.path)>0 and path_compare(entry.path,hi.path)<=0 and
                not (hi.offset==0 and path_is_prefix(entry.path,hi.path)))))) entry]
}
pub fn native_editor(editor) {
    let first = if (structural(editor)) selected(editor)[0] else null
    let point = pos(first.path,0)
    if (first==null) editor else {*:editor,selection:text_selection(point,point)}
}
pub fn result(editor,run) => if (structural(editor)) outcome.with_source_selection(run,native_editor(editor).selection,editor.model_revision) else run
pub fn command_editor(editor) {
    let ctx = context(editor)
    if (ctx==null or not structural(editor)) editor else {*:editor,selection:node_selection(ctx.cell_path)}
}
pub fn selection(editor,scope,path=null,head=null) {
    let base = if (path==null) context(editor).cell_path else path
    let all_cells = entries({*:editor,selection:node_selection(base)})
    let a = [for (entry in all_cells where entry.path==base) entry][0]
    let b = [for (entry in all_cells where entry.path==(head or base)) entry][0]
    let chosen = [for (entry in all_cells where
        (scope=="table") or
        ((scope=="row" or (entry.col>=min(a.col,b.col) and entry.col<=max(a.col,b.col))) and
         (scope=="column" or (entry.row>=min(a.row,b.row) and entry.row<=max(a.row,b.row))))) entry.path]
    if (a==null or b==null or len(chosen)==0) editor.selection
    else if (scope=="table") node_selection(context({*:editor,selection:node_selection(base)}).table_path)
    else if (scope=="row" and a.row==b.row) node_selection(a.row_path)
    else if (len(chosen)==1) node_selection(chosen[0]) else multi_node_selection(chosen)
}
pub fn border_settings(editor) {
    let css = attrs_get(selected(editor)[0].node.attrs,'style')
    let raw = format.css_get(css,"border-top") or format.css_get(css,"border") or "0.75pt solid #d6e0e7"
    let parts = split(raw," ")
    let style = [for (part in parts where contains(border_styles,part)) part][0]
    let width = (float(replace(replace(parts[0],"pt",""),"px","")) or 0.75)*if (ends_with(parts[0],"px")) 0.75 else 1
    // an absent edge has no usable pen; quick presets must be able to draw it again.
    {preset:"all",style:if (style=="none") "solid" else style or "solid",width:if (width>0) width else 0.75,
        color:[for (part in parts where colors.hex_color(part)) part][0] or "#d6e0e7"}
}
let sides = [{id:"top",opposite:"bottom",r: -1,c:0},{id:"right",opposite:"left",r:0,c:1},
    {id:"bottom",opposite:"top",r:1,c:0},{id:"left",opposite:"right",r:0,c: -1}]
fn set_border(tx,path,side,value) => tx_step(tx,step_set_attr(path,'style',
    format.css_set(attrs_get(node_at(tx.doc_after,path).attrs,'style'),"border-" ++ side,value)))
fn border_edges(tx,all,picked,p,i=0,j=0) {
    if (i>=len(picked)) tx else if (j>=len(sides)) border_edges(tx,all,picked,p,i+1,0) else {
        let cell = picked[i]
        let side = sides[j]
        let neighbor = [for (entry in all where entry.row==cell.row+side.r and entry.col==cell.col+side.c) entry][0]
        let inside = neighbor!=null and any([for (entry in picked) entry.path==neighbor.path])
        let applies = contains(["all","none",side.id],p.preset) or (p.preset=="outside" and not inside) or (p.preset=="inside" and inside)
        let value = if (p.preset=="none" or p.style=="none") "0pt none " ++ p.color else string(p.width) ++ "pt " ++ p.style ++ " " ++ p.color
        let next = if (not applies) tx else set_border(tx,cell.path,side.id,value)
        // collapsed table edges belong to both cells; a neighbor's old solid border must not win.
        let shared = if (applies and neighbor!=null) set_border(next,neighbor.path,side.opposite,value) else next
        border_edges(shared,all,picked,p,i,j+1)
    }
}
pub fn borders(editor,params) map^ {
    let p = {*:border_settings(editor),*:params}
    if (len(selected(editor))==0) raise error("Select a table cell first.")
    else if (not contains(border_presets,p.preset) or not contains(border_styles,p.style) or
        not colors.hex_color(p.color) or not (p.width is number) or p.width<0.25 or p.width>6)
        raise error("Choose a border style, color, and width from 0.25 to 6 pt.")
    else border_edges(tx_begin(editor.doc,editor.selection),entries(editor),selected(editor),p)
}
pub fn clear_transaction(editor) {
    let chosen = selected(editor)
    let tx = reduce([tx_begin(editor.doc,editor.selection),*chosen],(tx,cell)=>
        tx_step(tx,step_replace(cell.path,0,len(cell.node.content),[])))
    let point = pos(chosen[0].path,0)
    tx_set_selection(tx,text_selection(point,point))
}
pub fn replace_transaction(editor,input_type,data,markup="") {
    let chosen = selected(editor)
    let origin = chosen[0]
    let start = pos([0],0)
    let parsed = if (input_type=="insertFromPaste" and markup!="") commands.cmd_paste_html(
        {*:editor,doc:node('doc',[node('p',[])]),selection:text_selection(start,start)},markup,data) else null
    let table = [for (entry in descendants(parsed.doc_after) where entry.node.tag=='table') entry][0]
    if (table!=null) {
        let source = entries({*:editor,doc:parsed.doc_after,selection:node_selection(table.path)})
        let targets = if (len(chosen)==1) entries(editor) else chosen
        // paste cell contents into the existing grid; never replace cells with nested table nodes.
        let tx = reduce([tx_begin(editor.doc,editor.selection),*targets],(tx,cell)=>{
            let source_cell = if (len(source)==1) source[0] else [for (entry in source where
                entry.row==cell.row-origin.row and entry.col==cell.col-origin.col) entry][0]
            if (source_cell==null or cell.row<origin.row or cell.col<origin.col) tx else
                tx_step(tx,step_replace(cell.path,0,len(cell.node.content),source_cell.node.content))
        })
        tx_set_selection(tx,editor.selection)
    } else {
        let cleared = clear_transaction(editor)
        let target = {*:editor,doc:cleared.doc_after,selection:cleared.sel_after}
        let insertion = if (not starts_with(input_type,"insert")) null
            else if (input_type!="insertFromPaste") commands.cmd_insert_text(target,data)
            else if (markup!="") commands.cmd_paste_html(target,markup,data)
            else commands.cmd_paste_text(target,data)
        if (insertion==null) cleared else tx_set_selection(
            commands.tx_apply_steps(cleared,insertion.steps,0,len(insertion.steps)),insertion.sel_after)
    }
}
fn clipboard(editor,run) {
    let chosen = selected(editor)
    let rows = unique([for (entry in chosen) entry.row_path])
    let fragment = node('doc',[node('table',[for (path in rows)
        node('tr',[for (entry in chosen where entry.row_path==path) entry.node])])])
    let text = join([for (path in rows) join([for (entry in chosen where entry.row_path==path) doc_text(entry.node)],"\t")],"\n")
    outcome.with_clipboard_html(run,html.export_text(fragment,{document:false,head:<head>}),text)
}
pub pn handle(editor,evt,read_only=false) {
    if (read_only or not structural(editor)) { return controller.handle(editor,evt,read_only) }
    let copied = contains(["copy","deleteByCut"],evt.input_type)
    if (evt.input_type=="copy") {
        return {editor:editor,result:clipboard(editor,outcome.model_applied(false,false,false,"",native_editor(editor).selection,editor.model_revision))}
    }
    let replace = contains(["insertText","insertReplacementText","insertFromPaste","deleteContentBackward","deleteContentForward","deleteByCut"],evt.input_type)
    if (replace) {
        let tx = replace_transaction(editor,evt.input_type,evt.data or "",evt.html or "")
        let next = edit_apply(editor,tx)
        let run = result(next,outcome.model_applied(next.doc!=editor.doc,true,true,"clipboard",next.selection,next.model_revision))
        return {editor:next,result:if (copied) clipboard(editor,run) else run}
    }
    let run = controller.handle(editor,evt,false,"html5_subset","edit-surface",editor.selection)
    return {*:run,result:result(run.editor,run.result)}
}

// document order is shared by the model projection and the live cell elements.
pub pn cell_element(editor,node,path) {
    let entries = cells(editor.doc)
    let index = [for (i,entry in entries where entry.path==path) i][0]
    return if (index==null) null else dom.query_selector_all(controller.surface(node),"td,th")[index]
}
pub pn hit(editor,node) {
    let target = dom.closest(node,"td,th")
    let host = controller.surface(node)
    if (target==null or host==null or not dom.contains(host,target)) { return null }
    // native node identity distinguishes cells whose source wrappers compare equal.
    let index = tree.index_in(dom.query_selector_all(host,"td,th"),target)
    return cells(editor.doc)[index].path
}
pub pn point(editor,node,x,y) {
    let target = dom.element_from_point(dom.owner_document(node),x,y)
    return hit(editor,target) or gesture(editor,{target:target}).path
}
pub pn paint(editor,node) {
    let paths = if (structural(editor)) [for (entry in selected(editor)) entry.path] else []
    for (i,cell in dom.query_selector_all(controller.surface(node),"td,th")) {
        let active = contains(paths,cells(editor.doc)[i].path)
        if (active) dom.set_attribute(cell,"data-word-cell-selected","true")
        else if (dom.get_attribute(cell,"data-word-cell-selected")!=null) dom.remove_attribute(cell,"data-word-cell-selected")
    }
}
view <table_grips> {
    let editor = ~.editor
    let ctx = context(editor)
    let all_cells = entries(editor)
    let rows = unique([for (entry in all_cells) entry.row_path]);
    if (~.disabled or ctx==null) null else <div id:"table-grips",contenteditable:"false",
        <button id:"table-grip-table",class:"table-grip",["data-table-scope"]:"table",["aria-label"]:"Select table",title:"Select table">;
        <button id:"table-grip-cell",class:"table-grip cell-grip",["data-table-scope"]:"cell",["aria-label"]:"Select cells",title:"Select cell; drag across cells to select a range.">;
        *[for (i,path in rows) <button id:"table-grip-row-" ++ string(i),class:"table-grip row-grip",["data-table-scope"]:"row",
            ["data-table-index"]:string(i),["aria-label"]:"Select row " ++ string(i+1),title:"Select row; drag to select more rows.">];
        *[for (i,cell in all_cells where cell.row==0) <button id:"table-grip-column-" ++ string(cell.col),class:"table-grip column-grip",["data-table-scope"]:"column",
            ["data-table-index"]:string(cell.col),["aria-label"]:"Select column " ++ string(cell.col+1),title:"Select column; drag to select more columns.">]>
}
pub fn tools(editor,disabled) => apply(<table_grips editor:editor,disabled:disabled>)
pub pn position(editor,node) {
    paint(editor,node)
    let root = dom.root_node(node)
    let origin = dom.bounding_box(dom.get_element_by_id(root,"paper-wrap"))
    let all_cells = entries(editor)
    let ctx = context(editor)
    let table = dom.closest(cell_element(editor,node,ctx.cell_path),"table")
    let bounds = dom.bounding_box(table)
    for (grip in dom.query_selector_all(root,"#table-grips .table-grip")) {
        let scope = dom.get_attribute(grip,"data-table-scope")
        let index = int(dom.get_attribute(grip,"data-table-index") or "0")
        let cell = if (scope=="cell") ctx.cell_path else [for (entry in all_cells where
            (scope=="row" and entry.row==index) or (scope=="column" and entry.row==0 and entry.col==index)) entry.path][0]
        let box = if (scope=="table") bounds else dom.bounding_box(cell_element(editor,node,cell))
        let rectangle = if (scope=="row") {left:bounds.x-18,top:box.y,width:16,height:box.height}
            else if (scope=="column") {left:box.x,top:bounds.y-18,width:box.width,height:16}
            else if (scope=="cell") {left:box.x+1,top:box.y+1,width:9,height:9}
            else {left:bounds.x-18,top:bounds.y-18,width:16,height:16}
        for (key,value in rectangle) dom.style_set_property(grip,string(key),string(value-if (string(key)=="left") origin.x else if (string(key)=="top") origin.y else 0) ++ "px")
    }
}
pub pn gesture(editor,evt) {
    let grip = dom.closest(evt.target,"[data-table-scope]")
    let scope = dom.get_attribute(grip,"data-table-scope")
    let index = int(dom.get_attribute(grip,"data-table-index") or "0")
    let path = if (scope=="row" or scope=="column") [for (entry in entries(editor) where
        (scope=="row" and entry.row==index) or (scope=="column" and entry.col==index)) entry.path][0]
        else if (scope!=null) context(editor).cell_path else hit(editor,evt.target)
    return if (scope==null and not (evt.shiftKey and structural(editor) and path!=null)) null
    else {handle:"table",scope:scope or "cell",path:if (scope==null) selected(editor)[0].path else path,head:path,pointer:evt.pointerId}
}
pub fn preview(editor,gesture) => {*:editor,selection:selection(editor,gesture.scope,gesture.path,gesture.head)}
