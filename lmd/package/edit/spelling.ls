// Async spelling checks paint source-mapped underlines without changing editor data (D7.2.5, S13.3.1v2).
import dom
import utf8: lambda.io.utf8
import text_offsets: lambda.dom.edit_history

pub let css = "
 .spelling-layer { position:fixed;inset:0;z-index:60;pointer-events:none;overflow:hidden; }
 .spelling-underline { position:absolute;overflow:hidden;pointer-events:none; }
"

fn identity(scope) => "spelling-" ++ scope
fn host_id(scope,source) => if (scope=="word") "edit-surface" else if (scope=="slides-notes") "speaker-notes"
    else if (scope=="slides-canvas") "slide-render"
    else if (source is string) "canvas-text" else "canvas-rich-text"
pub fn current(seq,detail) => detail.seq==seq
pub pn update(node,scope,document,paused=false,revision=null,opened=false) {
    let owner = dom.get_element_by_id(dom.root_node(node),identity(scope))
    if (owner!=null) dom.dispatch(owner,{type:"spelling_update",bubbles:false,
        detail:{document:document,paused:paused,revision:revision,opened:opened}})
}
pub pn reposition(node,scope) {
    let owner = dom.get_element_by_id(dom.root_node(node),identity(scope))
    if (owner!=null) dom.dispatch(owner,{type:"spelling_reposition",bubbles:false})
}
pub pn update_canvas(node,skip=null,paused=false,opened=false) {
    let owner = dom.get_element_by_id(dom.root_node(node),identity("slides-canvas"))
    if (owner!=null) dom.dispatch(owner,{type:"spelling_canvas_refresh",bubbles:false,
        detail:{skip:skip,paused:paused,opened:opened}})
}
pn run(root,scope,seq,source,delay_ms) {
    if (delay_ms>0) sleep(delay_ms)^
    let folder = "temp/spelling"
    io.mkdir(folder)^
    let path = folder ++ "/" ++ string(sys.proc.self.pid#) ++ "-" ++ scope ++ "-" ++ string(seq) ++ ".mark"
    var failure = null
    var report = null
    output({document:source},path,'mark') ^ { failure = ^ }
    if (failure==null) {
        io.exec(sys.proc.self.argv#[0],["run","--no-log",sys.lambda.home# ++ "/package/doc/spelling/worker.ls"],
            {env:{LAMBDA_SPELLING_REQUEST:path},timeout_ms:30000,max_output_bytes:1048576}) ^ { failure = ^ } ~ {
            if (~.exit_code!=0 or ~.timed_out or ~.truncated) { failure = error("Spelling check could not finish.") }
            if (failure==null) { parse(utf8.decode(~.stdout)^,'json') ^ { failure = ^ } ~ { report = ~ } }
        }
    }
    // io.exec kills and joins its process on cancellation before removing its private request.
    if (exists(path)) io.delete(path) ^ { null }
    if (failure is error and failure.code==319) raise failure
    let owner = dom.get_element_by_id(root,identity(scope))
    if (owner!=null) dom.dispatch(owner,{type:"spelling_result",bubbles:false,
        detail:{seq:seq,report:report,error:failure.message}})
}

// A DOM Range uses the editor's render map, so each painted rectangle follows its source leaf.
pn source_rects(host,issue) {
    var found = []
    for (piece in issue.ranges or []) {
        let rects = dom.source_range_rects(host,{path:piece.path,offset:piece.start},
            {path:piece.path,offset:piece.end}) or []
        found = [*found,*rects]
    }
    return found
}

// The compiled slide canvas has no editor source map; retain DOM text leaves as the geometry anchors.
fn canvas_pieces(node,skip,language="en") {
    let kind = dom.node_type(node)
    if (kind==3) [{node:node,text:dom.node_value(node)}]
    else if (kind!=1) []
    else {
        let object_id = dom.get_attribute(node,"data-slide-object")
        let tag = lower(dom.node_name(node))
        let lang = lower(dom.get_attribute(node,"lang") or language)
        if ((skip!=null and object_id==skip) or dom.get_attribute(node,"spellcheck")=="false" or
            (lang!="en" and not starts_with(lang,"en-")) or
            tag in ['svg','math','script','style','canvas','audio','video']) []
        else {
            let kids = [for (child in dom.child_nodes(node)) *canvas_pieces(child,skip,lang)]
            if (object_id!=null or tag in ['div','p','h1','h2','h3','h4','h5','h6','li','br'])
                [{node:null,text:"\n"},*kids,{node:null,text:"\n"}]
            else kids
        }
    }
}
fn canvas_text(host,skip) => join([for (piece in canvas_pieces(host,skip)) piece.text],"")
pn canvas_snapshot(host,skip) {
    let pieces = canvas_pieces(host,skip)
    var offset = 0
    var spans = []
    for (piece in pieces) {
        push(spans,{*:piece,start:offset,end:offset+len(piece.text)})
        offset = offset+len(piece.text)
    }
    return {text:join([for (piece in pieces) piece.text],""),spans:spans}
}
pn canvas_rects(host,snapshot,issue) {
    var found = []
    for (span in snapshot.spans where span.node!=null and span.start<issue.end and span.end>issue.start) {
        let first = text_offsets.utf16_from_codepoint(span.text,max(issue.start,span.start)-span.start,0,0)
        let end_offset = text_offsets.utf16_from_codepoint(span.text,min(issue.end,span.end)-span.start,0,0)
        found = [*found,*(dom.text_node_range_rects(host,span.node,first,end_offset) or [])]
    }
    return found
}
pn measured(root,scope,source,report,skip) {
    if (report==null or source==null) { return [] }
    let host = dom.get_element_by_id(root,host_id(scope,source))
    if (host==null) { return [] }
    // selection geometry can extend beyond a scroller; chrome must stay outside its clip.
    let clip_host = if (scope=="word" or scope=="slides-canvas") dom.get_element_by_id(root,"stage") else host
    let clip = dom.bounding_box(clip_host)
    if (clip==null) { return [] }
    let snapshot = if (scope=="slides-canvas") canvas_snapshot(host,skip) else null
    if (snapshot!=null and snapshot.text!=source) { return [] }
    var found = []
    for (issue in report.issues) {
        let rects = if (snapshot!=null) canvas_rects(host,snapshot,issue)
            else if (source is string) dom.text_control_range_rects(host,
            text_offsets.utf16_from_codepoint(source,issue.start,0,0),
            text_offsets.utf16_from_codepoint(source,issue.end,0,0)) or []
            else source_rects(host,issue)
        found = [*found,*[for (rect in rects where rect.width>1 and rect.height>0 and
            rect.y+rect.height-3>=clip.y and rect.y+rect.height+1<=clip.y+clip.height and
            min(rect.x+rect.width,clip.x+clip.width)-max(rect.x,clip.x)>1)
            {word:issue.word,x:max(rect.x,clip.x),y:rect.y+rect.height-3,
                width:min(rect.x+rect.width,clip.x+clip.width)-max(rect.x,clip.x)}]]
    }
    return found
}
fn wave(width) => "M0 2 " ++ join([for (i in 0 to int(ceil(width/8))+1) {
    let x = i*8;
    "Q" ++ string(x+2) ++ " 0 " ++ string(x+4) ++ " 2 Q" ++
        string(x+6) ++ " 4 " ++ string(x+8) ++ " 2"
}]," ")

view <spelling_tools> state source:null, source_revision:null, paused:false, seq:0, checked_seq:0, painted_seq:0,
    task:null, report:null, failure:null, marks:[], frame:null, refresh_frame:null,
    skip:null, canvas_paused:false, canvas_opened:false {
    <div id:identity(~.scope),class:"spelling-layer",'aria-hidden':"true",
        'data-spelling-state':if (paused) "paused" else if (failure!=null) "error"
            else if (report!=null and painted_seq==seq) "ready" else "pending",
        'data-spelling-count':string(len(report.issues or [])),'data-spelling-seq':string(seq),
        *[for (mark in marks) <svg xmlns:"http://www.w3.org/2000/svg",class:"spelling-underline",
            'data-spelling-word':mark.word,style:"left:" ++ string(mark.x) ++ "px;top:" ++ string(mark.y) ++
                "px;width:" ++ string(mark.width) ++ "px;height:4px;",
            width:string(mark.width),height:"4",viewBox:"0 0 " ++ string(mark.width) ++ " 4",
            <path d:wave(mark.width),fill:"none",stroke:"#d63349",'stroke-width':"1.5">>]>
}
on spelling_update(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    let detail = evt.detail
    if (not detail.opened and source_revision==detail.revision and source==detail.document and paused==detail.paused) { return 'handled' }
    if (task!=null) cancel(task)
    if (frame!=null) dom.cancel_frame(evt.target,frame)
    let first_check = (source==null or detail.opened==true) and detail.document!=null and not detail.paused
    source = detail.document
    source_revision = detail.revision
    paused = detail.paused
    seq = seq+1
    failure = null
    frame = null
    // retain painted issues through edits, but never carry them into a different document.
    if (source==null or detail.opened==true) { report = null; marks = [] }
    // the first document check starts at load; subsequent edits still debounce off the input path.
    task = if (paused or source==null) null else start(run,
        [dom.root_node(evt.target),~.scope,seq,source,if (first_check) 0 else 350],{owner:'document'})
    return 'handled'
}
on spelling_result(evt) {
    if (evt.event_phase!=2 or paused or not current(seq,evt.detail)) { return 'pass' }
    failure = evt.detail.error
    task = null
    if (evt.detail.report!=null) {
        report = evt.detail.report
        checked_seq = seq
        frame = dom.request_frame(evt.target,"spelling_reposition_ready")
    }
    return 'handled'
}
on spelling_reposition(evt) {
    if (evt.event_phase!=2 or report==null or checked_seq!=seq) { return 'pass' }
    if (frame!=null) dom.cancel_frame(evt.target,frame)
    frame = dom.request_frame(evt.target,"spelling_reposition_ready")
    return 'handled'
}
on spelling_reposition_ready(evt) {
    if (evt.event_phase!=2 or evt.detail!=frame or report==null or checked_seq!=seq) { return 'pass' }
    frame = null
    marks = measured(dom.root_node(evt.target),~.scope,source,report,skip)
    painted_seq = seq
    return 'handled'
}
on spelling_canvas_refresh(evt) {
    if (evt.event_phase!=2 or ~.scope!="slides-canvas") { return 'pass' }
    skip = evt.detail.skip
    canvas_paused = evt.detail.paused
    canvas_opened = evt.detail.opened
    if (refresh_frame!=null) dom.cancel_frame(evt.target,refresh_frame)
    refresh_frame = dom.request_frame(evt.target,"spelling_canvas_ready")
    return 'handled'
}
on spelling_canvas_ready(evt) {
    if (evt.event_phase!=2 or evt.detail!=refresh_frame) { return 'pass' }
    refresh_frame = null
    let host = dom.get_element_by_id(dom.root_node(evt.target),"slide-render")
    let text = if (host==null or canvas_paused) null else canvas_text(host,skip)
    dom.dispatch(evt.target,{type:"spelling_update",bubbles:false,
        detail:{document:text,paused:canvas_paused,revision:null,opened:canvas_opened}})
    dom.dispatch(evt.target,{type:"spelling_reposition",bubbles:false})
    return 'handled'
}
// Stable source items keep task generations across editor redraws.
let word_tool = <spelling_tools scope:"word">
let notes_tool = <spelling_tools scope:"slides-notes">
let slides_tool = <spelling_tools scope:"slides-text">
let canvas_tool = <spelling_tools scope:"slides-canvas">
pub fn widget(scope) => apply(if (scope=="word") word_tool else if (scope=="slides-notes") notes_tool
    else if (scope=="slides-canvas") canvas_tool else slides_tool)
