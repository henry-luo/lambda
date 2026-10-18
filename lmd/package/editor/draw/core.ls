// S1.4 / S12.1.3: semantic edits produce shared editor transactions, never DOM repairs.
import model: .model
import shapes: .shapes
import geom: .geometry
import motion: lambda.slide.motion
import paths: .path
import defaults: lambda.slide.common
import doc: lambda.editor.mod_doc
import step: lambda.editor.mod_step
import tx: lambda.editor.mod_transaction

pub fn finite(v) => geom.finite_number(v)
fn value(v,fallback) => defaults.value(v,fallback)
pub fn serializable(v,depth=0) => depth<32 and (v == null or v is string or v is symbol or v is bool or finite(v) or
    (v is array and all([for (item in v) serializable(item,depth+1)])) or
    (v is map and all([for (key,item in v) (key is string or key is symbol) and serializable(item,depth+1)])))
pub fn new_document(options=null) => model.with_attrs(model.drawing([]),
    {profile:"draw",width:value(options.width,1120.0),height:value(options.height,760.0),
     'allow-loops':value(options['allow-loops'],false),'max-objects':value(options['max-objects'],200)})
pub fn vertices(drawing) => [for (n in model.objects(drawing) where n.tag == 'shape') n]
pub fn edges(drawing) => [for (n in model.objects(drawing) where n.tag == 'connector') n]
pub fn incident_edges(drawing,id) => [for (n in edges(drawing) where
    model.attr(n,"from-shape") == id or model.attr(n,"to-shape") == id) n]
pub fn incoming_edges(drawing,id) => [for (n in edges(drawing) where model.attr(n,"to-shape") == id) n]
pub fn outgoing_edges(drawing,id) => [for (n in edges(drawing) where model.attr(n,"from-shape") == id) n]
pub fn neighbors(drawing,id) {
    let adjacent = [for (n in incident_edges(drawing,id))
        model.attr(n,if (model.attr(n,"from-shape") == id) "to-shape" else "from-shape")];
    unique([for (other in adjacent where other != "") other])
}
fn port_list(n) => value(model.attr(n,"ports"),value(model.attr(n,"descriptor").ports,[]))
fn valid_ports(ports) => ports is array and len(ports)<=64 and
    len(unique([for (p in ports) p.id])) == len(ports) and all([for (p in ports)
        p is map and p.id is string and len(p.id)>0 and not contains(model.ports,p.id) and
        finite(p.x) and finite(p.y) and p.x>=0 and p.x<=1 and p.y>=0 and p.y<=1 and
        (p.direction == null or contains(['in','out','both'],p.direction))])
fn descriptor_ok(d) => d is map and d.id is string and len(d.id)>0 and d.outline is array and len(d.outline)>=3 and len(d.outline)<=512 and
    all([for (p in d.outline) p is array and len(p)==2 and all([for (n in p) finite(n) and n>=0 and n<=1])]) and
    (d.ports == null or valid_ports(d.ports))

