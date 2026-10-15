// View state owns review policy; DOM/clipboard operations are transport (D7.2.5, S12.1.3).
import dom
import units: lambda.dom.edit_history
import c: .common
import rows: .rows
import p: .presentation
import jobs: .jobs

fn host(target) {
    let nearest = dom.closest(target,".diff-interactive");
    if (nearest != null) nearest else dom.query_selector(dom.root_node(target),".diff-interactive")
}
fn current(session) => if (session.document == null) null else session.document.files[session.document_index]
fn files(session) => if (session.manifest != null) session.manifest.files else session.document.files
fn visible_files(session) => [for (change in files(session) where session.filter == "" or
    contains(lower(rows.file_name(change)),lower(session.filter))) change]
fn selected_path(session) {
    let change = files(session)[session.selected];
    if (change == null) null else c.get(change,"new_path",change.old_path)
}
fn bounded(session,top) => max(0,min(max(0,session.projection.count - session.visible_rows),top))
fn moved(session,top) => {*:session,top:bounded(session,top)}
fn comparison(session,mode = null) => if (mode == null) session.comparison else {*:session.comparison,mode:mode}
fn empty_session(document,options) => {repo:null,manifest:null,document:document,document_index:0,
    selected:if (len(document.files) > 0) 0 else null,projection:rows.index(document.files[0]),
    top:0,file_top:0,visible_rows:c.get(options,"rows",28),file_rows:c.get(options,"file_rows",18),
    filter:"",query:"",matches:[],match_index:0,selection:null,projected:null,frame:0,
    generation:0,pending:null,message:"",comparison:{mode:"unstaged"},options:options,io_options:{}}
fn check_dimensions(options) bool^ => c.require(c.get(options,"rows",28) is int and c.get(options,"rows",28) > 0 and
    c.get(options,"file_rows",18) is int and c.get(options,"file_rows",18) > 0,"invalid viewport size")^
fn counts(change) => if (change.added == null) "" else " +" ++ string(change.added) ++ " −" ++ string(change.removed)
fn file_button(change,selected) => <button type:"button",["data-diff-command"]:"file",
    ["data-diff-id"]:string(change.id),["aria-current"]:string(change.id == selected),title:rows.file_name(change),
    <span class:"diff-kind",change.kind> <span class:"diff-path",rows.file_name(change)> counts(change)>
fn tree(session,title) element^ {
    let change = current(session)
    let listing = visible_files(session)
    let shown = slice(listing,session.file_top,session.file_top + session.file_rows);
    <main class:"diff-review diff-interactive",tabindex:"0",["aria-label"]:"Diff review",
        ["data-generation"]:string(session.generation),["data-top"]:string(session.top),
        <header class:"diff-toolbar",<span class:"diff-title",title>
            if (session.repo != null) [*[for (mode in ["unstaged","staged","working"]) <button type:"button",
                ["data-diff-command"]:"mode",["data-diff-mode"]:mode,["aria-pressed"]:string(session.comparison.mode == mode),
                {unstaged:"Unstaged",staged:"Staged",working:"Working tree"}[mode]>],p.button("refresh","Refresh")]
            else null>
        <div class:"diff-workspace",
            <aside class:"diff-sidebar",["aria-label"]:"Changed files",
                <input class:"diff-filter",type:"search",placeholder:"Filter files",value:session.filter,["aria-label"]:"Filter files">
                <div class:"diff-files",*[for (i,entry in shown)
                    (if (i == 0 or entry.kind == "untracked" and shown[i - 1].kind != "untracked")
                        <span class:"diff-group",if (entry.kind == "untracked") "Untracked" else "Changes"> else null,
                     file_button(entry,session.selected))]>
                <div p.button("files-prev","↑ Files",session.file_top == 0);
                    p.button("files-next","↓ Files",session.file_top + session.file_rows >= len(listing))>>
            <section class:"diff-main",
                if (change != null) p.header(change) else <header class:"diff-file-header","No changes.">;
                <nav class:"diff-navigation",["aria-label"]:"Diff navigation",
                    p.button("previous","Previous hunk",change == null);
                    p.button("next","Next hunk",change == null);
                    <input class:"diff-query",type:"search",placeholder:"Find in this file",value:session.query,["aria-label"]:"Find in this file">
                    p.button("find","Find",change == null);
                    p.button("find-next","Next match",len(session.matches) == 0);
                    p.button("copy-old","Copy old",change == null);
                    p.button("copy-new","Copy new",change == null)>
                <div class:"diff-surface",tabindex:"0",role:"region",["aria-label"]:"Unified diff",
                    if (change == null) <p class:"diff-summary",if (session.pending != null) "Loading changes…" else "No changes.">
                    else *[for (value in rows.window(session.document,change,session.projection,session.top,session.visible_rows)^)
                        p.row(value,session.selection)]>
                <div class:"diff-navigation",p.button("page-prev","↑ Page",session.top == 0);
                    p.button("page-next","↓ Page",session.top + session.visible_rows >= session.projection.count);
                    <span class:"diff-position",string(session.top + 1) ++ "–" ++
                        string(min(session.top + session.visible_rows,session.projection.count)) ++ " / " ++ string(session.projection.count)>>>
        >
        <footer class:"diff-footer",<span class:"diff-status",["aria-live"]:"polite",
            if (session.pending != null) "Loading…" else if (session.message != "") session.message
            else string(len(files(session))) ++ " changed files" ++
                (if (len(session.matches) > 0) " · " ++ string(len(session.matches)) ++ " matches" else "")>
            <span "Read only · + added · − removed">>>
}

