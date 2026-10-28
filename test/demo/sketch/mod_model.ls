// immutable illustration values (S1.4); previews share the editor's snapshot history.
import geom: lambda.editor.draw.geometry
import history: lambda.editor.mod_history
import raster: .mod_raster

pub let TOOLS = [
    {id:"select",label:"Select",key:"V"}, {id:"nodes",label:"Edit nodes",key:"A"},
    {id:"marquee",label:"Rectangle select",key:"M"},
    {id:"brush",label:"Brush",key:"B"}, {id:"pencil",label:"Pencil",key:"N"},
    {id:"pen",label:"Pen",key:"P"}, {id:"rect",label:"Rectangle",key:"R"},
    {id:"ellipse",label:"Ellipse",key:"O"}, {id:"line",label:"Line",key:"L"},
    {id:"text",label:"Text",key:"T"}, {id:"eraser",label:"Object eraser",key:"E"},
    {id:"eyedropper",label:"Eyedropper",key:"I"}, {id:"pan",label:"Hand",key:"H"}
]
pub let COLORS = ["#252b36","#ffffff","#f5e9d4","#edb96b","#dd734b","#be5046",
    "#e3a2a0","#a988b9","#7665d9","#495c9b","#284f59","#418b80",
    "#88af91","#c6d2a1","#9c785c","#a9b2bb"]
pub fn clamp(v,lo,hi) => max(lo,min(hi,v))
pub fn blank() => {title:"Untitled illustration",width:960.0,height:800.0,background:"#fffaf1",seq:2,
    layers:[{id:"l1",name:"Layer 1",visible:true,locked:false,opacity:1.0,objects:[]}]}
pub fn initial(doc) => {doc:doc,saved:doc,hist:history.history_new(),active:doc.layers[last].id,selected:"",selection:[],
    tool:"brush",fill:"#dd734b",stroke:"#284f59",size:8.0,opacity:1.0,font_size:48.0,text:"Your own way",
    zoom:0.65,pan:{x:0.0,y:0.0},drag:null,pen:[],editing:null,grid:false,dialog:"",clipboard:[],paste_count:0,error:"",status:"Ready to make something."}
pub fn objects(doc) => [for (layer in doc.layers) for (o in layer.objects) o]
pub fn layer_of(doc,id) => [for (layer in doc.layers where layer.id == id) layer][0]
pub fn lookup(doc,id) => [for (layer in doc.layers) for (o in layer.objects where o.id == id) {layer:layer,object:o}][0]
pub fn editable(layer) => layer != null and layer.visible and not layer.locked
pub fn selected_objects(st) => [for (l in st.doc.layers where editable(l))
    for (o in l.objects where o.id==st.selected or contains(st.selection,o.id)) o]
pub fn selected_ids(st) => [for (o in selected_objects(st)) o.id]
pub fn chosen(st) => (let picked = selected_objects(st),if (len(picked)==1) picked[0] else null)
pub fn select_ids(st,ids) {
    let valid = selected_ids({*:st,selected:"",selection:ids})
    let primary = if (len(valid)>0) valid[last] else ""
    {*:st,selected:primary,selection:valid,active:if (primary!="") lookup(st.doc,primary).layer.id else st.active}
}
pub fn select(st,id) => select_ids(st,[id])
pub fn bounds(o) => {x:o.x,y:o.y,width:o.width,height:o.height}
fn object_box(picked) {
    let xs = [for (o in picked) o.x]
    let ys = [for (o in picked) o.y]
    if (len(picked)==0) null else {x:min(xs),y:min(ys),width:max([for (o in picked) o.x+o.width])-min(xs),
        height:max([for (o in picked) o.y+o.height])-min(ys)}
}
pub fn selection_box(st) => object_box(selected_objects(st))
pub fn enclosed(doc,box) => [for (l in doc.layers where editable(l) and l.opacity>0)
    for (o in l.objects where o.opacity>0 and geom.rect_contains(box,{x:o.x,y:o.y}) and
        geom.rect_contains(box,{x:o.x+o.width,y:o.y+o.height})) o.id]