pub fn vertex(id,descriptor,geometry,properties=null) any^ {
    let custom = descriptor is map
    let checked_orientation = if (properties.rotate != null and properties.rotation != null) raise error("Draw: conflicting orientation fields")
    let checked = if ((custom and not descriptor_ok(descriptor)) or
        (not custom and not contains(shapes.kinds,descriptor))) raise error("Draw: unknown or invalid shape descriptor")
    let kind = if (custom) descriptor.id else descriptor
    let base = model.make_shape(id,if (custom) "rect" else kind,geometry.x,geometry.y,value(properties.label,""))
    let merged = model.with_attrs(base,{*:geometry,*:value(properties,{}),variant:kind,
        *:(if (custom) {descriptor:descriptor} else {}),rotate:value(properties.rotate,value(properties.rotation,0.0))});
    {*:merged,attrs:[for (a in merged.attrs where a.name != 'rotation') a]}
}
pub fn endpoint_values(endpoint,side) any^ {
    let checked = if (not contains(['point','boundary','port'],endpoint.kind)) raise error("Draw: invalid endpoint kind")
    let checked_point = if (endpoint.kind == 'point' and (not finite(endpoint.x) or not finite(endpoint.y))) raise error("Draw: invalid free endpoint")
    let checked_vertex = if (endpoint.kind != 'point' and (not (endpoint.vertex is string) or endpoint.vertex == "")) raise error("Draw: invalid endpoint vertex")
    let port = if (endpoint.kind == 'point') "auto" else if (endpoint.kind == 'boundary') value(endpoint.side,"auto") else endpoint.port
    let checked_port = if (not (port is string) and not (port is symbol)) raise error("Draw: invalid endpoint port")
    let checked_named = if (endpoint.kind == 'port' and contains(model.ports,string(port))) raise error("Draw: cardinal names are boundary sides, not named ports")
    let checked_side = if (endpoint.kind == 'boundary' and not contains(model.ports,string(port))) raise error("Draw: unknown attachment side");
    {[side ++ "-shape"]:if (endpoint.kind == 'point') "" else endpoint.vertex,
     [side ++ "-port"]:string(port),[side ++ "-x"]:if (endpoint.kind == 'point') endpoint.x else 0.0,
     [side ++ "-y"]:if (endpoint.kind == 'point') endpoint.y else 0.0}
}
pub fn edge(id,source,target,properties=null) any^ =>
    model.with_attrs(model.edge(id,"","",value(properties.label,"")),
        {*:value(properties,{}),*:endpoint_values(source,"from")^,*:endpoint_values(target,"to")^,
         routing:symbol(value(properties.routing,"orthogonal"))})

fn endpoint_issue(drawing,n,side) {
    let target = model.attr(n,side ++ "-shape")
    let port = model.attr(n,side ++ "-port")
    let vertex = model.lookup(drawing,target).shape
    let matches = [for (p in port_list(vertex) where p.id == port) p]
    let declared = matches[0];
    if (target == "") (if (finite(model.attr(n,side ++ "-x")) and finite(model.attr(n,side ++ "-y"))) null else "invalid free endpoint")
    else if (vertex.tag != 'shape') "missing endpoint vertex"
    else if (contains(model.ports,port)) null
    else if (declared == null) "unknown named port"
    else if ((side == "from" and declared.direction == 'in') or (side == "to" and declared.direction == 'out')) "port direction rejects this connection"
    else null
}
fn well_formed(n) => doc.is_node(n) and n.attrs is array and n.content is array and
    all([for (a in n.attrs) a is map and (a.name is symbol or a.name is string)]) and
    len(unique([for (a in n.attrs) string(a.name)])) == len(n.attrs)