pn request(target,session) map {
    let token = dom.request_frame(host(target),"diff_ready");
    return {*:session,frame:token}
}
pn refresh(target,session,path,mode = null) map^ {
    if (session.pending != null) cancel(session.pending)
    let generation = session.generation + 1
    let chosen = comparison(session,mode)
    let task = jobs.start_capture(path,session.repo,chosen,selected_path(session),generation,session.io_options)^;
    return request(target,{*:session,generation:generation,comparison:chosen,pending:task,message:""})
}
pn choose(target,session,id) map^ {
    if (session.pending != null) cancel(session.pending)
    if (session.repo == null) {
        let change = session.document.files[id];
        return request(target,{*:session,selected:id,document_index:id,projection:rows.index(change),top:0,selection:null,
            projected:null,query:"",matches:[],pending:null,message:""})
    }
    let generation = session.generation + 1
    let task = jobs.start_load(session.repo,session.manifest,id,generation,session.io_options)^;
    return request(target,{*:session,generation:generation,pending:task,selected:id,document:null,top:0,
        message:"",selection:null,projected:null,query:"",matches:[]})
}
fn installed(session,result) map {
    let manifest = if (result.kind == "capture") result.manifest else session.manifest
    let document = result.document
    let change = document.files[0]
    let was_same = result.selected == session.selected and
        (result.kind != "capture" or selected_path(session) == c.get(change,"new_path",change.old_path))
    let projection = rows.index(change);
    {*:session,repo:if (result.kind == "capture") result.repo else session.repo,
        manifest:{*:manifest,files:[for (entry in manifest.files) if (entry.id == result.selected and change != null)
            {*:entry,added:change.added,removed:change.removed,loaded:true} else entry]},
        document:document,document_index:0,selected:result.selected,projection:projection,
        top:if (was_same) min(session.top,max(0,projection.count - session.visible_rows)) else 0,
        pending:null,message:result.message,selection:if (was_same) session.selection else null,
        projected:null,query:"",matches:[]}
}
fn dom_point(node,offset) {
    let parent = if (node == null) null else if (dom.node_type(node) == 3) dom.parent_node(node) else node
    let span = if (parent == null) null else dom.closest(parent,".diff-text[data-row]")
    let text = if (span == null) null else dom.first_child(span);
    if (span == null or text == null or not dom.same_node(text,node)) null else
        rows.point(int(dom.get_attribute(span,"data-row")),units.codepoint_from_utf16(dom.node_value(text),offset,0,0))
}
fn native_selection(target) {
    let root = host(target)
    let native = if (root == null) null else dom.document_selection(dom.owner_document(root))
    let bounds = if (native == null) null else dom.selection_boundaries(native)
    let anchor = if (bounds == null) null else dom_point(bounds.anchor_node,bounds.anchor_offset)
    let head = if (bounds == null) null else dom_point(bounds.focus_node,bounds.focus_offset);
    if (anchor == null or head == null) null else rows.selection(anchor,head)
}
fn clamped_point(session,point) map^ {
    let row = max(session.top,min(session.top + session.visible_rows - 1,min(session.projection.count - 1,point.row)))
    let text = rows.at(session.document,current(session),session.projection,row)^.text;
    rows.point(row,if (point.row < row) 0 else if (point.row > row) len(text) else min(len(text),point.column))
}
pn project(target,session) map^ {
    if (session.selection == null or current(session) == null) { return session }
    let root = host(target)
    let anchor = clamped_point(session,session.selection.anchor)^
    let head = clamped_point(session,session.selection.head)^
    let an = dom.first_child(dom.query_selector(root,".diff-text[data-row='" ++ string(anchor.row) ++ "']"))
    let hn = dom.first_child(dom.query_selector(root,".diff-text[data-row='" ++ string(head.row) ++ "']"))
    if (an != null and hn != null) dom.set_base_and_extent(dom.document_selection(dom.owner_document(root)),
        an,units.utf16_from_codepoint(dom.node_value(an),anchor.column,0,0),
        hn,units.utf16_from_codepoint(dom.node_value(hn),head.column,0,0))
    return {*:session,projected:rows.selection(anchor,head)}
}
pn copy(target,session,side,selection_only) map^ {
    let text = rows.copy_text(session.document,current(session),session.projection,side,
        if (selection_only) session.selection else null)^
    let written = dom.clipboard_write(text);
    return {*:session,message:if (written) "Copied " ++ side ++ " text." else "Clipboard could not accept this text."}
}
fn searched(session) map^ {
    let matches = rows.search(session.document,current(session),session.projection,session.query)^;
    {*:session,matches:matches,match_index:0,top:if (len(matches) == 0) session.top else bounded(session,matches[0]),
        message:if (len(matches) == 0) "No matches in this file." else ""}
}

