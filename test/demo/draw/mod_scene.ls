import model: .mod_model
import geom: lambda.editor.mod_geom
import router: lambda.editor.mod_router
import shapes: .mod_shapes
import paint: .mod_paint

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
fn shape_view(node,source) {
    let b = model.box(node);
    <g id:"shape-" ++ model.id(node),opacity:model.attr(node,"opacity"),
        <g transform:rotation(node),
            <g class:"shape-graphic",transform:flipping(node),(paint.graphic(node));(shapes.detail(model.attr(node,"variant"),b,paint.stroke(node)))>
            ; (paint.text(node,b,model.attr(node,"label")))
            ; (if (source == model.id(node)) <rect x:b.x-6,y:b.y-6,width:b.width+12,height:b.height+12,rx:10,
                fill:"none",stroke:"#d3902a",'stroke-width':2,'stroke-dasharray':"5 4"> else null)
        >
    >
}
fn edge_view(drawing,node,ids) {
    let points = model.route(drawing,node)
    let label = model.attr(node,"label")
    let center = model.point_on_route(model.visible_route(drawing,node),model.attr(node,"label-position"))
    let size = model.attr(node,"font-size")
    let label_width = max(30,len(label)*size*0.6+14)
    let d = if (model.attr(node,"routing") == 'curved') router.route_to_curved_svg_path(points,model.curve_radius) else router.route_to_svg_path(points);
    <g id:"edge-" ++ model.id(node),opacity:model.attr(node,"opacity"),
        (if (contains(ids,model.id(node))) <path d:d,fill:"none",stroke:"#c9d5ff",'stroke-width':model.attr(node,"stroke-width")+5> else null);
        <path class:"connector-line",d:d,fill:"none",*:(paint.stroke(node)),
            'marker-start':if (model.attr(node,"start-arrow") == "none") "none" else "url(#arrow-start-" ++ model.id(node) ++ ")",
            'marker-end':if (model.attr(node,"end-arrow") == "none") "none" else "url(#arrow-end-" ++ model.id(node) ++ ")">
        ; (if (label != "") <g
            <rect x:center.x-label_width/2,y:center.y-size-8,width:label_width,height:size+10,rx:4,fill:"#ffffff">
            ; (paint.text(node,{x:center.x-label_width/2,y:center.y-size-8,width:label_width,height:size+10},label,true))
        > else null)
    >
}
fn selection_view(drawing,ids,zoom) {
    let single = if (len(ids)==1) model.lookup(drawing,ids[0]).shape else null
    let handles = if (single.tag == 'connector') model.edge_handles(drawing,single) else {}
    let group_box = model.selection_box(drawing,ids)
    let r = 6/zoom;
    // controls render after every object, so attached endpoints remain visible and draggable.
    <g id:"selection-overlay",
        *[for (node in model.objects(drawing) where node.tag == 'shape' and contains(ids,model.id(node)))
            (let b = model.box(node), <g transform:rotation(node),
                <rect class:"selection-frame",x:b.x-4/zoom,y:b.y-4/zoom,width:b.width+8/zoom,height:b.height+8/zoom,
                    fill:"none",stroke:"#4c6ef5",'stroke-width':1.2/zoom>
                ; (if (len(ids)==1) <g
                    <path d:"M " ++ string(b.x+b.width/2) ++ " " ++ string(b.y) ++ " v " ++ string(-28/zoom),stroke:"#4c6ef5",'stroke-width':1/zoom>
                    ; *[for (p in [geom.v2(b.x,b.y),geom.v2(b.x+b.width,b.y),geom.v2(b.x,b.y+b.height),geom.v2(b.x+b.width,b.y+b.height)])
                        <rect x:p.x-4/zoom,y:p.y-4/zoom,width:8/zoom,height:8/zoom,fill:"#ffffff",stroke:"#4c6ef5",'stroke-width':1.2/zoom>]
                    ; <circle id:"shape-rotation-handle",cx:b.x+b.width/2,cy:b.y-28/zoom,r:5/zoom,fill:"#ffffff",stroke:"#4c6ef5",'stroke-width':1.5/zoom>
                > else null)
            >)]
        ; (if (single.tag == 'connector') <g
            <rect id:"edge-bend-handle",x:handles.bend.x-5/zoom,y:handles.bend.y-5/zoom,width:10/zoom,height:10/zoom,fill:"#fff1d6",stroke:"#d3902a",'stroke-width':1.5/zoom>
            <circle id:"edge-from-handle",cx:handles.from.x,cy:handles.from.y,r:r,fill:"#ffffff",stroke:"#4c6ef5",'stroke-width':2/zoom>
            <circle id:"edge-to-handle",cx:handles.to.x,cy:handles.to.y,r:r,fill:"#ffffff",stroke:"#4c6ef5",'stroke-width':2/zoom>
        > else null)
        ; (if (len(ids)>1) <rect id:"group-selection-frame",x:group_box.x-8/zoom,y:group_box.y-8/zoom,width:group_box.width+16/zoom,height:group_box.height+16/zoom,
            fill:"none",stroke:"#4c6ef5",'stroke-width':1/zoom,'stroke-dasharray':string(5/zoom) ++ " " ++ string(4/zoom)> else null)
    >
}
pub fn svg(drawing,selected="",source="",grid=false,standalone=false,selection=[],zoom=1.0,marquee=null) {
    let ids = if (len(selection)>0) selection else if (selected != "") [selected] else [];
    <svg xmlns:"http://www.w3.org/2000/svg",width:if (standalone) string(model.width) else "100%",
        height:if (standalone) string(model.height) else "100%",viewBox:"0 0 " ++ string(model.width) ++ " " ++ string(model.height),role:"img",'aria-label':"Diagram",
        <defs *[for (node in model.objects(drawing) where node.tag == 'shape' and model.attr(node,"pattern") != "solid") paint.pattern(node)]
            ; *[for (node in model.objects(drawing) where node.tag == 'connector') <g (paint.marker(node,"start"));(paint.marker(node,"end"))>]>
        <rect width:model.width,height:model.height,fill:"#ffffff">
        ; (if (grid) <g stroke:"#eef1f6",'stroke-width':0.6,
            *[for (x in 0 to int(model.width/20)) <path d:"M " ++ string(x*20) ++ " 0 V " ++ string(model.height)>]
            ; *[for (y in 0 to int(model.height/20)) <path d:"M 0 " ++ string(y*20) ++ " H " ++ string(model.width)>]
        > else null)
        ; *[for (node in model.objects(drawing) where node.tag == 'connector') edge_view(drawing,node,ids)]
        ; *[for (node in model.objects(drawing) where node.tag == 'shape') shape_view(node,source)]
        ; (if (len(ids)>0) selection_view(drawing,ids,zoom) else null)
        ; (if (marquee != null) <rect id:"marquee",x:marquee.x,y:marquee.y,width:marquee.width,height:marquee.height,
            fill:"#4c6ef5",'fill-opacity':0.1,stroke:"#4c6ef5",'stroke-width':1/zoom,'stroke-dasharray':string(4/zoom) ++ " " ++ string(3/zoom)> else null)
    >
}

