// compatibility exports; the drawing package owns the implementation.
import core: lambda.editor.draw.model
import codec: lambda.editor.draw.codec

pub let width = core.width
pub let height = core.height
pub let curve_radius = core.curve_radius
pub let kinds = core.kinds
pub let colors = core.colors
pub let max_objects = core.max_objects
pub let stroke_styles = core.stroke_styles
pub let patterns = core.patterns
pub let arrows = core.arrows
pub let ports = core.ports
pub let routings = core.routings
pub let common_defaults = core.common_defaults
pub let shape_defaults = core.shape_defaults
pub let edge_defaults = core.edge_defaults
pub let numeric_fields = core.numeric_fields
pub fn editable_keys(node) => core.editable_keys(node)
pub fn attr(node,key) => core.attr(node,key)
pub fn id(node) => core.id(node)
pub fn objects(drawing) => core.objects(drawing)
pub fn lookup(drawing,target) => core.lookup(drawing,target)
pub fn box(node) => core.box(node)
pub fn with_attr(node,key,value) => core.with_attr(node,key,value)
pub fn with_attrs(node,values) => core.with_attrs(node,values)
pub fn selected_ids(st) => core.selected_ids(st)
pub fn select_ids(st,ids) => core.select_ids(st,ids)
pub fn turn_point(node,p,flip=true) => core.turn_point(node,p,flip)
pub fn contour(node) => core.contour(node)
pub fn world_box(node) => core.world_box(node)
pub fn shape_handles(node,zoom=1.0) => core.shape_handles(node,zoom)
pub fn selection_box(drawing,ids) => core.selection_box(drawing,ids)
pub fn enclosed(drawing,rectangle) => core.enclosed(drawing,rectangle)
pub fn snap(value,enabled) => core.snap(value,enabled)
pub fn clamp(value,low,high) => core.clamp(value,low,high)
pub fn make_shape(target,kind,x,y,label,fill=colors[0]) => core.make_shape(target,kind,x,y,label,fill)
pub fn edge(target,from,to,label="",routing="orthogonal") => core.edge(target,from,to,label,routing)
pub fn drawing(items) => core.drawing(items)
pub fn apply_tx(st,change,selected) => core.apply_tx(st,change,selected)
pub fn commit_edit(st,before,after) => core.commit_edit(st,before,after)
pub fn step_history(st,redo=false) => core.step_history(st,redo)
pub fn add(st,kind,x,y) => core.add(st,kind,x,y)
pub fn connect(st,target) => core.connect(st,target)
pub fn change(st,key,value) => core.change(st,key,value)
pub fn preview_properties(st,values) any^ => core.preview_properties(st,values)
pub fn edit_properties(st,values) any^ => core.edit_properties(st,values)
pub fn reverse_edge(st) any^ => core.reverse_edge(st)
pub fn copy_style(st) => core.copy_style(st)
pub fn paste_style(st) any^ => core.paste_style(st)
pub fn transform(drawing,target,mode,dx,dy,grid) => core.transform(drawing,target,mode,dx,dy,grid)
pub fn move_selection(drawing,ids,dx,dy,grid) => core.move_selection(drawing,ids,dx,dy,grid)
pub fn nudge(st,dx,dy) => core.nudge(st,dx,dy)
pub fn remove(st) => core.remove(st)
pub fn align(st,mode) any^ => core.align(st,mode)
pub fn orient(st,mode) => core.orient(st,mode)
pub fn rotate_document(drawing,target,p,snapping=false) => core.rotate_document(drawing,target,p,snapping)
pub fn resize_document(drawing,target,dx,dy,grid) => core.resize_document(drawing,target,dx,dy,grid)
pub fn duplicate(st) => core.duplicate(st)
pub fn reorder(st,front) => core.reorder(st,front)
pub fn endpoints(drawing,node) => core.endpoints(drawing,node)
pub fn route(drawing,node) => core.route(drawing,node)
pub fn point_on_route(points,fraction) => core.point_on_route(points,fraction)
pub fn visible_route(drawing,node) => core.visible_route(drawing,node)
pub fn edge_handles(drawing,node) => core.edge_handles(drawing,node)
pub fn handle_at(drawing,node,p,zoom=1.0) => core.handle_at(drawing,node,p,zoom)
pub fn connection_targets(drawing) => core.connection_targets(drawing)
pub fn edit_edge_document(drawing,target,mode,p,drop="") => core.edit_edge_document(drawing,target,mode,p,drop)
pub fn hit_shape(drawing,p) => core.hit_shape(drawing,p)
pub fn hit(drawing,p,tolerance=6.0) => core.hit(drawing,p,tolerance)
pub fn archive(drawing) => core.archive(drawing)
pub fn valid_color(value) => core.valid_color(value)
pub fn valid_node(node,shape_ids) => core.valid_node(node,shape_ids)
pub fn restore(value) any^ => core.restore(value)
pub fn restore_archive(value) any^ {
    let restored = codec.decode(value)^
    let ids = [for (node in objects(restored) where node.tag == 'shape') id(node)];
    // the full package admits pages and objects beyond this demo's fixed inspector profile.
    if (core.page(restored) != {width:width,height:height} or core.object_limit(restored)>max_objects or len(objects(restored))>max_objects or
        not all([for (node in objects(restored)) valid_node(node,ids)])) raise error("Draw: unsupported demo page or objects")
    else restored
}
pub fn open_document(st,value) => core.open_document(st,value)

import shapes: lambda.editor.draw.shapes
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
pub fn initial() => {*:core.initial(starter()),next_id:6,status:"A little structure for your next big idea."}
