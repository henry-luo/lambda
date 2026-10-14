// S1.4 / S12.1.3: document edits are values; the native shell owns interaction.
import doc: lambda.editor.mod_doc
import step: lambda.editor.mod_step
import tx: lambda.editor.mod_transaction
import history: lambda.editor.mod_history
import commands: lambda.editor.mod_drawing_commands
import geom: lambda.editor.mod_geom
import router: lambda.editor.mod_router
import shapes: .mod_shapes

pub let width = 1120.0
pub let height = 760.0
pub let curve_radius = 12.0
pub let kinds = shapes.kinds
pub let colors = ["#e8eeff","#e4f4ed","#fff1d6","#fbe5eb","#eee8fa","#ffffff"]
pub let max_objects = 200
pub let stroke_styles = ["solid","dashed","dotted","dash-dot","none"]
pub let patterns = ["solid","diagonal","crosshatch","dots","grid","horizontal"]
pub let arrows = ["none","triangle","open","diamond","circle","bar"]
pub let ports = ["auto","north","east","south","west"]
pub let routings = ["orthogonal","straight","curved"]
pub let common_defaults = {'stroke-style':"solid",'stroke-width':1.5,opacity:1.0,
    'font-size':14.0,'font-weight':"normal",'text-align':"center",'text-color':"#29354c"}
pub let shape_defaults = {*:common_defaults,rotation:0.0,'flip-x':false,'flip-y':false,stroke:"#65718b",fill:colors[0],pattern:"solid",'pattern-color':"#9aa7bd"}
pub let edge_defaults = {*:common_defaults,stroke:"#79849a",'stroke-width':1.7,
    'start-arrow':"none",'end-arrow':"triangle",'from-port':"auto",'to-port':"auto",
    'bend-x':-1.0,'bend-y':-1.0,'label-position':0.5,'from-x':0.0,'from-y':0.0,'to-x':0.0,'to-y':0.0}
pub let numeric_fields = {'stroke-width':[0,12],opacity:[0.1,1],'font-size':[8,48],
    rotation:[-360,360],'from-x':[0,width],'from-y':[0,height],'to-x':[0,width],'to-y':[0,height],'label-position':[0,1],'bend-x':[-1,width],'bend-y':[-1,height],x:[0,width],y:[0,height],width:[60,width],height:[40,height]}
pub fn editable_keys(node) => ["label","stroke",*[for (key,value in common_defaults) string(key)],
    *(if (node.tag == 'shape') ["variant","fill","pattern","pattern-color","x","y","width","height","rotation"] else
        ["routing","from-shape","to-shape","from-port","to-port","start-arrow","end-arrow","label-position"])]
pub fn attr(node,key) {
    let value = step.attrs_get(node.attrs,symbol(key));
    if (value != null) value else (if (node.tag == 'shape') shape_defaults[key] else edge_defaults[key])
}
pub fn id(node) => attr(node,"id")
pub fn objects(drawing) => drawing.content[0].content
pub fn lookup(drawing,target) => geom.find_shape_by_id(drawing,target)
pub fn box(node) => geom.get_shape_bbox(node)
pub fn with_attr(node,key,value) => {*:node,attrs:step.attrs_set(node.attrs,symbol(key),value)}
pub fn with_attrs(node,values) {
    // preserve attribute order so unchanged input never creates an undo entry.
    let existing = [for (a in node.attrs) string(a.name)];
    {*:node,attrs:[*[for (a in node.attrs) if (values[string(a.name)] != null) {*:a,value:values[string(a.name)]} else a],
        *[for (key,value in values where not contains(existing,string(key))) {name:symbol(key),value:value}]]}
}
pub fn selected_ids(st) => if (contains(st.selection,st.selected)) st.selection else if (st.selected != "") [st.selected] else []
pub fn select_ids(st,ids) {
    let valid = unique([for (target in ids where lookup(st.doc,target) != null) target]);
    {*:st,selection:valid,selected:if (len(valid)>0) valid[last] else ""}
}
pub fn turn_point(node,p,flip=true) {
    let c = geom.rect_center(box(node))
    let local = if (flip) {x:c.x+(p.x-c.x)*(if (attr(node,"flip-x")) -1 else 1),y:c.y+(p.y-c.y)*(if (attr(node,"flip-y")) -1 else 1)} else p;
    geom.rotate_point(local,c,attr(node,"rotation"))
}
pub fn contour(node) => [for (p in shapes.outline(attr(node,"variant"),box(node))) turn_point(node,p)]
fn bounds(points) {
    let xs = [for (p in points) p.x]
    let ys = [for (p in points) p.y];
    {x:min(xs),y:min(ys),width:max(xs)-min(xs),height:max(ys)-min(ys)}
}
pub fn world_box(node) => if (node.tag == 'shape') bounds(contour(node)) else null
pub fn shape_handles(node,zoom=1.0) {
    let b = box(node);
    {resize:turn_point(node,{x:b.x+b.width,y:b.y+b.height},false),
        rotate:turn_point(node,{x:b.x+b.width/2,y:b.y-28/zoom},false)}
}
pub fn selection_box(drawing,ids) {
    let points = [for (node in objects(drawing) where contains(ids,id(node)))
        *if (node.tag == 'shape') contour(node) else visible_route(drawing,node)];
    if (len(points)>0) bounds(points) else null
}
pub fn enclosed(drawing,rectangle) => [for (node in objects(drawing) where
    all([for (p in (if (node.tag == 'shape') contour(node) else visible_route(drawing,node))) geom.rect_contains(rectangle,p)])) id(node)]