pub fn points(o) => [for (p in o.points) {x:o.x+p.x*o.width,y:o.y+p.y*o.height}]
pub fn make_object(id,kind,b,style={}) => {id:id,kind:kind,*:b,fill:"#dd734b",stroke:"#284f59",size:2.0,
    opacity:1.0,points:[],closed:false,text:"Text",font_size:48.0,*:style}
pub fn path_object(id,pts,style={}) {
    let x = min([for (p in pts) p.x])
    let y = min([for (p in pts) p.y])
    let w = max(1.0,max([for (p in pts) p.x])-x)
    let h = max(1.0,max([for (p in pts) p.y])-y)
    make_object(id,"path",{x:x,y:y,width:w,height:h},{fill:"none",*:style,
        points:[for (p in pts) {x:(p.x-x)/w,y:(p.y-y)/h}]})
}
fn map_objects(doc,ids,transform: fn (map) map) map => {*:doc,layers:[for (l in doc.layers) {*:l,
    objects:[for (o in l.objects) if (editable(l) and contains(ids,o.id)) transform(o) else o]}]}
pub fn replace_object(doc,id,patch) => map_objects(doc,[id],(o)=>{*:o,*:patch})
fn move_objects(doc,ids,dx,dy) map => map_objects(doc,ids,(o)=>{*:o,x:o.x+dx,y:o.y+dy})
fn set_layer(doc,id,patch) => {*:doc,layers:[for (l in doc.layers) if (l.id == id) {*:l,*:patch} else l]}
pub fn record_edit(st,doc,status="Illustration updated.") => {*:st,doc:doc,hist:history.history_snapshot(st.hist,st.doc,doc),
    drag:null,editing:null,error:"",status:status}
pub fn finish_edit(st) => if (st.editing == null) st else
    {*:st,hist:history.history_snapshot(st.hist,st.editing,st.doc),editing:null}
pub fn cancel(st) => if (st.drag != null) {*:st,doc:st.drag.before,
    selected:if (st.drag.mode=="marquee") st.drag.primary else st.selected,
    selection:if (st.drag.mode=="marquee") st.drag.previous else st.selection,
    active:if (st.drag.mode=="marquee") st.drag.active else st.active,
    pan:if (st.drag.mode=="pan") st.drag.pan else st.pan,drag:null,error:"",status:"Gesture cancelled."}
    else if (st.editing != null) {*:st,doc:st.editing,editing:null,error:""}
    else {*:st,selected:"",selection:[],pen:[],dialog:"",error:"",status:"Ready."}
pub fn step_history(st,redo=false) {
    let r = if (redo) history.history_redo(st.hist,st.doc) else history.history_undo(st.hist,st.doc)
    let active = if (layer_of(r.doc,st.active) != null) st.active else r.doc.layers[last].id
    select_ids({*:st,doc:r.doc,hist:r.hist,active:active,
        drag:null,pen:[],error:"",status:if (redo) "Redone." else "Undone."},selected_ids(st))
}
pub fn insert(st,o) => if (not editable(layer_of(st.doc,st.active))) {*:st,status:"Choose a visible, unlocked layer."}
    else if (lookup(st.doc,o.id) != null) {*:st,status:"That object identity is already in use."}
    else if (len(objects(st.doc)) >= 1000) {*:st,status:"This illustration has reached its 1,000 object limit."}
    else (let doc = set_layer(st.doc,st.active,{objects:[*layer_of(st.doc,st.active).objects,o]}),
        {*:record_edit(st,{*:doc,seq:doc.seq+1},"Object added."),selected:o.id,selection:[o.id]})
