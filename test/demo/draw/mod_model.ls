// S1.4 / S12.1.3: document edits are values; the native shell owns interaction.
import doc: lambda.editor.mod_doc
import step: lambda.editor.mod_step
import tx: lambda.editor.mod_transaction
import history: lambda.editor.mod_history
import commands: lambda.editor.mod_drawing_commands
import geom: lambda.editor.mod_geom
import router: lambda.editor.mod_router

pub let width = 1120.0
pub let height = 760.0
pub let kinds = ["rect","ellipse","diamond"]
pub let colors = ["#e8eeff","#e4f4ed","#fff1d6","#fbe5eb","#eee8fa","#ffffff"]
pub let max_objects = 200
pub fn attr(node,key) => step.attrs_get(node.attrs,symbol(key))
pub fn id(node) => attr(node,"id")
pub fn objects(drawing) => drawing.content[0].content
pub fn lookup(drawing,target) => geom.find_shape_by_id(drawing,target)
pub fn box(node) => geom.get_shape_bbox(node)
pub fn with_attr(node,key,value) => {*:node,attrs:step.attrs_set(node.attrs,symbol(key),value)}
pub fn snap(value,enabled) => if (enabled) round(value/10)*10 else value
pub fn clamp(value,low,high) => min(high,max(low,value))
pub fn diamond_points(b) => [geom.v2(b.x+b.width/2,b.y),geom.v2(b.x+b.width,b.y+b.height/2),
    geom.v2(b.x+b.width/2,b.y+b.height),geom.v2(b.x,b.y+b.height/2)]
pub fn make_shape(target,kind,x,y,label,fill=colors[0]) {
    let base = commands.make_rect_shape(target,{x:x,y:y,width:160.0,height:80.0},{fill:fill,stroke:"#65718b",'stroke-width':1.5});
    with_attr(with_attr(with_attr(base,"kind",if (kind == "ellipse") 'ellipse' else 'rect'),"variant",kind),"label",label)
}
pub fn edge(target,from,to,label="",routing="orthogonal") {
    let base = commands.make_connector(target,{from_shape:from,to_shape:to,routing:symbol(routing)});
    with_attr(with_attr(base,"label",label),"stroke","#79849a")
}
pub fn drawing(items) => doc.node_attrs('drawing',[{name:'id',value:"diagram"}],
    [doc.node_attrs('layer',[{name:'id',value:"main"}],items)])
pub fn starter() => drawing([
    make_shape("n1","ellipse",100.0,300.0,"New idea",colors[1]),
    make_shape("n2","rect",350.0,300.0,"Sketch the flow"),
    make_shape("n3","diamond",610.0,300.0,"Ready?",colors[2]),
    make_shape("n4","rect",870.0,180.0,"Make it happen",colors[1]),
    make_shape("n5","rect",870.0,430.0,"Keep exploring",colors[4]),
    edge("e1","n1","n2"),edge("e2","n2","n3"),
    edge("e3","n3","n4","Yes"),edge("e4","n3","n5","Not yet")])
pub fn initial() => {doc:starter(),hist:history.history_new(),selected:"",next_id:6,
    tool:"select",source:"",drag:null,zoom:0.8,pan:{x:0.0,y:0.0},grid:true,
    dialog:"",status:"A little structure for your next big idea.",error:"",saved:starter()}
fn command_state(st) => {doc:st.doc,selection:null}
fn insert_object(st,node) => apply_tx(st,commands.cmd_insert_shape(command_state(st),{drawing_path:[],shape:node}),id(node))
pub fn apply_tx(st,change,selected) => if (change == null or change.doc_after == st.doc) {*:st,selected:selected}
    else {*:st,doc:change.doc_after,hist:history.history_push(st.hist,change),selected:selected,error:""}