fn icon_path(kind) => if (kind == "select") "M 5 3 L 19 13 L 12 14 L 9 21 Z"
    else if (kind == "marquee") "M 3 8 V 3 H 8 M 16 3 H 21 V 8 M 21 16 V 21 H 16 M 8 21 H 3 V 16"
    else if (kind == "connect") "M 4 17 H 10 V 7 H 20 M 16 3 L 20 7 L 16 11"
    else if (kind == "pan") "M 12 3 V 21 M 3 12 H 21 M 8 7 L 12 3 L 16 7 M 8 17 L 12 21 L 16 17 M 7 8 L 3 12 L 7 16 M 17 8 L 21 12 L 17 16"
    else if (kind == "diamond") "M 12 3 L 22 12 L 12 21 L 2 12 Z"
    else if (kind == "undo") "M 8 5 L 3 10 L 8 15 M 3 10 H 14 Q 21 10 21 17 V 20"
    else if (kind == "redo") "M 16 5 L 21 10 L 16 15 M 21 10 H 10 Q 3 10 3 17 V 20"
    else if (kind == "duplicate") "M 8 8 H 21 V 21 H 8 Z M 16 8 V 3 H 3 V 16 H 8"
    else if (kind == "delete") "M 4 6 H 20 M 9 6 V 3 H 15 V 6 M 6 6 L 7 21 H 17 L 18 6 M 10 10 V 17 M 14 10 V 17"
    else "M 5 5 H 19 V 19 H 5 Z"