pub fn remove(st) {
    let ids = selected_ids(st)
    if (len(ids)==0) st else select_ids(record_edit(st,{*:st.doc,layers:[for (l in st.doc.layers)
        {*:l,objects:[for (o in l.objects where not contains(ids,o.id)) o]}]},"Selection deleted."),[])
}
// duplicate and paste reserve identities and publish the complete insertion as one edit.
fn insert_copies(st,entries,dx,dy,target=null) {
    let copies = [for (i,entry in entries) {layer:if (target==null) entry.layer else target,
        object:{*:entry.object,id:"o" ++ string(st.doc.seq+i),x:entry.object.x+dx,y:entry.object.y+dy}}]
    let doc = {*:st.doc,seq:st.doc.seq+len(copies),layers:[for (l in st.doc.layers)
        {*:l,objects:[*l.objects,*[for (copy in copies where copy.layer==l.id) copy.object]]}]}
    if (len(entries)==0) st
    else if (len(objects(st.doc))+len(entries)>1000 or raster.storage(objects(doc))>raster.MAX_EMBEDDED)
        {*:st,status:"The illustration's object or embedded image limit has been reached."}
    else select_ids(record_edit(st,doc,"Objects inserted."),[for (copy in copies) copy.object.id])
}
fn selection_entries(st) => [for (o in selected_objects(st)) {layer:lookup(st.doc,o.id).layer.id,object:o}]
pub fn duplicate(st) => insert_copies(st,selection_entries(st),20.0,20.0)
pub fn copy_selection(st,cut=false) {
    let entries = selection_entries(st)
    let copied = {*:st,clipboard:entries,paste_count:0}
    if (len(entries)==0) st else if (cut) {*:remove(copied),status:"Selection cut to the Sketch clipboard."}
    else {*:copied,status:"Selection copied to the Sketch clipboard."}
}
pub fn can_add_image(st) => len(st.doc.layers)<64 and len(objects(st.doc))<1000
fn image_clipboard(st) => len(st.clipboard)==1 and st.clipboard[0].object.kind=="image"
pub fn can_paste(st) => len(st.clipboard)>0 and (if (image_clipboard(st)) can_add_image(st) else editable(layer_of(st.doc,st.active))) and
    len(objects(st.doc))+len(st.clipboard)<=1000 and raster.storage(objects(st.doc))+raster.storage([for (e in st.clipboard) e.object])<=raster.MAX_EMBEDDED
fn image_layer(st,o) {
    let layer = {id:"l" ++ string(st.doc.seq),name:o.image_name,visible:true,locked:false,opacity:1.0,objects:[o]}
    let doc = {*:st.doc,seq:st.doc.seq+2,layers:[*st.doc.layers,layer]}
    if (not can_add_image(st) or raster.storage(objects(doc))>raster.MAX_EMBEDDED)
        {*:st,error:"Sketch: the layer, object or 64 MB embedded image limit has been reached."}
    else {*:record_edit(st,doc,"Image added as a new layer."),active:layer.id,selected:o.id,selection:[o.id],tool:"select",pen:[]}
}
pub fn add_image(st,source,point=null) {
    let scale = min(1.0,min(st.doc.width*.8/source.image_width,st.doc.height*.8/source.image_height))
    let w = source.image_width*scale
    let h = source.image_height*scale
    image_layer(st,make_object("o" ++ string(st.doc.seq+1),"image",{
        x:if (point==null) (st.doc.width-w)/2 else point.x,y:if (point==null) (st.doc.height-h)/2 else point.y,width:w,height:h},
        {fill:"none",stroke:"none",*:source}))
}
pub fn paste(st,point=null) {
    if (not can_paste(st)) {*:st,status:"Paste needs copied objects, space, and a visible, unlocked layer."}
    else {
        let box = object_box([for (entry in st.clipboard) entry.object])
        let dx = if (point==null) 20.0*(st.paste_count+1) else point.x-box.x
        let dy = if (point==null) 20.0*(st.paste_count+1) else point.y-box.y
        let pasted = if (image_clipboard(st)) image_layer(st,{*:st.clipboard[0].object,
            id:"o" ++ string(st.doc.seq+1),x:box.x+dx,y:box.y+dy}) else insert_copies(st,st.clipboard,dx,dy,st.active)
        {*:pasted,paste_count:st.paste_count+1,status:"Objects pasted."}
    }
}
pub fn context_select(st,p) {
    let target = hit(st.doc,p,5.0/st.zoom)
    if (contains(selected_ids(st),target)) st else select(st,target)
}
pub fn nudge(st,dx,dy) any^ => record_edit(st,move_objects(st.doc,selected_ids(st),dx,dy)^,"Selection moved.")
pub fn arrange(st,front) {
    let ids = selected_ids(st)
    let doc = {*:st.doc,layers:[for (l in st.doc.layers) (
        let rest = [for (o in l.objects where not contains(ids,o.id)) o],
        let picked = [for (o in l.objects where contains(ids,o.id)) o],
        {*:l,objects:if (front) [*rest,*picked] else [*picked,*rest]})]}
    record_edit(st,doc,"Stacking order updated.")
}