pub fn commit_edit(st,before,after) {
    // one drag is one undo item, regardless of how many pointer moves it receives.
    let change = tx.tx_step(tx.tx_begin(before,null),step.step_replace([],0,1,after.content));
    apply_tx({*:st,doc:before},change,st.selected)
}
pub fn step_history(st,redo=false) {
    let result = if (redo) history.history_redo(st.hist,st.doc) else history.history_undo(st.hist,st.doc);
    {*:st,doc:result.doc,hist:result.hist,selected:"",source:"",drag:null}
}
fn next_free(drawing,n) => if (lookup(drawing,"n" ++ string(n)) == null and lookup(drawing,"e" ++ string(n)) == null) n else next_free(drawing,n+1)
pub fn add(st,kind,x,y) {
    if (len(objects(st.doc)) >= max_objects) {*:st,status:"This demo supports up to 200 objects per diagram."}
    else (
        let serial = next_free(st.doc,st.next_id),
        let target = "n" ++ string(serial),
        let node = make_shape(target,kind,clamp(snap(x,st.grid),0,width-160),clamp(snap(y,st.grid),0,height-80),
            if (kind == "diamond") "Decision" else if (kind == "ellipse") "Start / end" else "Process"),
        {*:insert_object(st,node),next_id:serial+1,tool:"select",source:"",status:"Shape added. Drag to move; use the inspector to edit."}
    )
}
pub fn connect(st,target) {
    if (lookup(st.doc,target).shape.tag != 'shape') st
    else if (st.source == "") {*:st,source:target,selected:target,status:"Now select the destination shape."}
    else if (st.source == target) {*:st,status:"Choose a different destination shape."}
    else if (len(objects(st.doc)) >= max_objects) {*:st,status:"This demo supports up to 200 objects per diagram."}
    else (
        let serial = next_free(st.doc,st.next_id),
        let edge_id = "e" ++ string(serial),
        let node = edge(edge_id,st.source,target),
        {*:insert_object(st,node),next_id:serial+1,source:"",tool:"select",status:"Connected. The arrow follows both shapes."}
    )
}
pub fn change(st,key,value) {
    let found = lookup(st.doc,st.selected);
    if (found == null) st else apply_tx(st,commands.cmd_set_shape_attr(command_state(st),found.path,symbol(key),value),st.selected)
}
pub fn transform(drawing,target,mode,dx,dy,grid) {
    let found = lookup(drawing,target)
    let b = box(found.shape);
    if (b == null) drawing else (
        let resized = mode == "resize",
        let x = if (resized) b.x else clamp(snap(b.x+dx,grid),0,width-b.width),
        let y = if (resized) b.y else clamp(snap(b.y+dy,grid),0,height-b.height),
        let w = if (resized) clamp(snap(b.width+dx,grid),60,width-b.x) else b.width,
        let h = if (resized) clamp(snap(b.height+dy,grid),40,height-b.y) else b.height,
        let change = commands.cmd_resize_shape({doc:drawing,selection:null},{shape_path:found.path,x:x,y:y,width:w,height:h}),
        if (change == null) drawing else change.doc_after
    )
}
pub fn nudge(st,dx,dy) => commit_edit(st,st.doc,transform(st.doc,st.selected,"move",dx,dy,false))
pub fn remove(st) {
    let selected = lookup(st.doc,st.selected)
    let paths = [for (i,node in objects(st.doc) where id(node) == st.selected or
        (selected.shape.tag == 'shape' and (attr(node,"from-shape") == st.selected or attr(node,"to-shape") == st.selected))) [0,i]];
    apply_tx(st,commands.cmd_delete_shapes(command_state(st),paths),"")
}
pub fn duplicate(st) {
    let found = lookup(st.doc,st.selected);
    if (found.shape.tag != 'shape') st
    else if (len(objects(st.doc)) >= max_objects) {*:st,status:"This demo supports up to 200 objects per diagram."}
    else (
        let serial = next_free(st.doc,st.next_id),
        let target = "n" ++ string(serial),
        let b = box(found.shape),
        let node = with_attr(with_attr(with_attr(found.shape,"id",target),"x",min(width-b.width,b.x+30)),"y",min(height-b.height,b.y+30)),
        {*:insert_object(st,node),next_id:serial+1}
    )
}
pub fn reorder(st,front) {
    let found = lookup(st.doc,st.selected);
    if (found == null) st else apply_tx(st,if (front) commands.cmd_bring_to_front(command_state(st),found.path)
        else commands.cmd_send_to_back(command_state(st),found.path),st.selected)
}
pub fn route(drawing,node) {
    let a = box(lookup(drawing,attr(node,"from-shape")).shape)
    let b = box(lookup(drawing,attr(node,"to-shape")).shape)
    let ac = geom.rect_center(a)
    let bc = geom.rect_center(b)
    let horizontal = abs(bc.x-ac.x) >= abs(bc.y-ac.y)
    let sign = if (horizontal) (if (bc.x >= ac.x) 1 else -1) else (if (bc.y >= ac.y) 1 else -1)
    let from = if (horizontal) geom.v2(ac.x+sign*a.width/2,ac.y) else geom.v2(ac.x,ac.y+sign*a.height/2)
    let to = if (horizontal) geom.v2(bc.x-sign*b.width/2,bc.y) else geom.v2(bc.x,bc.y-sign*b.height/2);
    if (attr(node,"routing") == 'straight') router.route_straight(from,to) else router.route_orthogonal(from,to,a,b)
}
pub fn hit(drawing,p,tolerance=6.0) {
    let shapes = [for (node in objects(drawing) where node.tag == 'shape' and
        (if (attr(node,"variant") == "diamond") geom.point_in_polygon(p,diamond_points(box(node))) else geom.hit_test_shape(node,p))) id(node)]
    let edges = [for (node in objects(drawing) where node.tag == 'connector' and router.is_point_on_route(p,route(drawing,node),tolerance)) id(node)];
    if (len(shapes)>0) shapes[last] else if (len(edges)>0) edges[last] else ""
}
pub fn archive(drawing) => {version:1,drawing:drawing}
fn scalar_attrs(node) => node is map and node.attrs is array and len(node.attrs)<=20 and
    all([for (a in node.attrs) a is map and a.name is string and (a.value is string or a.value is number)]) and
    len(unique([for (a in node.attrs) a.name])) == len(node.attrs)
