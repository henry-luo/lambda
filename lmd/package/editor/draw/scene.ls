import model: .model
import geom: .geometry
import router: .routing
import shapes: .shapes
import paint: .paint

fn rotation(node) {
    let c = geom.rect_center(model.box(node));
    "rotate(" ++ string(model.attr(node,"rotation")) ++ " " ++ string(c.x) ++ " " ++ string(c.y) ++ ")"
}
fn flipping(node) {
    let c = geom.rect_center(model.box(node));
    "translate(" ++ string(c.x) ++ " " ++ string(c.y) ++ ") scale(" ++
        (if (model.attr(node,"flip-x")) "-1" else "1") ++ " " ++ (if (model.attr(node,"flip-y")) "-1" else "1") ++
        ") translate(" ++ string(0-c.x) ++ " " ++ string(0-c.y) ++ ")"
}
fn shape_view(node,source,prefix) {
    let b = model.box(node);
    <g id:prefix ++ "shape-" ++ model.id(node),opacity:model.attr(node,"opacity"),
        <g transform:rotation(node),
            <g class:"shape-graphic",transform:flipping(node),(paint.graphic(node,prefix));(shapes.detail(model.attr(node,"variant"),b,paint.stroke(node)))>
            ; (paint.text(node,b,model.attr(node,"label")))
            ; (if (source == model.id(node)) <rect x:b.x-6,y:b.y-6,width:b.width+12,height:b.height+12,rx:10,
                fill:"none",stroke:"#d3902a",'stroke-width':2,'stroke-dasharray':"5 4"> else null)
        >
    >
}
pub fn edge_view(drawing,node,ids,prefix="") {
    let label = model.attr(node,"label")
    let center = model.point_on_route(model.visible_route(drawing,node),model.attr(node,"label-position"))
    let size = model.attr(node,"font-size")
    let label_width = max(30,len(label)*size*0.6+14)
    let d = model.path_data(drawing,node);
    <g id:prefix ++ "edge-" ++ model.id(node),opacity:model.attr(node,"opacity"),
        (if (contains(ids,model.id(node))) <path d:d,fill:"none",stroke:"#c9d5ff",'stroke-width':model.attr(node,"stroke-width")+5> else null);
        <path class:"connector-line",d:d,fill:"none",*:(paint.stroke(node)),
            'marker-start':if (model.attr(node,"start-arrow") == "none") "none" else "url(#" ++ prefix ++ "arrow-start-" ++ model.id(node) ++ ")",
            'marker-end':if (model.attr(node,"end-arrow") == "none") "none" else "url(#" ++ prefix ++ "arrow-end-" ++ model.id(node) ++ ")">
        ; (if (label != "") <g
            <rect x:center.x-label_width/2,y:center.y-size-8,width:label_width,height:size+10,rx:4,fill:"#ffffff">
            ; (paint.text(node,{x:center.x-label_width/2,y:center.y-size-8,width:label_width,height:size+10},label,true))
        > else null)
    >
}
fn connection_mark(p,zoom,target) => <g id:target,class:"connection-mark",
    <circle cx:p.x,cy:p.y,r:6/zoom,fill:"#20bf55",stroke:"#ffffff",'stroke-width':1/zoom>
    <path d:"M " ++ string(p.x-3/zoom) ++ " " ++ string(p.y-3/zoom) ++ " l " ++ string(6/zoom) ++ " " ++ string(6/zoom) ++
        " M " ++ string(p.x-3/zoom) ++ " " ++ string(p.y+3/zoom) ++ " l " ++ string(6/zoom) ++ " " ++ string(-6/zoom),
        fill:"none",stroke:"#ffffff",'stroke-width':1.3/zoom>