view <diff_viewer> state session: ~.session {
    p.page(tree(session,~.title)^,~.title)
}
on load(evt) {
    if (~.path != null and session.generation == 0) { session = refresh(evt.target,session,~.path)^ }
    else { session = request(evt.target,session) }
    return 'handled'
}
on resize(evt) {
    let surface = dom.query_selector(host(evt.target),".diff-surface")
    let height = dom.scroll_metrics(surface).client_height
    if (height > 0) { session = request(evt.target,{*:session,visible_rows:max(1,int(height div 22))}) }
    return 'handled'
}
on diff_ready(evt) {
    var next = session
    if (session.pending != null) {
        var complete = true
        var outcome = null
        // A short waiter yields to process tasks without blocking a UI turn on command completion (S13.3.2).
        wait(session.pending,timeout:1) ^ { if (^.code == 310) { complete = false } else { outcome = ^ } } ~ { outcome = ~ }
        if (complete) {
            if (outcome is error) { next = {*:next,pending:null,message:outcome.message} }
            else if (outcome.generation == session.generation) { next = installed(next,outcome) }
        }
    }
    if (next != session) {
        session = request(evt.target,next)
    } else if (session.pending != null) {
        // polling readiness does not change the desired DOM; publish only a completed request.
        dom.request_frame(host(evt.target),"diff_ready")
    }
    else if (session.selection != null) { project(evt.target,session)^ }
    return 'handled'
}
on render(evt) {
    if (session.selection != null) { project(evt.target,session)^ }
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target,"[data-diff-command]")
    if (button == null) { return 'pass' }
    let command = dom.get_attribute(button,"data-diff-command")
    if (command == "refresh" or command == "mode") { session = refresh(evt.target,session,~.path,
        if (command == "mode") dom.get_attribute(button,"data-diff-mode") else null)^ }
    else if (command == "file") { session = choose(evt.target,session,int(dom.get_attribute(button,"data-diff-id")))^ }
    else if (command == "files-prev" or command == "files-next") { session = {*:session,file_top:max(0,min(
        max(0,len(visible_files(session)) - session.file_rows),session.file_top + (if (command == "files-next") session.file_rows else -session.file_rows)))} }
    else if (command == "previous" or command == "next") { session = request(evt.target,moved(session,
        rows.hunk_step(session.projection,session.top,if (command == "next") 1 else -1))) }
    else if (command == "page-prev" or command == "page-next") { session = request(evt.target,moved(session,
        session.top + (if (command == "page-next") session.visible_rows else -session.visible_rows))) }
    else if (command == "copy-old" or command == "copy-new") { session = copy(evt.target,session,if (command == "copy-old") "old" else "new",false)^ }
    else if (command == "find") { session = request(evt.target,searched(session)^) }
    else if (command == "find-next" and len(session.matches) > 0) {
        let cursor = (session.match_index + 1)%len(session.matches)
        session = request(evt.target,{*:session,match_index:cursor,top:bounded(session,session.matches[cursor])})
    }
    return 'prevent-default'
}
on input(evt) {
    if (dom.closest(evt.target,".diff-filter") != null) { session = {*:session,filter:dom.get_state(evt.target,"value"),file_top:0} }
    else if (dom.closest(evt.target,".diff-query") != null) { session = {*:session,query:dom.get_state(evt.target,"value"),matches:[]} }
    return 'handled'
}
on wheel(evt) {
    if (dom.closest(evt.target,".diff-sidebar") != null) {
        session = {*:session,file_top:max(0,min(max(0,len(visible_files(session)) - session.file_rows),
            session.file_top + int(float(evt.deltaY or 0)/32.0)))}
    } else if (dom.closest(evt.target,".diff-surface") != null and abs(float(evt.deltaY or 0)) >= 1.0) {
        let delta = int(float(evt.deltaY)/22.0)
        session = request(evt.target,moved(session,session.top + (if (delta == 0) sign(float(evt.deltaY)) else delta)))
    } else { return 'pass' }
    return 'prevent-default'
}
on selectionchange(evt) {
    let picked = native_selection(evt.target)
    let projected = if (session.selection == null or current(session) == null) null else
        rows.selection(clamped_point(session,session.selection.anchor)^,clamped_point(session,session.selection.head)^)
    if (picked != null and picked != projected and picked != session.selection) {
        session = request(evt.target,{*:session,selection:picked,projected:picked})
    }
    return 'handled'
}
on mousedown(evt) {
    let gutter = dom.closest(evt.target,".diff-num")
    if (gutter == null) {
        if (evt.shiftKey == true and session.selection != null and dom.closest(evt.target,".diff-text") != null) {
            let boundary = dom.boundary_from_point(host(evt.target),int(evt.clientX),int(evt.clientY),null)
            let head = if (boundary == null) null else dom_point(boundary.node,boundary.offset)
            if (head != null) {
                // a projected DOM anchor can be offscreen; Shift-click extends from the model anchor.
                dom.mouse_focus(dom.query_selector(host(evt.target),".diff-surface"))
                let selected = rows.selection(session.selection.anchor,head)
                session = request(evt.target,{*:session,selection:selected})
                return 'prevent-default'
            }
        }
        return 'pass'
    }
    let row = int(dom.get_attribute(dom.closest(gutter,".diff-row"),"data-row"))
    let text = rows.at(session.document,current(session),session.projection,row)^.text
    let anchor = if (evt.shiftKey == true and session.selection != null) session.selection.anchor else rows.point(row,0)
    session = request(evt.target,{*:session,selection:rows.selection(anchor,rows.point(row,len(text)))})
    return 'prevent-default'
}
on keydown(evt) {
    if (dom.closest(evt.target,"input") != null) {
        if (evt.key == "Enter" and dom.closest(evt.target,".diff-query") != null) {
            session = request(evt.target,searched(session)^)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (current(session) == null) { return 'pass' }
    let primary = evt.ctrlKey == true or evt.metaKey == true
    if (primary and lower(string(evt.key)) == "c" and session.selection != null) {
        session = copy(evt.target,session,"unified",true)^
        return 'prevent-default'
    }
    if (primary and lower(string(evt.key)) == "a") {
        session = request(evt.target,{*:session,selection:rows.selected_all(session.document,current(session),session.projection)^})
        return 'prevent-default'
    }
    let delta = {ArrowDown:1,ArrowUp:-1,PageDown:session.visible_rows,PageUp:-session.visible_rows}[string(evt.key)]
    if (delta == null and not (evt.key in ["Home","End","ArrowLeft","ArrowRight"])) { return 'pass' }
    let selected = c.get(session,"selection",rows.selection(rows.point(session.top,0),rows.point(session.top,0)))
    let row = max(0,min(session.projection.count - 1,selected.head.row + c.get({value:delta},"value",0)))
    let text = rows.at(session.document,current(session),session.projection,row)^.text
    let column = if (evt.key == "Home") 0 else if (evt.key == "End") len(text)
        else max(0,min(len(text),selected.head.column + (if (evt.key == "ArrowLeft") -1 else if (evt.key == "ArrowRight") 1 else 0)))
    let head = rows.point(row,column)
    let anchor = if (evt.shiftKey == true) selected.anchor else head
    session = request(evt.target,{*:session,selection:rows.selection(anchor,head),
        top:if (row < session.top) row else if (row >= session.top + session.visible_rows) bounded(session,row - session.visible_rows + 1) else session.top})
    return 'prevent-default'
}
on closerequest(evt) {
    if (session.pending != null) cancel(session.pending)
    return 'pass'
}

pub fn viewer(document,options = null) element^ {
    let opts = c.options(options,["title","rows","file_rows"])^
    let valid = c.require(document.kind == "diff" and document.complete == true,"viewer requires a diff document")^
    let dimensions = check_dimensions(opts)^
    let title = c.get(opts,"title","Diff review");
    apply(<diff_viewer path:null,session:empty_session(document,opts),title:title>)
}
pub fn repository(path,options = null) element^ {
    let opts = c.options(options,["title","mode","base","target","timeout_ms","max_output_bytes","rows","file_rows"])^
    let dimensions = check_dimensions(opts)^
    let mode = c.text(c.get(opts,"mode","unstaged"))^
    let valid = c.require(mode in ["unstaged","staged","working","commits"],"unknown comparison mode")^
    let title = c.get(opts,"title","Repository diff")
    let empty = {kind:"diff",source:null,files:[],complete:true,added:0,removed:0}
    let session = {*:empty_session(empty,opts),comparison:{mode:mode,base:opts.base,target:opts.target},
        io_options:{timeout_ms:c.get(opts,"timeout_ms",30000),max_output_bytes:c.get(opts,"max_output_bytes",16777216)}};
    apply(<diff_viewer path:path,session:session,title:title>)
}