fn object_issue(drawing,n) {
    let b = model.box(n)
    let size = model.page(drawing)
    let descriptor = model.attr(n,"descriptor")
    let custom = model.attr(n,"segments")
    let route = if (custom == null) null else (motion.compile(custom,"draw.edge") ^ { null });
    if (not (model.id(n) is string) or model.id(n) == "" or len(model.id(n))>128) "invalid identity"
    else if (not well_formed(n) or n.content != []) "malformed object attributes"
    else if (not serializable(n)) "object contains non-serializable data"
    else if (not (model.attr(n,"label") is string) or not model.valid_color(model.attr(n,"stroke")) or
        not model.valid_color(model.attr(n,"text-color")) or not contains(model.stroke_styles,model.attr(n,"stroke-style")) or
        not all([for (key in ["stroke-width","font-size","opacity"]) finite(model.attr(n,key))]) or
        model.attr(n,"stroke-width")<0 or model.attr(n,"font-size")<=0 or model.attr(n,"opacity")<0 or model.attr(n,"opacity")>1) "invalid appearance"
    else if (n.tag == 'shape') (
        if (not contains(['rect','ellipse'],model.attr(n,"kind")) or
            (descriptor == null and (model.attr(n,"variant") == "ellipse") != (model.attr(n,"kind") == 'ellipse'))) "inconsistent vertex primitive"
        else if (not all([for (key in ["x","y","width","height","rotation"]) finite(model.attr(n,key))]) or b.width<=0 or b.height<=0 or
            b.x<0 or b.y<0 or b.x+b.width>size.width or b.y+b.height>size.height) "invalid vertex geometry"
        else if (not contains(shapes.kinds,model.attr(n,"variant")) and not descriptor_ok(descriptor)) "unknown shape descriptor"
        else if (descriptor != null and (not descriptor_ok(descriptor) or descriptor.id != model.attr(n,"variant"))) "invalid shape descriptor"
        else if (not valid_ports(port_list(n))) "invalid vertex ports"
        else if (not (model.attr(n,"flip-x") is bool) or not (model.attr(n,"flip-y") is bool) or
            (model.attr(n,"fill") != "none" and not model.valid_color(model.attr(n,"fill"))) or
            not contains(model.patterns,model.attr(n,"pattern"))) "invalid vertex appearance"
        else if (model.attr(n,"rotate") != null and step.attrs_get(n.attrs,'rotation') != null) "conflicting orientation fields"
        else null)
    else if (n.tag == 'connector') (
        let issues = [for (side in ["from","to"]) endpoint_issue(drawing,n,side)],
        if (issues[0] != null) issues[0] else if (issues[1] != null) issues[1]
        else if (model.attr(n,"from-shape") != "" and model.attr(n,"from-shape") == model.attr(n,"to-shape") and
            model.attr(drawing,"allow-loops") != true) "self loops are disabled"
        else if (not contains(model.routings,string(model.attr(n,"routing")))) "unknown edge router"
        else if (not contains(model.arrows,model.attr(n,"start-arrow")) or not contains(model.arrows,model.attr(n,"end-arrow")) or
            not finite(model.attr(n,"label-position")) or model.attr(n,"label-position")<0 or model.attr(n,"label-position")>1) "invalid edge appearance"
        else if (custom != null and route == null) "invalid custom path"
        else if (not model.valid_waypoints(model.attr(n,"waypoints"))) "invalid waypoints"
        else null)
    else "unsupported drawing object"
}
pub fn validate(drawing,options=null) {
    let size = model.page(drawing)
    let objects = model.objects(drawing)
    let ids = [for (n in objects) model.id(n)]
    let structure = well_formed(drawing) and drawing.tag == 'drawing' and len(drawing.content)==1 and
        well_formed(drawing.content[0]) and drawing.content[0].tag == 'layer' and objects is array;
    if (not structure) [{path:[],message:"expected a drawing with one layer"}]
    else [*(if (not serializable(drawing) or
        (model.attr(drawing,"allow-loops") != null and not (model.attr(drawing,"allow-loops") is bool))) [{path:[],message:"invalid document options"}] else []),
        *(if (not finite(size.width) or not finite(size.height) or size.width<=0 or size.height<=0) [{path:[],message:"invalid page dimensions"}] else []),
        *(if (len(unique(ids)) != len(ids)) [{path:[],message:"duplicate object identities"}] else []),
        *(if (not finite(model.object_limit(drawing)) or model.object_limit(drawing)<1 or floor(model.object_limit(drawing)) != model.object_limit(drawing)) [{path:[],message:"invalid object limit"}] else []),
        *(if (len(objects)>value(model.attr(drawing,"max-objects"),200)) [{path:[],message:"object limit exceeded"}] else []),
        *[for (i,n in objects) (let issue = object_issue(drawing,n),
            *if (issue != null) [{path:[0,i],id:model.id(n),message:issue}] else [])]]
}
pub fn checked(drawing) any^ {
    let diagnostics = validate(drawing);
    if (len(diagnostics)>0) raise error("Draw: " ++ diagnostics[0].message ++ " (" ++ value(diagnostics[0].id,"document") ++ ")") else drawing
}
pub fn open_session(drawing,options=null) any^ {
    let initial = model.initial(checked(drawing)^)
    let zoom = value(options.zoom,1.0)
    let checked_zoom = if (not finite(zoom) or zoom<0.1 or zoom>3.0) raise error("Draw: zoom must be between 0.1 and 3")
    let checked_grid = if (options.grid != null and not (options.grid is bool)) raise error("Draw: grid must be boolean");
    model.select_ids({*:initial,grid:value(options.grid,false),zoom:zoom},value(options.selection,[]))
}

