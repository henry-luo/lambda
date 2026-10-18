// a preview is always computed from the press snapshot, so cancellation and one-step undo agree.
import model: .model
import geom: .geometry
pub fn connector_drag(mode) => contains(["from","to","bend","segment","connect"],mode)
// screen-space slop preserves click-to-connect when the pointer jitters, independent of zoom.
pub let connection_drag_slop = 4.0
pub fn cancel(st) => {*:st,doc:if (st.drag.before != null) st.drag.before else if (st.editing.before != null) st.editing.before else st.doc,
    pan:if (st.drag.pan != null) st.drag.pan else st.pan,
    selection:if (contains(["marquee","connect"],st.drag.mode)) st.drag.previous else st.selection,
    selected:if (contains(["marquee","connect"],st.drag.mode)) st.drag.primary else st.selected,
    arrow_menu:"",editing:null,drag:null,source:"",tool:"select",dialog:"",error:"",hover:""}
pub fn finish_edit(st) => if (st.editing == null) st else {*:model.commit_edit(st,st.editing.before,st.doc),editing:null}
pub fn begin_connection(st,input) {
    let size = model.page(st.doc)
    let targets = model.connection_targets(st.doc)
    let target = model.hit_targets(targets,input.point,6/st.zoom)
    let from = if (st.source != "") st.source else target
    let p = model.snap_point(st.doc,input.point,st.grid)
    let draft = model.connection(st,from,p,"",p);
    if (not geom.rect_contains({x:0,y:0,width:size.width,height:size.height},input.point)) st
    else if (len(model.objects(st.doc))>=model.object_limit(st.doc)) {*:st,status:"The diagram's object limit has been reached."}
    else {*:st,source:from,hover:"",drag:{mode:"connect",x:input.x,y:input.y,start:input.point,before:st.doc,
        pointer:input.pointer,targets:targets,draft:draft,from:from,clicked:target,source_before:st.source,
        previous:model.selected_ids(st),primary:st.selected,drawn:false,blocked:false},
        status:"Drag to draw a connector; release on a shape to attach."}
}
fn preview_connection(st,x,y,point) {
    let drag = st.drag
    let drawn = drag.drawn or geom.v2_dist({x:drag.x,y:drag.y},{x:x,y:y})>=connection_drag_slop
    let target = model.hit_targets(drag.targets,point,6/st.zoom)
    let blocked = target != "" and target == drag.from and model.attr(drag.before,"allow-loops") != true
    let drop = if (blocked) "" else target
    let next = if (drawn) model.edit_edge_document(drag.draft.doc,model.id(drag.draft.node),"to",
        model.snap_point(drag.before,point,st.grid),drop) else drag.before
    let preview = {*:st,doc:next,drag:{*:drag,drawn:drawn,blocked:blocked},hover:if (drawn) drop else ""};
    if (drawn) model.select_ids(preview,[model.id(drag.draft.node)]) else preview
}
pub fn finish_connection(st) {
    let drag = st.drag
    let base = {*:st,doc:drag.before,drag:null,hover:"",selected:drag.primary,selection:drag.previous,source:drag.source_before}
    let node = model.lookup(st.doc,model.id(drag.draft.node)).shape
    let ends = if (node != null) model.endpoints(st.doc,node) else null;
    if (not drag.drawn) model.connect(base,drag.clicked)
    else if (drag.blocked or ends == null or ends.from == ends.to) {*:base,source:"",status:"Choose a different endpoint to create a connector."}
    else {*:model.commit_edit(st,drag.before,st.doc),drag:null,source:"",hover:"",tool:"select",
        next_id:drag.draft.serial+1,status:"Connector added. Drag its endpoints or segments to edit."}
}
pub fn preview_drag(st,x,y,point,shift=false) {
    let drag = st.drag
    let dx = x-drag.x
    let dy = y-drag.y
    let snapped = model.snap_point(drag.before,point,st.grid)
    // the press snapshot fixes target contours throughout a connector gesture.
    let drop = if (not contains(["from","to"],drag.mode)) ""
        else if (drag.targets != null) model.hit_targets(drag.targets,point,6/st.zoom) else model.hit_shape(drag.before,point,6/st.zoom);
    if (drag.mode == "connect") preview_connection(st,x,y,point)
    else if ((if (connector_drag(drag.mode)) point == drag.start else dx == 0 and dy == 0) and
        not contains(["pan","marquee"],drag.mode)) {*:st,doc:drag.before}
    else if (drag.mode == "pan") {*:st,pan:{x:drag.pan.x+dx,y:drag.pan.y+dy}}
    else if (drag.mode == "marquee") (
        let ids = model.enclosed(st.doc,geom.rect_from_points(drag.start,point)),
        {*:model.select_ids(st,if (drag.additive) [*drag.previous,*ids] else ids),drag:{*:drag,current:point}})
    else if (contains(["from","to","bend"],drag.mode)) (
        let next = model.edit_edge_document(drag.before,drag.selected,drag.mode,snapped,drop),
        {*:st,hover:if (drag.mode != "bend" and model.attr(model.lookup(next,drag.selected).shape,drag.mode ++ "-shape") == drop) drop else "",doc:next})
    else if (drag.mode == "segment") {*:st,doc:model.edit_segment_document(drag.before,drag.selected,drag.segment,
        {x:model.snap(point.x-drag.start.x,st.grid),y:model.snap(point.y-drag.start.y,st.grid)})}
    else if (drag.mode == "rotate") {*:st,doc:model.rotate_document(drag.before,drag.selected,point,shift)}
    else if (drag.mode == "resize") {*:st,doc:model.resize_document(drag.before,drag.selected,dx/st.zoom,dy/st.zoom,st.grid)}
    else {*:st,doc:model.move_selection(drag.before,drag.ids,dx/st.zoom,dy/st.zoom,st.grid)}
}
fn begin_selection(st,input) {
    let p = input.point
    let ids = model.selected_ids(st)
    let node = if (len(ids)==1) model.lookup(st.doc,st.selected).shape else null
    let picked = model.handle_at(st.doc,node,p,st.zoom)
    let handle = picked.mode
    let target = if (handle != null) st.selected else model.hit(st.doc,p,6/st.zoom)
    let selected = if (target == "" or handle != null or contains(ids,target)) st else model.select_ids(st,[target])
    let mode = if (input.mode != null) input.mode else if (handle != null) handle else if (target == "") "marquee" else "move";
    if (input.shift == true and target != "" and handle == null and input.mode == null)
        model.select_ids(st,if (contains(ids,target)) [for (id in ids where id != target) id] else [*ids,target])
    else {*:selected,drag:{mode:mode,segment:picked.segment,x:input.x,y:input.y,start:p,current:p,before:st.doc,
        targets:if (contains(["from","to"],mode)) model.connection_targets(st.doc) else null,
        selected:selected.selected,ids:model.selected_ids(selected),pointer:input.pointer,
        previous:ids,primary:st.selected,additive:input.shift == true,pan:st.pan}}
}
pub fn begin(st,input) => if (st.tool == "connect") begin_connection(st,input) else begin_selection(st,input)
pub fn update(st,input) {
    if (input.kind == 'begin') begin(finish_edit(st),input)
    else if (input.kind == 'cancel') cancel(st)
    else if (input.kind == 'preview' and st.drag != null) preview_drag(st,input.x,input.y,input.point,input.shift == true)
    else if (input.kind == 'commit' and st.drag != null) (
        let next = preview_drag(st,input.x,input.y,input.point,input.shift == true),
        let committed = if (st.drag.mode == "connect") finish_connection(next)
            else if (contains(["pan","marquee"],st.drag.mode)) next else model.commit_edit(next,st.drag.before,next.doc),
        {*:committed,drag:null,hover:""})
    else st
}