// picking shares the package's geometric primitives; hidden and locked layers do not intercept input.
fn object_hit(o,p,tolerance) {
    let b = bounds(o)
    if (o.opacity == 0) false
    else if (o.kind == "path" or o.kind == "line") {
        let pts = points(o)
        let closed = if (o.closed and len(pts)>0) [*pts,pts[0]] else pts
        ; (o.closed and o.fill != "none" and geom.point_in_polygon(p,pts)) or
            geom.point_near_polyline(p,closed,max(tolerance,o.size/2.0))
    } else if (o.kind == "text" or o.kind == "image") geom.rect_contains(b,p)
    else if (o.kind == "ellipse") {
        let outer = {x:b.x-tolerance,y:b.y-tolerance,width:b.width+2*tolerance,height:b.height+2*tolerance}
        let inset = max(tolerance,o.size/2)
        let inner = {x:b.x+inset,y:b.y+inset,width:max(0.0,b.width-2*inset),height:max(0.0,b.height-2*inset)}
        geom.point_in_ellipse(p,outer) and (o.fill != "none" or not geom.point_in_ellipse(p,inner))
    } else {
        let pts = [{x:o.x,y:o.y},{x:o.x+o.width,y:o.y},{x:o.x+o.width,y:o.y+o.height},{x:o.x,y:o.y+o.height},{x:o.x,y:o.y}]
        ; (o.fill != "none" and geom.rect_contains(b,p)) or geom.point_near_polyline(p,pts,max(tolerance,o.size/2.0))
    }
}
pub fn hit(doc,p,tolerance=4.0) => (let hits = [for (l in doc.layers where editable(l) and l.opacity > 0)
    for (o in l.objects where object_hit(o,p,tolerance)) o.id], if (len(hits)>0) hits[last] else "")
pub fn inside(doc,p) => p.x>=0 and p.y>=0 and p.x<=doc.width and p.y<=doc.height
pub fn snap(st,p) => if (not st.grid) p else {x:round(p.x/20)*20.0,y:round(p.y/20)*20.0}
pub fn style(st) => {fill:st.fill,stroke:st.stroke,size:st.size,opacity:st.opacity,text:st.text,font_size:st.font_size}
pub fn new_shape(st,kind,a,b,shift=false) {
    let end = if (shift and contains(["rect","ellipse"],kind)) (let d = max(abs(b.x-a.x),abs(b.y-a.y)),
        {x:a.x+(if (b.x<a.x) -d else d),y:a.y+(if (b.y<a.y) -d else d)})
        else if (shift and kind=="line") (let angle = round(math.atan2(b.y-a.y,b.x-a.x)/(math.pi/4))*math.pi/4,
            let length = geom.v2_dist(a,b),{x:a.x+length*math.cos(angle),y:a.y+length*math.sin(angle)}) else b
    let box = geom.rect_from_points(a,end)
    let id = "o" ++ string(st.doc.seq)
    if (kind == "line") path_object(id,[a,end],{*:style(st),kind:"line",fill:"none"})
    else make_object(id,kind,{*:box,width:max(1.0,box.width),height:max(1.0,box.height)},style(st))
}
pub fn draft(st) => if (st.drag == null or not contains(["draw","shape"],st.drag.mode)) null
    else if (st.drag.mode == "draw") path_object("draft",st.drag.pts,
        {*:style(st),fill:"none",size:if (st.tool == "pencil") min(2.0,st.size) else st.size})
    else new_shape(st,st.tool,st.drag.start,st.drag.end,st.drag.shift)