>
pub fn selection_view(drawing,ids,zoom,prefix="") {
    let single = if (len(ids)==1) model.lookup(drawing,ids[0]).shape else null
    let handles = if (single.tag == 'connector') model.edge_handles(drawing,single) else {}
    let group_box = model.selection_box(drawing,ids)
    let r = 6/zoom;
    // controls render after every object, so attached endpoints remain visible and draggable.
    <g id:prefix ++ "selection-overlay",
        *[for (node in model.objects(drawing) where node.tag == 'shape' and contains(ids,model.id(node)))
            (let b = model.box(node), <g transform:rotation(node),
                <rect class:"selection-frame",x:b.x-4/zoom,y:b.y-4/zoom,width:b.width+8/zoom,height:b.height+8/zoom,
                    fill:"none",stroke:"#4c6ef5",'stroke-width':1.2/zoom>
                ; (if (len(ids)==1) <g
                    <path d:"M " ++ string(b.x+b.width/2) ++ " " ++ string(b.y) ++ " v " ++ string(-28/zoom),stroke:"#4c6ef5",'stroke-width':1/zoom>
                    ; *[for (p in [geom.v2(b.x,b.y),geom.v2(b.x+b.width,b.y),geom.v2(b.x,b.y+b.height),geom.v2(b.x+b.width,b.y+b.height)])
                        <rect x:p.x-4/zoom,y:p.y-4/zoom,width:8/zoom,height:8/zoom,fill:"#ffffff",stroke:"#4c6ef5",'stroke-width':1.2/zoom>]
                    ; <circle id:prefix ++ "shape-rotation-handle",cx:b.x+b.width/2,cy:b.y-28/zoom,r:5/zoom,fill:"#ffffff",stroke:"#4c6ef5",'stroke-width':1.5/zoom>
                > else null)
            >)]
        ; (if (single.tag == 'connector') <g
            *[for (handle in model.segment_handles(drawing,single,zoom)) <circle id:prefix ++ "edge-segment-" ++ string(handle.index),
                class:"segment-handle",cx:handle.point.x,cy:handle.point.y,r:3.5/zoom,fill:"#81dafa",stroke:"#ffffff",'stroke-width':1/zoom>]
            ; <rect id:prefix ++ "edge-bend-handle",x:handles.bend.x-5/zoom,y:handles.bend.y-5/zoom,width:10/zoom,height:10/zoom,fill:"#fff1d6",stroke:"#d3902a",'stroke-width':1.5/zoom>
            <circle id:prefix ++ "edge-from-handle",cx:handles.from.x,cy:handles.from.y,r:r,fill:"#ffffff",stroke:"#4c6ef5",'stroke-width':2/zoom>
            <circle id:prefix ++ "edge-to-handle",cx:handles.to.x,cy:handles.to.y,r:r,fill:"#ffffff",stroke:"#4c6ef5",'stroke-width':2/zoom>
            ; *[for (side in ["from","to"] where model.attr(single,side ++ "-shape") != "")
                connection_mark(handles[side],zoom,prefix ++ "edge-" ++ side ++ "-connection")]
        > else null)
        ; (if (len(ids)>1) <rect id:prefix ++ "group-selection-frame",x:group_box.x-8/zoom,y:group_box.y-8/zoom,width:group_box.width+16/zoom,height:group_box.height+16/zoom,
            fill:"none",stroke:"#4c6ef5",'stroke-width':1/zoom,'stroke-dasharray':string(5/zoom) ++ " " ++ string(4/zoom)> else null)
    >
}
// retained hosts replace only a changed object's children, preserving unrelated SVG nodes.
pub fn inner_xml(graphic) => join([for (child in content(graphic)) format(child,'xml')],"")
pub fn svg(drawing,selected="",source="",grid=false,standalone=false,selection=[],zoom=1.0,marquee=null,prefix="") {
    let ids = if (len(selection)>0) selection else if (selected != "") [selected] else [];
    <svg xmlns:"http://www.w3.org/2000/svg",width:if (standalone) string(model.page(drawing).width) else "100%",
        height:if (standalone) string(model.page(drawing).height) else "100%",viewBox:"0 0 " ++ string(model.page(drawing).width) ++ " " ++ string(model.page(drawing).height),role:"img",'aria-label':"Diagram",
        <defs *[for (node in model.objects(drawing) where node.tag == 'shape' and model.attr(node,"pattern") != "solid") paint.pattern(node,prefix)]
            ; *[for (node in model.objects(drawing) where node.tag == 'connector') <g (paint.marker(node,"start",prefix));(paint.marker(node,"end",prefix))>]>
        <rect width:model.page(drawing).width,height:model.page(drawing).height,fill:"#ffffff">
        ; (if (grid) <g stroke:"#eef1f6",'stroke-width':0.6,
            *[for (x in 0 to int(model.page(drawing).width/20)) <path d:"M " ++ string(x*20) ++ " 0 V " ++ string(model.page(drawing).height)>]
            ; *[for (y in 0 to int(model.page(drawing).height/20)) <path d:"M 0 " ++ string(y*20) ++ " H " ++ string(model.page(drawing).width)>]
        > else null)
        ; *[for (node in model.objects(drawing) where node.tag == 'connector') edge_view(drawing,node,ids,prefix)]
        ; (if (not standalone) <g id:prefix ++ "connector-draft"> else null)
        ; *[for (node in model.objects(drawing) where node.tag == 'shape') shape_view(node,source,prefix)]
        ; (if (not standalone or len(ids)>0) selection_view(drawing,ids,zoom,prefix) else null)
        ; (if (marquee != null) <rect id:prefix ++ "marquee",x:marquee.x,y:marquee.y,width:marquee.width,height:marquee.height,
            fill:"#4c6ef5",'fill-opacity':0.1,stroke:"#4c6ef5",'stroke-width':1/zoom,'stroke-dasharray':string(4/zoom) ++ " " ++ string(3/zoom)> else null)
    >
}
