// Async spelling checks paint source-mapped underlines without changing editor data (D7.2.5, S13.3.1v2).
import dom
import utf8: lambda.io.utf8
import text_offsets: lambda.dom.edit_history
import incremental: lambda.doc.spelling.incremental

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
// keep one deadline from the first dirty edit; later input only updates its queue.
pn wake(root,scope,seq,delay_ms) {
    if (delay_ms>0) sleep(delay_ms)^
    let owner = dom.get_element_by_id(root,identity(scope))
    if (owner!=null) dom.dispatch(owner,{type:"spelling_due",bubbles:false,detail:{seq:seq}})
}
pn run(root,scope,seq,request) {
    let folder = "temp/spelling"
    io.mkdir(folder)^
    let path = folder ++ "/" ++ string(sys.proc.self.pid#) ++ "-" ++ scope ++ "-" ++ string(seq) ++ ".mark"
    var failure = null
    var report = null
    output({units:request},path,'mark') ^ { failure = ^ }
    if (failure==null) {
        var process_result = null
        io.exec(sys.proc.self.argv#[0],["run","--no-log",sys.lambda.home# ++ "/package/doc/spelling/worker.ls"],
            {env:{LAMBDA_SPELLING_REQUEST:path},timeout_ms:30000,max_output_bytes:1048576}) ^ { failure = ^ } ~ { process_result = ~ }
        if (failure==null and (process_result.exit_code!=0 or process_result.timed_out or process_result.truncated)) {
            failure = error("Spelling check could not finish.")
        }
        if (failure==null) {
            utf8.decode(process_result.stdout) ^ { failure = ^ } ~ {
                parse(~,'json') ^ { failure = ^ } ~ { report = ~ }
            }
        }
    }
    // io.exec kills and joins its process on cancellation before removing its private request.
    if (exists(path)) io.delete(path) ^ { null }
    if (failure is error and failure.code==319) raise failure
    let owner = dom.get_element_by_id(root,identity(scope))
    if (owner!=null) dom.dispatch(owner,{type:"spelling_result",bubbles:false,
        detail:{seq:seq,report:report,error:failure.message}})
}
pn launch(root,scope,seq,units,delay_ms=0) {
    if (delay_ms>0) { return start(wake,[root,scope,seq,delay_ms],{owner:'document'}) }
    let request = take(incremental.pending(units),8)
    // immediate checks retain the actual worker handle for cancellation.
    return if (len(request)==0) null else start(run,[root,scope,seq,request],{owner:'document'})
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
        // DOM names require string literals; symbol literals miss these tag checks (S16.8.7).
        if ((skip!=null and object_id==skip) or dom.get_attribute(node,"spellcheck")=="false" or
            (lang!="en" and not starts_with(lang,"en-")) or
            tag in ["svg","math","script","style","canvas","audio","video"]) []
        else {
            let kids = [for (child in dom.child_nodes(node)) *canvas_pieces(child,skip,lang)]
            if (object_id!=null or tag in ["div","p","h1","h2","h3","h4","h5","h6","li","br"])
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

view <spelling_tools> state source:null, source_revision:null, paused:false, seq:0, painted_seq:0,
    task:null, report:null, failure:null, marks:[], frame:null, refresh_frame:null,
    units:[], remaining:0, job:0, checks:0, last_paragraphs:0, last_checked:0,
    skip:null, canvas_paused:false, canvas_opened:false {
    <div id:identity(~.scope),class:"spelling-layer",'aria-hidden':"true",
        'data-spelling-state':if (paused) "paused" else if (failure!=null) "error"
            else if (report!=null and remaining==0 and painted_seq==seq) "ready" else "pending",
        'data-spelling-count':string(len(report.issues or [])),'data-spelling-seq':string(seq),
        'data-spelling-checks':string(checks),'data-spelling-pending':string(remaining),
        'data-spelling-last-paragraphs':string(last_paragraphs),'data-spelling-last-checked':string(last_checked),
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
    let first_check = (source==null or detail.opened==true) and detail.document!=null and not detail.paused
    // edits coalesce into the running job; only document replacement or IME cancels it.
    if (detail.opened==true or detail.document==null or detail.paused) {
        let previous_task = task
        job = job+1
        task = null
        if (previous_task!=null) cancel(previous_task)
    }
    if (source==null or detail.opened==true or detail.document==null) {
        // an old document's pending frame must not block the replacement's first paint.
        if (frame!=null) dom.cancel_frame(evt.target,frame)
        frame = null
        units = []; report = null; marks = []
    }
    source = detail.document
    source_revision = detail.revision
    paused = detail.paused
    seq = seq+1
    failure = null
    units = incremental.refresh(source,units)^
    remaining = len(incremental.pending(units))
    if (source==null) { report = null; marks = [] }
    else if (frame==null) { frame = dom.request_frame(evt.target,"spelling_reposition_ready") }
    if (not paused and source!=null and remaining>0 and task==null) {
        job = job+1
        task = launch(dom.root_node(evt.target),~.scope,job,units,if (first_check) 0 else 350)
    }
    return 'handled'
}
on spelling_due(evt) {
    if (evt.event_phase!=2 or paused or not current(job,evt.detail)) { return 'pass' }
    task = launch(dom.root_node(evt.target),~.scope,job,units)
    return 'handled'
}
on spelling_result(evt) {
    if (evt.event_phase!=2 or paused or not current(job,evt.detail)) { return 'pass' }
    failure = evt.detail.error
    task = null
    if (evt.detail.report!=null) {
        units = incremental.accept(units,evt.detail.report.units)
        remaining = len(incremental.pending(units))
        checks = checks+1
        last_paragraphs = len(evt.detail.report.units)
        last_checked = evt.detail.report.checked
        if (frame==null) { frame = dom.request_frame(evt.target,"spelling_reposition_ready") }
        if (remaining>0) {
            job = job+1
            task = launch(dom.root_node(evt.target),~.scope,job,units)
        }
    }
    return 'handled'
}
on spelling_reposition(evt) {
    if (evt.event_phase!=2 or source==null) { return 'pass' }
    if (frame==null) { frame = dom.request_frame(evt.target,"spelling_reposition_ready") }
    return 'handled'
}
on spelling_reposition_ready(evt) {
    if (evt.event_phase!=2 or evt.detail!=frame or source==null) { return 'pass' }
    frame = null
    // current paragraph spans remap cached words after inline formatting and structural edits.
    report = incremental.report(units)
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