pub fn icon(kind) => <svg width:"20",height:"20",viewBox:"0 0 24 24",'aria-hidden':"true",
    (if (contains(model.kinds,kind)) shapes.graphic(kind,{x:2,y:4,width:20,height:16},{fill:"none",stroke:"currentColor",'stroke-width':1.5})
    else <path d:icon_path(kind),fill:"none",stroke:"currentColor",'stroke-width':1.5,'stroke-linejoin':"round",'stroke-linecap':"round">)>
pub fn button(target,label,action,graphic="") => <button id:target,type:"button",'data-action':action,title:label,'aria-label':label,
    (if (graphic != "") (icon(graphic)) else null);<span label>>
fn field(key,label,kind="text") => <label class:"field",'for':"prop-" ++ key,<span label>
    <input id:"prop-" ++ key,type:kind,'data-property':key>>
fn select_field(key,label,values) => <label class:"field",'for':"prop-" ++ key,<span label>
    <select id:"prop-" ++ key,'data-property':key,*[for (value in values) <option value:if (value is map) value.id else value,
        (if (value is map) value.label else upper(slice(value,0,1)) ++ replace(slice(value,1),"-"," "))>]>>
pub fn tree() => <main id:"draw",'data-error':"",
    <header class:"topbar",
        <div class:"brand",<div class:"brand-mark",(icon("connect"))><h1 "Draw"><span class:"native-badge","LAMBDA">>
        <div class:"document-title",<strong "Untitled diagram"><span id:"save-state","All changes saved">>
        <div class:"file-actions",(button("open","Open","open"));(button("save","Save","save"));(button("export","Export SVG","export"))>
    >
    <nav class:"toolbar",'aria-label':"Diagram tools",
        <div class:"tool-group",(button("tool-select","Select (V)","tool-select","select"));(button("tool-marquee","Rectangle select (M)","tool-marquee","marquee"));(button("tool-connect","Connect","tool-connect","connect"));(button("tool-pan","Pan","tool-pan","pan"))>
        <span class:"divider">
        <div class:"tool-group",(button("undo","Undo","undo","undo"));(button("redo","Redo","redo","redo"))>
        <span class:"divider">
        <div class:"tool-group",(button("duplicate","Duplicate","duplicate","duplicate"));(button("delete","Delete","delete","delete"))>
        <div class:"toolbar-end",(button("grid","Snap & grid","grid"));(button("fit","Fit page","fit"));
            (button("zoom-out","−","zoom-out"));<span id:"zoom-label","100%">;(button("zoom-in","+","zoom-in"))>
    >
    <div class:"workspace",
        <aside class:"library",
            <p class:"eyebrow","SHAPE LIBRARY"><h2 "Start with a shape."><p class:"muted","Choose a shape, then click on the page to place it.">
            <label class:"field object-picker",'for':"object-list",<span "Objects on this page"><select id:"object-list">>
            ; (button("showcase","Style showcase","showcase"));
            <div class:"shape-library",*[for (entry in shapes.catalog) <button id:"add-" ++ entry.id,type:"button",'data-action':"add-" ++ entry.id,
                (icon(entry.id));<strong (entry.label)>>
            ]>
            <div class:"library-section",<p class:"eyebrow","MAKE CONNECTIONS"><p "Select Connect, then click a source shape and a destination.">;
                (button("connect-help","Connect shapes","tool-connect","connect"))>
            <div class:"library-section",<p class:"eyebrow","A FRESH PAGE">;(button("blank","New blank diagram","blank"));(button("example","Load example","example"))>
            <div class:"library-note",<strong "Ideas, connected."><p "Flowcharts, little plans, and the big picture."><span "BUILT WITH LAMBDA + RADIANT">>
        >
        <section class:"canvas-column",
            <div class:"canvas-caption",<span "DIAGRAM 01"><span "1120 × 760">>
            <div id:"stage",tabindex:"0",role:"region",'aria-label':"Diagram canvas. Arrow keys move the selected shape.",
                <div id:"paper">>
            <div class:"canvas-help",<span id:"tool-hint","Drag shapes to move · Drag the bottom-right handle to resize"><span "Esc cancels · Delete removes">>
        >
        <aside class:"inspector",
            <p class:"eyebrow","INSPECTOR"><h2 id:"selection-title","The big picture">
            <div id:"empty-selection",<div class:"empty-icon",(icon("select"))><h3 "Make it yours."><p "Select a shape or connector to edit its label, color, and position.">>
            <div id:"properties",style:"display:none",
                <div class:"style-actions",(button("copy-style","Copy style","copy-style"));(button("paste-style","Paste style","paste-style"))>
                <p class:"control-note","Changes apply automatically.">;
                <label class:"field",'for':"prop-label",<span "Label"><textarea id:"prop-label",rows:"3",maxlength:"200",'data-property':"label">>
                <div id:"group-properties",style:"display:none",
                    <p class:"property-heading","Align selected shapes">
                    <div class:"alignment-actions",*[for (entry in [{id:"left",label:"Left"},{id:"center",label:"Center"},{id:"right",label:"Right"},
                        {id:"top",label:"Top"},{id:"middle",label:"Middle"},{id:"bottom",label:"Bottom"}]) button("align-" ++ entry.id,entry.label,"align-" ++ entry.id)]>
                    <div class:"arrange-actions",(button("align-distribute-x","Space H","align-distribute-x"));(button("align-distribute-y","Space V","align-distribute-y"))>
                >
                <div id:"shape-properties",
                    (select_field("variant","Shape",shapes.catalog));
                    <p class:"property-heading","Fill color"><div class:"swatches",*[for (i,color in model.colors)
                        <button id:"color-" ++ string(i),type:"button",'data-color':color,style:"background:" ++ color,
                            title:color,'aria-label':"Fill " ++ color>]>
                    ; (field("fill","Fill hex color (or none)"));
                    (select_field("pattern","Pattern",model.patterns));
                    (field("pattern-color","Pattern hex color"));
                    <p class:"property-heading","Position & size"><div class:"geometry-fields",*[
                        field("x","X","number"),field("y","Y","number"),field("width","Width","number"),field("height","Height","number")]>
                    <p class:"property-heading","Orientation">;
                    (field("rotation","Rotation (degrees)","number"));
                    <div class:"orientation-actions",(button("rotate","Rotate 90°","rotate"));(button("flip-x","Flip H","flip-x"));(button("flip-y","Flip V","flip-y"))>
                    <p class:"property-heading","Arrange"><div class:"arrange-actions",(button("back","To back","back"));(button("front","To front","front"))>
                >
                <div id:"edge-properties",style:"display:none",
                    (select_field("routing","Connector style",model.routings));
                    <div class:"geometry-fields",(select_field("start-arrow","Start arrowhead",model.arrows));(select_field("end-arrow","End arrowhead",model.arrows))>
                    ;
                    (select_field("from-shape","From shape",[]));(select_field("from-port","Source attachment",model.ports));
                    (select_field("to-shape","To shape",[]));(select_field("to-port","Destination attachment",model.ports));
                    (field("label-position","Label position (0–1)","number"));
                    <div class:"arrange-actions",(button("reverse-edge","Reverse","reverse-edge"));(button("reset-bend","Reset bend","reset-bend"))>
                    <p class:"control-note","Drag blue endpoints anywhere or onto a shape to attach. Drag the gold handle to edit the route.">
                >
                ; <p class:"property-heading","Outline & line">
                ; (field("stroke","Outline hex color"));(select_field("stroke-style","Line style",model.stroke_styles));
                <div class:"geometry-fields",(field("stroke-width","Weight (px)","number"));(field("opacity","Opacity (0.1–1)","number"))>
                <p class:"property-heading","Text formatting">
                ; (field("font-size","Text size (px)","number"));(select_field("font-weight","Text weight",["normal","bold"]));
                (select_field("text-align","Text alignment",["left","center","right"]));(field("text-color","Text hex color"))
            >
            <div class:"inspector-foot",<p class:"eyebrow","ON THIS PAGE"><strong id:"object-count","5 shapes · 4 connectors"><p "Your diagram stays on this device. Save a JSON file to pick up later.">>
        >
    >
    <footer <span class:"status-dot"><span id:"status",role:"status",'aria-live':"polite","Ready"><span id:"history-label","0 edits">>
    <div id:"file-dialog",class:"dialog-backdrop",style:"display:none",
        <section class:"file-dialog",role:"dialog",'aria-modal':"true",'aria-labelledby':"dialog-title",
            <p class:"eyebrow","YOUR DIAGRAM"><h2 id:"dialog-title","Save diagram">
            <p id:"dialog-copy","Save an editable diagram to a JSON file.">
            <label class:"field",'for':"file-path",<span "File path"><input id:"file-path",type:"text",value:"temp/draw/diagram.json">>
            <p id:"dialog-error",role:"alert">
            <div class:"dialog-actions",(button("file-cancel","Cancel","close-dialog"));(button("file-confirm","Save file","file-confirm"))>
        >
    >
>