pub fn begin(st,p,pointer=1,shift=false) {
    let q = snap(st,p)
    let id = hit(st.doc,p,5.0/st.zoom)
    let selected = chosen(st)
    let ids = selected_ids(st)
    let base = {before:st.doc,start:q,end:q,pts:[q],pointer:pointer,shift:false,
        previous:ids,primary:st.selected,active:st.active}
    if (contains(["select","nodes","marquee"],st.tool)) {
        let node_hits = if (st.tool == "nodes" and selected != null) [for (i,node in points(selected) where geom.v2_dist(node,p)<9.0/st.zoom) i] else []
        let resize = st.tool == "select" and selected != null and
            geom.v2_dist(p,{x:selected.x+selected.width,y:selected.y+selected.height})<10.0/st.zoom
        if (st.tool=="marquee" or (st.tool=="select" and id=="" and not resize))
            {*:select_ids(st,if (shift) ids else []),drag:{*:base,mode:"marquee",start:p,end:p,additive:shift}}
        else if (len(node_hits)>0) {*:st,drag:{*:base,mode:"node",id:st.selected,node:node_hits[0]}}
        else if (resize) {*:st,drag:{*:base,mode:"resize",id:st.selected}}
        else if (shift and st.tool=="select" and id!="") select_ids(st,
            if (contains(ids,id)) [for (selected_id in ids where selected_id!=id) selected_id] else [*ids,id])
        else if (id != "") (let next = if (st.tool=="select" and contains(ids,id)) st else select(st,id),
            {*:next,drag:{*:base,mode:"move",id:next.selected,ids:selected_ids(next)}})
        else select_ids(st,[])
    } else if (st.tool == "eyedropper") {
        let o = lookup(st.doc,id).object
        if (o == null) st else {*:st,stroke:if (o.fill != "none") o.fill else o.stroke,status:"Color picked."}
    } else if (not editable(layer_of(st.doc,st.active))) {*:st,status:"Choose a visible, unlocked layer."}
    else if (st.tool == "eraser") (let erased = remove(select(st,id)),
        {*:erased,hist:st.hist,drag:{*:base,mode:"erase"},selected:"",selection:[]})
    else if (st.tool == "text") insert(st,make_object("o" ++ string(st.doc.seq),"text",
        {x:q.x,y:q.y,width:max(24.0,len(st.text)*st.font_size*.62),height:st.font_size*1.25},{*:style(st),fill:st.stroke,stroke:"none"}))
    else if (contains(["brush","pencil"],st.tool)) {*:st,selected:"",selection:[],drag:{*:base,mode:"draw"}}
    else {*:st,selected:"",selection:[],drag:{*:base,mode:"shape"}}
}
pub fn preview(st,p,shift=false) {
    let d = st.drag
    let q = snap(st,p)
    if (d == null) st
    // marquee geometry stays in document coordinates and never previews a document edit.
    else if (d.mode=="marquee") (
        let ids = if (geom.v2_dist(d.start,p)*st.zoom<3.0) [] else enclosed(d.before,geom.rect_from_points(d.start,p)),
        {*:select_ids(st,if (d.additive) [*d.previous,*ids] else ids),drag:{*:d,end:p}})
    else if (d.mode == "draw") (let tail = d.pts[last], let pts =
        if (geom.v2_dist(tail,q)>=1.0/st.zoom and len(d.pts)<4096) [*d.pts,q] else d.pts,
        {*:st,drag:{*:d,pts:pts,end:q}})
    else if (d.mode == "shape") {*:st,drag:{*:d,end:q,shift:shift}}
    else if (d.mode == "erase") (let erased = remove(select(st,hit(st.doc,p,st.size/2.0))),
        {*:erased,hist:st.hist,drag:{*:d,end:q},selected:"",selection:[]})
    else if (d.mode=="move") {*:st,doc:move_objects(d.before,d.ids,q.x-d.start.x,q.y-d.start.y),drag:{*:d,end:q}}
    else {
        let o = lookup(d.before,d.id).object
        let dx = q.x-d.start.x
        let dy = q.y-d.start.y
        let patch = if (d.mode == "resize" and o.kind=="image" and not shift) (
            let scale = max(2.0/min(o.width,o.height),max((o.width+dx)/o.width,(o.height+dy)/o.height)),
            {width:o.width*scale,height:o.height*scale})
            else if (d.mode == "resize") {width:max(2.0,o.width+dx),height:max(2.0,o.height+dy)}
            // renormalize node coordinates so selection bounds continue to enclose the edited path.
            else if (d.mode == "node") path_object(o.id,[for (i,pt in points(o)) if (i == d.node) q else pt],{*:style(o),kind:o.kind,closed:o.closed})
            else {x:o.x+dx,y:o.y+dy}
        {*:st,doc:replace_object(d.before,d.id,patch),drag:{*:d,end:q}}
    }
}
pub fn finish(st) {
    let d = st.drag
    if (d == null) st
    else if (d.mode=="marquee") {*:st,drag:null,status:string(len(selected_ids(st))) ++ " objects selected."}
    else if (d.mode == "draw" or d.mode == "shape") {
        let o = draft(st)
        let dot = d.mode == "draw" and len(d.pts) == 1
        let drawn = if (dot) path_object(o.id,[d.start,{x:d.start.x+.01,y:d.start.y}],style(o)) else o
        if (d.mode == "shape" and geom.v2_dist(d.start,d.end)<2.0/st.zoom) {*:st,drag:null}
        else insert({*:st,drag:null},{*:drawn,id:"o" ++ string(st.doc.seq)})
    } else {*:st,drag:null,hist:history.history_snapshot(st.hist,d.before,st.doc),status:"Edit complete."}
}
pub fn pen_point(st,p) => if (not editable(layer_of(st.doc,st.active))) {*:st,status:"Choose a visible, unlocked layer."}
    else if (len(st.pen)>=3 and geom.v2_dist(st.pen[0],p)<10.0/st.zoom) finish_pen(st,true)
    else if (len(st.pen)>=4096) st else {*:st,pen:[*st.pen,snap(st,p)],status:"Click to add nodes. Enter finishes; click the first node to close; Esc cancels."}