fn restore_node(node) => doc.node_attrs(symbol(node.tag),[for (a in node.attrs)
    {name:symbol(a.name),value:if (contains(["kind","routing"],a.name)) symbol(a.value) else a.value}],[])
fn valid_shape(node) {
    let b = box(node);
    contains(kinds,attr(node,"variant")) and contains(['rect','ellipse'],attr(node,"kind")) and
    (attr(node,"variant") == "ellipse") == (attr(node,"kind") == 'ellipse') and
    all([for (a in node.attrs) contains(['id','kind','x','y','width','height','fill','stroke','stroke-width','variant','label'],a.name)]) and
    all([for (v in [b.x,b.y,b.width,b.height]) v is number and v >= 0 and v <= max(width,height)]) and
    b.width >= 60 and b.height >= 40 and b.x+b.width<=width and b.y+b.height<=height and
    attr(node,"label") is string and len(attr(node,"label"))<=200 and
    contains(colors,attr(node,"fill")) and attr(node,"stroke") == "#65718b" and attr(node,"stroke-width") == 1.5
}
pub fn restore(value) any^ {
    let layer = value.drawing.content[0]
    let items = layer.content;
    if (value.version != 1 or value.drawing.tag != "drawing" or len(value.drawing.content)!=1 or
        layer.tag != "layer" or not (items is array) or len(items)>max_objects or
        not all([for (node in items) scalar_attrs(node) and contains(["shape","connector"],node.tag) and node.content == []]))
        raise error("Draw: unsupported or invalid diagram file")
    else (
        let nodes = [for (node in items) restore_node(node)],
        let shapes = [for (node in nodes where node.tag == 'shape') id(node)],
        let valid = all([for (node in nodes) id(node) is string and len(id(node))>0 and len(id(node))<=40 and
            (if (node.tag == 'shape') valid_shape(node) else
                contains(shapes,attr(node,"from-shape")) and contains(shapes,attr(node,"to-shape")) and
                attr(node,"from-shape") != attr(node,"to-shape") and contains(['straight','orthogonal'],attr(node,"routing")) and
                attr(node,"label") is string and len(attr(node,"label"))<=200 and attr(node,"stroke") == "#79849a" and
                all([for (a in node.attrs) contains(['id','routing','from-shape','to-shape','label','stroke'],a.name)]))]),
        if (not valid or len(unique([for (node in nodes) id(node)])) != len(nodes)) raise error("Draw: invalid shapes or connector endpoints")
        else drawing(nodes)
    )
}
pub fn open_document(st,value) {
    // loaded IDs need not follow our prefix convention; advance until an unused pair exists.
    let serial = next_free(value,1);
    {*:st,doc:value,hist:history.history_new(),selected:"",source:"",drag:null,next_id:serial,saved:value,dialog:"",error:"",status:"Diagram opened."}
}