fn patch(node,properties) any^ {
    let checked = if (properties.rotate != null and properties.rotation != null) raise error("Draw: conflicting orientation fields")
    // normalize an edited legacy orientation at the public API boundary.
    let normalized = if (properties.rotate != null or (properties.rotation != null and model.attr(node,"rotate") != null))
        {*:node,attrs:[for (a in node.attrs where a.name != 'rotation') a]} else node
    let values = if (properties.rotation != null and model.attr(node,"rotate") != null)
        {*:map([for (key,value in properties where string(key) != "rotation") *[key,value]]),rotate:properties.rotation} else properties;
    model.with_attrs(normalized,values)
}
fn changed(drawing,command) any^ {
    let kind = command.kind
    let ids = value(command.ids,if (command.id != null) [command.id] else [])
    let present = [for (id in ids) model.lookup(drawing,id)]
    let checked_ids = if (not all([for (found in present) found != null])) raise error("Draw: command targets a missing object")
    let require_target = if (contains(['set','reconnect','reverse','front','back'],kind) and len(ids)==0) raise error("Draw: command requires a target")
    let checked_identity = if (command.properties.id != null) raise error("Draw: object identity is immutable; duplicate to create a new identity")
    let checked_endpoint = if (kind == 'reconnect' and (not contains(["from","to"],command.side) or present[0].shape.tag != 'connector')) raise error("Draw: reconnect requires an edge and endpoint side")
    let checked_reverse = if (kind == 'reverse' and present[0].shape.tag != 'connector') raise error("Draw: reverse requires an edge")
    let st = model.select_ids(model.initial(drawing),ids);
    if (kind == 'insert') model.drawing_with(drawing,[*model.objects(drawing),*command.objects])
    else if (kind == 'delete') model.remove(st).doc
    else if (kind == 'duplicate') (
        let copied = model.duplicate(st),
        if (len(ids)>0 and copied.doc == drawing) raise error("Draw: duplication exceeds the object limit") else copied.doc)
    else if (kind == 'move') model.move_selection(drawing,ids,command.dx,command.dy,false)
    else if (kind == 'align') (model.align(st,command.mode)^).doc
    else if (kind == 'front' or kind == 'back') model.reorder(st,kind == 'front').doc
    else if (kind == 'set' or kind == 'reconnect' or kind == 'reverse') (
        let node = present[0].shape,
        let ends = if (kind == 'reconnect') endpoint_values(command.endpoint,command.side)^ else if (kind == 'reverse') {
            'from-shape':model.attr(node,"to-shape"),'to-shape':model.attr(node,"from-shape"),
            'from-port':model.attr(node,"to-port"),'to-port':model.attr(node,"from-port"),
            'from-x':model.attr(node,"to-x"),'from-y':model.attr(node,"to-y"),
            'to-x':model.attr(node,"from-x"),'to-y':model.attr(node,"from-y"),
            *:(if (model.attr(node,"waypoints") != null) {waypoints:reverse(model.attr(node,"waypoints"))} else {}),
            *:(if (model.attr(node,"segments") != null) {segments:paths.reversed(model.attr(node,"segments"))} else {})} else value(command.properties,{}),
        model.drawing_with(drawing,[for (n in model.objects(drawing)) if (contains(ids,model.id(n))) patch(n,ends)^ else n]))
    else raise error("Draw: unknown command " ++ string(kind))
}
pub fn transaction(document,context,command) any^ {
    let path = value(context.drawing_path,[])
    let before = doc.node_at(document,path)
    let after = checked(changed(before,command)^)^;
    if (after == before) null else tx.tx_step(tx.tx_begin(document,context.selection),
        step.step_replace(path,0,len(before.content),after.content))
}
pub fn exec(session,command) any^ {
    if (command.kind == 'undo' or command.kind == 'redo') model.step_history(session,command.kind == 'redo')
    else (
        let change = transaction(session.doc,{drawing_path:[]},command)^,
        model.apply_tx(session,change,session.selected))
}