pub fn finish_pen(st,closed=false) => if (len(st.pen)<2) {*:st,pen:[]} else
    {*:insert(st,path_object("o" ++ string(st.doc.seq),st.pen,{*:style(st),fill:if (closed) st.fill else "none",closed:closed})),pen:[]}

pub fn layer_action(st,action,id="") {
    let target = if (id == "") st.active else id
    let l = layer_of(st.doc,target)
    let seq = st.doc.seq
    if (action == "select") {*:st,active:target,selected:"",selection:[]}
    else if (action == "add" and len(st.doc.layers)>=64) {*:st,status:"This illustration has reached its 64 layer limit."}
    else if (action == "add") (let added = {id:"l" ++ string(seq),name:"Layer " ++ string(len(st.doc.layers)+1),visible:true,locked:false,opacity:1.0,objects:[]},
        {*:record_edit(st,{*:st.doc,layers:[*st.doc.layers,added],seq:seq+1},"Layer added."),active:added.id,selected:"",selection:[]})
    else if (action == "delete") {
        if (l == null or l.locked or len(st.doc.layers)<=1) st else (let layers = [for (row in st.doc.layers where row.id != target) row],
            {*:record_edit(st,{*:st.doc,layers:layers},"Layer deleted."),active:layers[last].id,selected:"",selection:[]})
    } else if (action == "up" or action == "down") {
        let at = [for (i,row in st.doc.layers where row.id == target) i][0]
        let dest = clamp(at+(if (action == "up") 1 else -1),0,len(st.doc.layers)-1)
        record_edit(st,{*:st.doc,layers:[for (i,row in st.doc.layers) if (i == at) st.doc.layers[dest] else if (i == dest) l else row]},"Layer order updated.")
    } else if (contains(["visible","locked"],action) and l != null) {*:record_edit(st,set_layer(st.doc,target,{[action]:not l[action]})),selected:"",selection:[]}
    else st
}
pub fn valid_color(v) => v is string and (v == "none" or (len(v)==7 and starts_with(v,"#") and
    len([for (ch in split(lower(slice(v,1)),"") where not contains("0123456789abcdef",ch)) ch])==0))