pub fn snap(value,enabled) => if (enabled) round(value/10)*10 else value
pub fn clamp(value,low,high) => min(high,max(low,value))
pub fn make_shape(target,kind,x,y,label,fill=colors[0]) {
    let base = commands.make_rect_shape(target,{x:x,y:y,width:160.0,height:80.0},{fill:fill,stroke:"#65718b",'stroke-width':1.5});
    with_attrs(base,{*:shape_defaults,kind:if (kind == "ellipse") 'ellipse' else 'rect',variant:kind,label:label,
        fill:if (kind == "text") "none" else fill,'stroke-style':if (kind == "text") "none" else "solid"})
}
pub fn edge(target,from,to,label="",routing="orthogonal") {
    let base = commands.make_connector(target,{from_shape:from,to_shape:to,routing:symbol(routing)});
    with_attrs(base,{*:edge_defaults,label:label})
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
pub fn showcase() => drawing([
    *[for (i,kind in kinds) with_attrs(make_shape("n" ++ string(i+1),kind,60.0+(i%4)*250,100.0+floor(i/4)*220,shapes.title(kind),colors[i%len(colors)]),
        {pattern:if (kind == "text") "solid" else patterns[i%len(patterns)],'stroke-style':if (kind == "text") "none" else stroke_styles[i%4],
            'stroke-width':1.5+(i%3),'font-weight':"bold",'font-size':16.0})],
    *[for (i in 1 to 11 where i%4 != 0) with_attrs(edge("e" ++ string(i),"n" ++ string(i),"n" ++ string(i+1)),
        {'start-arrow':arrows[(i-1)%len(arrows)],'end-arrow':arrows[i%len(arrows)],'stroke-width':2.0})]])
pub fn initial() => {doc:starter(),hist:history.history_new(),selected:"",selection:[],editing:null,next_id:6,
    tool:"select",source:"",drag:null,zoom:0.8,pan:{x:0.0,y:0.0},grid:true,
    dialog:"",status:"A little structure for your next big idea.",error:"",saved:starter(),clipboard:null,hover:""}
fn command_state(st) => {doc:st.doc,selection:null}
fn insert_object(st,node) => apply_tx(st,commands.cmd_insert_shape(command_state(st),{drawing_path:[],shape:node}),id(node))
pub fn apply_tx(st,change,selected) => if (change == null or change.doc_after == st.doc) {*:select_ids(st,if (selected == st.selected) selected_ids(st) else [selected])}
    else select_ids({*:st,doc:change.doc_after,hist:history.history_push(st.hist,change),error:""},if (selected == st.selected) selected_ids(st) else [selected])
pub fn commit_edit(st,before,after) {
    // one drag is one undo item, regardless of how many pointer moves it receives.
    let change = tx.tx_step(tx.tx_begin(before,null),step.step_replace([],0,1,after.content));
    apply_tx({*:st,doc:before},change,st.selected)
}
pub fn step_history(st,redo=false) {
    let result = if (redo) history.history_redo(st.hist,st.doc) else history.history_undo(st.hist,st.doc);
    {*:st,doc:result.doc,hist:result.hist,selected:"",selection:[],editing:null,source:"",drag:null}
}
fn next_free(drawing,n) => if (lookup(drawing,"n" ++ string(n)) == null and lookup(drawing,"e" ++ string(n)) == null) n else next_free(drawing,n+1)
pub fn add(st,kind,x,y) {
    if (len(objects(st.doc)) >= max_objects) {*:st,status:"This demo supports up to 200 objects per diagram."}
    else (
        let serial = next_free(st.doc,st.next_id),
        let target = "n" ++ string(serial),
        let node = make_shape(target,kind,clamp(snap(x,st.grid),0,width-160),clamp(snap(y,st.grid),0,height-80),
            shapes.title(kind)),
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
pub fn preview_properties(st,values) any^ {
    let ids = selected_ids(st)
    let shape_ids = [for (node in objects(st.doc) where node.tag == 'shape') id(node)]
    let nodes = [for (node in objects(st.doc)) if (not contains(ids,id(node))) node else (
        let allowed = map([for (key,value in values where
            contains(["id","label",*[for (a in node.attrs) string(a.name)]],string(key)) and (id(node) == st.selected or
            (not contains(["label","x","y","width","height","from-shape","to-shape","from-port","to-port","bend-x","bend-y","label-position"],string(key)) and
            (node.tag == 'shape' or shape_defaults[string(key)] == null or common_defaults[string(key)] != null or string(key) == "stroke")))) *[key,value]]),
        let detached = map([for (side in ["from","to"] where node.tag == 'connector' and allowed[side ++ "-shape"] == "" and attr(node,side ++ "-shape") != "")
            (let p = endpoints(st.doc,node)[side], *[side ++ "-x",p.x,side ++ "-y",p.y])]),
        let requested = with_attrs(node,{*:detached,*:allowed}),
        if (node.tag == 'shape') with_attr(requested,"kind",if (attr(requested,"variant") == "ellipse") 'ellipse' else 'rect') else requested)];
    if (not all([for (node in nodes) valid_node(node,shape_ids)])) raise error("Draw: invalid properties; keep shapes on the page (minimum 60 × 40), use hex colors and supported styles")
    else {*:st,doc:drawing(nodes),error:""}
}
pub fn edit_properties(st,values) any^ {
    let preview = preview_properties(st,values)^;
    commit_edit(preview,st.doc,preview.doc)
}
pub fn reverse_edge(st) any^ {
    let node = lookup(st.doc,st.selected).shape;
    if (node.tag != 'connector') st else edit_properties(st,{'from-shape':attr(node,"to-shape"),'to-shape':attr(node,"from-shape"),
        'from-port':attr(node,"to-port"),'to-port':attr(node,"from-port"),
        'from-x':attr(node,"to-x"),'from-y':attr(node,"to-y"),'to-x':attr(node,"from-x"),'to-y':attr(node,"from-y")})^
}
pub fn copy_style(st) {
    let node = lookup(st.doc,st.selected).shape;
    if (node == null) st else {*:st,clipboard:{tag:node.tag,values:map([for (key,value in
        (if (node.tag == 'shape') shape_defaults else edge_defaults) where not contains(["bend-x","bend-y","from-port","to-port","from-x","from-y","to-x","to-y","label-position","rotation","flip-x","flip-y"],string(key))) *[key,attr(node,string(key))]])},
        status:"Style copied. Select another object and paste its style."}
}
pub fn paste_style(st) any^ {
    let node = lookup(st.doc,st.selected).shape;
    if (node == null or st.clipboard == null) st else edit_properties(st,
        map([for (key,value in st.clipboard.values where st.clipboard.tag == node.tag or common_defaults[string(key)] != null or string(key) == "stroke") *[key,value]]))^
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
fn translate(node,dx,dy,ids) {
    if (node.tag == 'shape') with_attrs(node,{x:attr(node,"x")+dx,y:attr(node,"y")+dy})
    else (
        let free = map([for (side in ["from","to"] where attr(node,side ++ "-shape") == "")
            *[side ++ "-x",attr(node,side ++ "-x")+dx,side ++ "-y",attr(node,side ++ "-y")+dy]]),
        let bend = if (attr(node,"bend-x")>=0 and (contains(ids,id(node)) or
            (contains(ids,attr(node,"from-shape")) and contains(ids,attr(node,"to-shape")))))
            {'bend-x':attr(node,"bend-x")+dx,'bend-y':attr(node,"bend-y")+dy} else {},
        with_attrs(node,{*:free,*:bend}))
}
pub fn move_selection(drawing,ids,dx,dy,grid) {
    let selected = [for (node in objects(drawing) where contains(ids,id(node))) node]
    let authored = [for (node in selected where node.tag == 'shape') box(node)]
    let visual = selection_box(drawing,ids)
    let low_x = max([0-visual.x,*[for (b in authored) 0-b.x]])
    let high_x = min([width-visual.x-visual.width,*[for (b in authored) width-b.x-b.width]])
    let low_y = max([0-visual.y,*[for (b in authored) 0-b.y]])
    let high_y = min([height-visual.y-visual.height,*[for (b in authored) height-b.y-b.height]])
    let x = clamp(snap(dx,grid),low_x,high_x)
    let y = clamp(snap(dy,grid),low_y,high_y);
    if (len(selected)==0) drawing else drawing_with(drawing,[for (node in objects(drawing))
        if (contains(ids,id(node)) or (node.tag == 'connector' and contains(ids,attr(node,"from-shape")) and contains(ids,attr(node,"to-shape")))) translate(node,x,y,ids) else node])
}
fn drawing_with(value,nodes) => {*:value,content:[{*:value.content[0],content:nodes}]}
pub fn nudge(st,dx,dy) => commit_edit(st,st.doc,move_selection(st.doc,selected_ids(st),dx,dy,false))
pub fn remove(st) {
    let ids = selected_ids(st)
    let paths = [for (i,node in objects(st.doc) where contains(ids,id(node)) or
        (node.tag == 'connector' and (contains(ids,attr(node,"from-shape")) or contains(ids,attr(node,"to-shape"))))) [0,i]];
    apply_tx(st,commands.cmd_delete_shapes(command_state(st),paths),"")
}
pub fn align(st,mode) any^ {
    let ids = selected_ids(st)
    let nodes = [for (node in objects(st.doc) where node.tag == 'shape' and contains(ids,id(node))) node]
    let horizontal = contains(["left","center","right","distribute-x"],mode)
    let axis = if (horizontal) "x" else "y"
    let size = if (horizontal) "width" else "height"
    let all_bounds = selection_box(st.doc,[for (node in nodes) id(node)])
    let ordered = sort(nodes,(node) => world_box(node)[axis])
    let distributing = starts_with(mode,"distribute-")
    let gap = if (len(nodes)>1) (all_bounds[size]-sum([for (node in nodes) world_box(node)[size]]))/(len(nodes)-1) else 0
    let positions = map([for (i,node in ordered) (let b = world_box(node),
        let desired = if (distributing) all_bounds[axis]+sum([for (j in 0 to i-1) world_box(ordered[j])[size]])+gap*i
            else all_bounds[axis]+(if (contains(["right","bottom"],mode)) all_bounds[size]-b[size]
                else if (contains(["center","middle"],mode)) (all_bounds[size]-b[size])/2 else 0),
        *[id(node),desired-b[axis]])])
    let changed = drawing_with(st.doc,[for (node in objects(st.doc)) if (positions[id(node)] == null) node
        else translate(node,if (horizontal) positions[id(node)] else 0,if (horizontal) 0 else positions[id(node)],ids)]);
    if (len(nodes)<2 or (distributing and len(nodes)<3)) st
    else if (not all([for (node in objects(changed) where node.tag == 'shape') valid_shape(node)])) raise error("Draw: alignment would move a shape outside the page")
    else commit_edit(st,st.doc,changed)
}
pub fn orient(st,mode) {
    let ids = selected_ids(st)
    let changed = drawing_with(st.doc,[for (node in objects(st.doc)) if (node.tag != 'shape' or not contains(ids,id(node))) node
        else if (mode == "rotate") with_attr(node,"rotation",(attr(node,"rotation")+90)%360)
        else with_attr(node,mode,not attr(node,mode))]);
    commit_edit(st,st.doc,changed)
}
pub fn rotate_document(drawing,target,p,snapping=false) {
    let found = lookup(drawing,target)
    let c = geom.rect_center(box(found.shape))
    let angle = math.atan2(p.y-c.y,p.x-c.x)*180/math.pi+90;
    if (found.shape.tag != 'shape') drawing else doc.replace_node_at(drawing,found.path,
        with_attr(found.shape,"rotation",if (snapping) round(angle/15)*15 else round(angle)))
}
pub fn resize_document(drawing,target,dx,dy,grid) {
    let found = lookup(drawing,target)
    let node = found.shape
    let b = box(node)
    let local = geom.rotate_point({x:dx,y:dy},{x:0,y:0},0-attr(node,"rotation"))
    let w = max(60,snap(b.width+local.x,grid))
    let h = max(40,snap(b.height+local.y,grid))
    let fixed = turn_point(node,{x:b.x,y:b.y},false)
    let offset = geom.rotate_point({x:w/2,y:h/2},{x:0,y:0},attr(node,"rotation"))
    let resized = with_attrs(node,{x:fixed.x+offset.x-w/2,y:fixed.y+offset.y-h/2,width:w,height:h});
    if (valid_shape(resized)) doc.replace_node_at(drawing,found.path,resized) else drawing
}
fn reserve_ids(value,serial,count) {
    let available = next_free(value,serial);
    if (count == 0) [] else [available,*reserve_ids(value,available+1,count-1)]
}
pub fn duplicate(st) {
    let ids = selected_ids(st)
    let picked = [for (node in objects(st.doc) where contains(ids,id(node)) or
        (node.tag == 'connector' and contains(ids,attr(node,"from-shape")) and contains(ids,attr(node,"to-shape")))) node]
    let serials = reserve_ids(st.doc,st.next_id,len(picked))
    let names = map([for (i,node in picked) *[id(node),(if (node.tag == 'shape') "n" else "e") ++ string(serials[i])]])
    let clones = [for (node in picked) (let endpoints = if (node.tag == 'connector') map([for (side in ["from","to"] where names[attr(node,side ++ "-shape")] != null)
            *[side ++ "-shape",names[attr(node,side ++ "-shape")]]]) else {},
        let bend = if (len(picked)==1 and node.tag == 'connector') point_on_route(route(st.doc,node),0.5) else null,
        with_attrs(node,{id:names[id(node)],*:endpoints,*:(if (bend != null) {'bend-x':bend.x,'bend-y':bend.y} else {})}))]
    let copy_ids = [for (node in clones) id(node)]
    let inserted = drawing_with(st.doc,[*objects(st.doc),*clones])
    let moved = move_selection(inserted,copy_ids,30,30,false);
    if (len(picked)==0) st
    else if (len(objects(st.doc))+len(picked)>max_objects) {*:st,status:"This demo supports up to 200 objects per diagram."}
    else {*:select_ids(commit_edit(st,st.doc,moved),copy_ids),next_id:serials[last]+1}
}
pub fn reorder(st,front) {
    let found = lookup(st.doc,st.selected);
    if (found == null) st else apply_tx(st,if (front) commands.cmd_bring_to_front(command_state(st),found.path)
        else commands.cmd_send_to_back(command_state(st),found.path),st.selected)
}
fn port_direction(port,direction) {
    let explicit = {north:{x:0,y:-1},east:{x:1,y:0},south:{x:0,y:1},west:{x:-1,y:0}};
    if (explicit[port] != null) explicit[port] else direction
}
fn attachment(node,direction,port) {
    let local = geom.rotate_point(direction,{x:0,y:0},0-attr(node,"rotation"))
    let flipped = {x:local.x*(if (attr(node,"flip-x")) -1 else 1),y:local.y*(if (attr(node,"flip-y")) -1 else 1)};
    turn_point(node,shapes.anchor(attr(node,"variant"),box(node),port_direction(port,flipped)))
}
pub fn endpoints(drawing,node) {
    let from_node = lookup(drawing,attr(node,"from-shape")).shape
    let to_node = lookup(drawing,attr(node,"to-shape")).shape
    let a = world_box(from_node)
    let b = world_box(to_node)
    let ac = if (a != null) geom.rect_center(box(from_node)) else {x:attr(node,"from-x"),y:attr(node,"from-y")}
    let bc = if (b != null) geom.rect_center(box(to_node)) else {x:attr(node,"to-x"),y:attr(node,"to-y")}
    let horizontal = abs(bc.x-ac.x) >= abs(bc.y-ac.y)
    let sign = if (horizontal) (if (bc.x >= ac.x) 1 else -1) else (if (bc.y >= ac.y) 1 else -1)
    let direction = if (attr(node,"routing") == 'straight' and ac != bc) geom.v2_sub(bc,ac)
        else if (horizontal) geom.v2(sign,0) else geom.v2(0,sign);
    {from:if (a == null) ac else attachment(from_node,direction,attr(node,"from-port")),
        to:if (b == null) bc else attachment(to_node,geom.v2_scale(direction,-1),attr(node,"to-port")),a:a,b:b}
}
pub fn route(drawing,node) {
    let ends = endpoints(drawing,node)
    let bend = geom.v2(attr(node,"bend-x"),attr(node,"bend-y"))
    let points = if (bend.x>=0 and bend.y>=0)
        (if (attr(node,"routing") == 'straight') [ends.from,bend,ends.to] else
            [ends.from,geom.v2(bend.x,ends.from.y),bend,geom.v2(ends.to.x,bend.y),ends.to])
        else if (attr(node,"routing") == 'straight') router.route_straight(ends.from,ends.to)
        else router.route_orthogonal(ends.from,ends.to,ends.a,ends.b)
    let compact = [for (i,p in points where i == 0 or p != points[i-1]) p];
    if (len(compact)<2) [ends.from,ends.to] else compact
}
pub fn point_on_route(points,fraction) {
    let lengths = [for (i in 1 to len(points)-1) geom.v2_dist(points[i-1],points[i])]
    let distance = sum(lengths)*fraction
    let candidates = [for (i,span in lengths where span>0 and sum(slice(lengths,0,i+1))>=distance) i]
    let i = candidates[0];
    if (i == null) points[0] else geom.v2_add(points[i],geom.v2_scale(geom.v2_sub(points[i+1],points[i]),
        clamp((distance-sum(slice(lengths,0,i)))/lengths[i],0,1)))
}
pub fn visible_route(drawing,node) {
    let points = route(drawing,node);
    if (attr(node,"routing") == 'curved') router.route_to_curved_points(points,curve_radius) else points
}
pub fn edge_handles(drawing,node) {
    let points = route(drawing,node);
    {from:points[0],to:points[last],bend:if (attr(node,"bend-x")>=0) geom.v2(attr(node,"bend-x"),attr(node,"bend-y")) else point_on_route(points,0.5)}
}
pub fn edit_edge_document(drawing,target,mode,p,drop="") {
    let found = lookup(drawing,target)
    let node = found.shape
    let values = if (mode == "bend") {'bend-x':clamp(p.x,0,width),'bend-y':clamp(p.y,0,height)}
        else {[mode ++ "-shape"]:drop,[mode ++ "-port"]:"auto",[mode ++ "-x"]:clamp(p.x,0,width),[mode ++ "-y"]:clamp(p.y,0,height)}
    let opposite = attr(node,if (mode == "from") "to-shape" else "from-shape");
    if (node.tag != 'connector' or (mode != "bend" and ((drop != "" and lookup(drawing,drop).shape.tag != 'shape') or (drop != "" and drop == opposite)))) drawing
    else doc.replace_node_at(drawing,found.path,with_attrs(node,values))
}
pub fn hit_shape(drawing,p) {
    let hits = [for (node in objects(drawing) where node.tag == 'shape' and
        geom.point_in_polygon(p,contour(node))) id(node)];
    if (len(hits)>0) hits[last] else ""
}
pub fn hit(drawing,p,tolerance=6.0) {
    let shape = hit_shape(drawing,p)
    let edges = [for (node in objects(drawing) where node.tag == 'connector' and router.is_point_on_route(p,visible_route(drawing,node),tolerance)) id(node)];
    if (shape != "") shape else if (len(edges)>0) edges[last] else ""
}
pub fn archive(drawing) => {version:3,drawing:drawing}
fn scalar_attrs(node) => node is map and node.attrs is array and len(node.attrs)<=48 and
    all([for (a in node.attrs) a is map and a.name is string and (a.value is string or a.value is number or a.value is bool)]) and
    len(unique([for (a in node.attrs) a.name])) == len(node.attrs)
fn restore_node(node) => doc.node_attrs(symbol(node.tag),[for (a in node.attrs)
    {name:symbol(a.name),value:if (contains(["kind","routing"],a.name)) symbol(a.value) else a.value}],[])
pub fn valid_color(value) {
    if (not (value is string) or len(value)!=7 or not starts_with(value,"#")) false
    else (
        let digits = [for (i in 1 to 6) (let digit = lower(slice(value,i,i+1)), contains("0123456789abcdef",digit))],
        all(digits))
}
fn valid_number(node,key) {
    let value = attr(node,key)
    let interval = numeric_fields[key];
    value is number and value>=interval[0] and value<=interval[1]
}
fn valid_shape(node) {
    let b = box(node);
    contains(kinds,attr(node,"variant")) and contains(['rect','ellipse'],attr(node,"kind")) and
    (attr(node,"variant") == "ellipse") == (attr(node,"kind") == 'ellipse') and
    all([for (a in node.attrs) contains(["id","kind","x","y","width","height","variant","label",*[for (key,value in shape_defaults) string(key)]],string(a.name))]) and
    all([for (v in [b.x,b.y,b.width,b.height]) v is number and v >= 0 and v <= max(width,height)]) and
    b.width >= 60 and b.height >= 40 and b.x+b.width<=width and b.y+b.height<=height and
    (attr(node,"fill") == "none" or valid_color(attr(node,"fill"))) and valid_color(attr(node,"pattern-color")) and
    contains(patterns,attr(node,"pattern")) and valid_number(node,"rotation") and attr(node,"flip-x") is bool and attr(node,"flip-y") is bool
}
pub fn valid_node(node,shape_ids) => id(node) is string and len(id(node))>0 and len(id(node))<=40 and
    attr(node,"label") is string and len(attr(node,"label"))<=200 and valid_color(attr(node,"stroke")) and
    valid_color(attr(node,"text-color")) and contains(stroke_styles,attr(node,"stroke-style")) and
    contains(["normal","bold"],attr(node,"font-weight")) and contains(["left","center","right"],attr(node,"text-align")) and
    all([for (key in ["stroke-width","opacity","font-size"]) valid_number(node,key)]) and
    (if (node.tag == 'shape') valid_shape(node) else node.tag == 'connector' and
        (attr(node,"from-shape") == "" or contains(shape_ids,attr(node,"from-shape"))) and
        (attr(node,"to-shape") == "" or contains(shape_ids,attr(node,"to-shape"))) and
        (attr(node,"from-shape") == "" or attr(node,"from-shape") != attr(node,"to-shape")) and contains(routings,string(attr(node,"routing"))) and
        contains(arrows,attr(node,"start-arrow")) and contains(arrows,attr(node,"end-arrow")) and
        contains(ports,attr(node,"from-port")) and contains(ports,attr(node,"to-port")) and
        all([for (key in ["bend-x","bend-y","label-position","from-x","from-y","to-x","to-y"]) valid_number(node,key)]) and
        ((attr(node,"bend-x") == -1 and attr(node,"bend-y") == -1) or (attr(node,"bend-x")>=0 and attr(node,"bend-y")>=0)) and
        all([for (a in node.attrs) contains(["id","routing","from-shape","to-shape","label",*[for (key,value in edge_defaults) string(key)]],string(a.name))]))
fn upgrade_legacy(node) {
    let defaults = if (node.tag == 'shape') shape_defaults else edge_defaults
    let absent = [for (key,value in defaults where step.attrs_get(node.attrs,symbol(key)) == null) {name:symbol(key),value:value}];
    {*:node,attrs:[*node.attrs,*absent]}
}
pub fn restore(value) any^ {
    let layer = value.drawing.content[0]
    let items = layer.content;
    if (not contains([1,2,3],value.version) or value.drawing.tag != "drawing" or len(value.drawing.content)!=1 or
        layer.tag != "layer" or not (items is array) or len(items)>max_objects or
        not all([for (node in items) scalar_attrs(node) and contains(["shape","connector"],node.tag) and node.content == []]))
        raise error("Draw: unsupported or invalid diagram file")
    else (
        let nodes = [for (node in items) (let restored = restore_node(node), if (value.version < 3) upgrade_legacy(restored) else restored)],
        let shapes = [for (node in nodes where node.tag == 'shape') id(node)],
        let valid = all([for (node in nodes) valid_node(node,shapes)]),
        if (not valid or len(unique([for (node in nodes) id(node)])) != len(nodes)) raise error("Draw: invalid shapes or connector endpoints")
        else drawing(nodes)
    )
}
pub fn open_document(st,value) {
    // loaded IDs need not follow our prefix convention; advance until an unused pair exists.
    let serial = next_free(value,1);
    {*:st,doc:value,hist:history.history_new(),selected:"",selection:[],editing:null,source:"",drag:null,next_id:serial,saved:value,dialog:"",error:"",status:"Diagram opened."}
}