pub fn property(st,key,value) any^ {
    let checked_key = if (not contains(["fill","stroke","size","opacity","font_size","text","x","y","width","height","layer_name","layer_opacity"],key))
        raise error("Sketch: unknown property.")
    let numeric = contains(["size","opacity","font_size","x","y","width","height","layer_opacity"],key)
    let v = if (numeric) float(value)^ else value
    let checked_number = if (numeric and (not geom.finite_number(v) or abs(v)>100000.0)) raise error("Sketch: enter a finite number within 100,000.")
    let checked_color = if (contains(["fill","stroke"],key) and not valid_color(v)) raise error("Sketch: use a six-digit hex color or none.")
    let checked_size = if (contains(["size","font_size","width","height"],key) and (v<.1 or v>4096)) raise error("Sketch: size must be between 0.1 and 4096.")
    let checked_opacity = if (contains(["opacity","layer_opacity"],key) and (v<0 or v>1)) raise error("Sketch: opacity must be between 0 and 1.")
    let checked_text = if (contains(["text","layer_name"],key) and (not (v is string) or len(v)>500)) raise error("Sketch: text must be at most 500 characters.")
    let defaults = if (contains(["fill","stroke","size","opacity","font_size","text"],key)) {[key]:v} else map()
    let picked = selected_objects(st)
    let single = chosen(st)
    let layer = layer_of(st.doc,st.active)
    let patch = if (single.kind=="text" and contains(["text","font_size"],key)) (
        let caption = if (key=="text") v else single.text,
        let font = if (key=="font_size") v else single.font_size,
        {[key]:v,width:max(24.0,len(caption)*font*.62),height:font*1.25}) else {[key]:v}
    let doc = if (key == "layer_opacity") (if (layer.locked) st.doc else set_layer(st.doc,st.active,{opacity:v}))
        else if (key == "layer_name") (if (layer.locked) st.doc else set_layer(st.doc,st.active,{name:v}))
        else if (len(picked)>0 and (len(picked)==1 or contains(["fill","stroke","size","opacity"],key)))
            map_objects(st.doc,[for (o in picked where o.kind!="image" or not contains(["fill","stroke","size","text","font_size"],key)) o.id],(o)=>{*:o,*:patch}) else st.doc
    {*:st,*:defaults,doc:doc,editing:if (st.editing == null) st.doc else st.editing,error:"",status:"Properties updated."}
}

// strict archives keep a failed open from replacing the current drawing.
fn valid_object(o) => o is map and o.id is string and len(o.id)>0 and
    contains(["path","line","rect","ellipse","text","image"],o.kind) and
    (o.kind!="image" or raster.valid_descriptor(o)) and
    all([for (key in ["x","y","width","height","size","opacity","font_size"]) geom.finite_number(o[key]) and abs(o[key])<=100000]) and
    o.width>0 and o.height>0 and o.size>=.1 and o.size<=4096 and o.font_size>=.1 and o.font_size<=4096 and o.opacity>=0 and o.opacity<=1 and
    valid_color(o.fill) and valid_color(o.stroke) and o.closed is bool and o.text is string and len(o.text)<=500 and
    o.points is array and len(o.points)<=4096 and
    (not contains(["path","line"],o.kind) or len(o.points)>=2) and
    all([for (p in o.points) p is map and geom.finite_number(p.x) and geom.finite_number(p.y) and abs(p.x)<=100000 and abs(p.y)<=100000])
pub fn decode(data) any^ {
    let checked_format = if (not (data is map) or data.format != "lambda-sketch" or not contains([1,2],data.version)) raise error("Sketch: unsupported project format.")
    let doc = data.document
    let checked_document = if (not (doc is map) or not (doc.title is string) or len(doc.title)>500 or
        not geom.finite_number(doc.width) or not geom.finite_number(doc.height) or doc.width<1 or doc.height<1 or doc.width>16384 or doc.height>16384 or
        not valid_color(doc.background) or not (doc.layers is array) or len(doc.layers)<1 or len(doc.layers)>64 or
        not (doc.seq is int) or doc.seq<1 or doc.seq>100000000) raise error("Sketch: invalid document.")
    let valid_layers = all([for (l in doc.layers) l is map and l.id is string and len(l.id)>0 and l.name is string and len(l.name)<=500 and
        l.visible is bool and l.locked is bool and geom.finite_number(l.opacity) and l.opacity>=0 and l.opacity<=1 and l.objects is array])
    let checked_layers = if (not valid_layers) raise error("Sketch: invalid layers.")
    let obs = objects(doc)
    let ids = [*[for (l in doc.layers) l.id],*[for (o in obs) o.id]]
    let checked_objects = if (len(obs)>1000 or len(unique(ids)) != len(ids) or not all([for (o in obs) valid_object(o)])) raise error("Sketch: invalid or duplicate objects.")
    let images = [for (o in obs where o.kind=="image") o]
    let checked_budget = if ((data.version==1 and len(images)>0) or raster.storage(obs)>raster.MAX_EMBEDDED)
        raise error("Sketch: invalid embedded image storage.")
    // decode each shared source once before publishing any part of the project.
    let checked_images = [for (src in unique([for (o in images) o.src]),
        let shared = [for (o in images where o.src==src) o], let first = shared[0], let valid = raster.validate(first)^)
        if (all([for (o in shared) o.image_width==first.image_width and o.image_height==first.image_height])) true
        else raise error("Sketch: copies of an embedded image disagree on its dimensions.")]
    // generated identities and a monotonic counter prevent collisions after a reopened project grows.
    let checked_ids = if (not all([for (id in ids) len(id)>1 and contains(["o","l"],slice(id,0,1)) and
        all([for (ch in split(slice(id,1),"")) contains("0123456789",ch)])])) raise error("Sketch: invalid object identity.")
    let counters = [for (id in ids) int(slice(id,1))^]
    let checked_counter = if (any([for (n in counters) n<1 or n>=doc.seq])) raise error("Sketch: project identity counter is in use.")
    doc
}
pub fn encode(doc) => {format:"lambda-sketch",version:if (any([for (o in objects(doc)) o.kind=="image"])) 2 else 1,document:doc}
pub fn open_document(st,doc) => {*:record_edit(st,doc,"Project opened."),saved:doc,active:doc.layers[last].id,selected:"",selection:[],pen:[],dialog:""}
