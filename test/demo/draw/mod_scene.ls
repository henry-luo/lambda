import model: .mod_model
import geom: lambda.editor.mod_geom
import router: lambda.editor.mod_router

fn point_list(points) => join([for (p in points) string(p.x) ++ "," ++ string(p.y)]," ")
fn shape_graphic(node) {
    let b = model.box(node)
    let style = {fill:model.attr(node,"fill"),stroke:model.attr(node,"stroke"),'stroke-width':1.5};
    if (model.attr(node,"variant") == "diamond") <polygon points:point_list(model.diamond_points(b)),*:style>
    else if (model.attr(node,"kind") == 'ellipse') <ellipse cx:b.x+b.width/2,cy:b.y+b.height/2,rx:b.width/2,ry:b.height/2,*:style>
    else <rect x:b.x,y:b.y,width:b.width,height:b.height,rx:8,*:style>
}
fn shape_view(node,selected,source) {
    let b = model.box(node)
    let lines = split(model.attr(node,"label"),"\n");
    <g id:"shape-" ++ model.id(node),
        shape_graphic(node);
        *[for (i,line in lines) <text x:b.x+b.width/2,y:b.y+b.height/2+5+(i-(len(lines)-1)/2)*18,
            'font-family':"Arial, sans-serif",'font-size':14,'text-anchor':"middle",fill:"#29354c",line>]
        ; (if (source == model.id(node)) <rect x:b.x-6,y:b.y-6,width:b.width+12,height:b.height+12,rx:10,
            fill:"none",stroke:"#d3902a",'stroke-width':2,'stroke-dasharray':"5 4"> else null)
        ; (if (selected == model.id(node)) <g
            <rect x:b.x-4,y:b.y-4,width:b.width+8,height:b.height+8,rx:4,fill:"none",stroke:"#4c6ef5",'stroke-width':1.5>
            ; *[for (p in [geom.v2(b.x,b.y),geom.v2(b.x+b.width,b.y),geom.v2(b.x,b.y+b.height),geom.v2(b.x+b.width,b.y+b.height)])
                <rect x:p.x-4,y:p.y-4,width:8,height:8,fill:"#ffffff",stroke:"#4c6ef5",'stroke-width':1.5>]
        > else null)
    >
}
fn edge_view(drawing,node,selected) {
    let points = model.route(drawing,node)
    let tip = points[last]
    let prev = points[len(points)-2]
    let direction = geom.v2_sub(tip,prev)
    let length = max(0.001,geom.v2_len(direction))
    let ux = direction.x/length
    let uy = direction.y/length
    let arrow = [tip,geom.v2(tip.x-10*ux+4*uy,tip.y-10*uy-4*ux),geom.v2(tip.x-10*ux-4*uy,tip.y-10*uy+4*ux)]
    let middle = points[int(floor((len(points)-1)/2))]
    let following = points[int(floor((len(points)-1)/2))+1]
    let label = model.attr(node,"label")
    let color = if (selected == model.id(node)) "#4c6ef5" else model.attr(node,"stroke");
    <g id:"edge-" ++ model.id(node),
        <path d:router.route_to_svg_path(points),fill:"none",stroke:color,'stroke-width':if (selected == model.id(node)) 2.5 else 1.7>
        <polygon points:point_list(arrow),fill:color>
        ; (if (label != "") <g
            <rect x:(middle.x+following.x)/2-len(label)*4-7,y:(middle.y+following.y)/2-22,
                width:len(label)*8+14,height:22,rx:4,fill:"#ffffff">
            <text x:(middle.x+following.x)/2,y:(middle.y+following.y)/2-7,'text-anchor':"middle",
                'font-family':"Arial, sans-serif",'font-size':12,fill:"#778195",label>
        > else null)
    >
}
pub fn svg(drawing,selected="",source="",grid=false,standalone=false) =>
    <svg xmlns:"http://www.w3.org/2000/svg",width:if (standalone) string(model.width) else "100%",
        height:if (standalone) string(model.height) else "100%",viewBox:"0 0 1120 760",role:"img",'aria-label':"Diagram",
        <rect width:model.width,height:model.height,fill:"#ffffff">
        ; (if (grid) <g stroke:"#eef1f6",'stroke-width':0.6,
            *[for (x in 0 to 56) <path d:"M " ++ string(x*20) ++ " 0 V 760">]
            ; *[for (y in 0 to 38) <path d:"M 0 " ++ string(y*20) ++ " H 1120">]
        > else null)
        ; *[for (node in model.objects(drawing) where node.tag == 'connector') edge_view(drawing,node,selected)]
        ; *[for (node in model.objects(drawing) where node.tag == 'shape') shape_view(node,selected,source)]
    >

fn icon_path(kind) => if (kind == "select") "M 5 3 L 19 13 L 12 14 L 9 21 Z"
    else if (kind == "connect") "M 4 17 H 10 V 7 H 20 M 16 3 L 20 7 L 16 11"
    else if (kind == "pan") "M 12 3 V 21 M 3 12 H 21 M 8 7 L 12 3 L 16 7 M 8 17 L 12 21 L 16 17 M 7 8 L 3 12 L 7 16 M 17 8 L 21 12 L 17 16"
    else if (kind == "diamond") "M 12 3 L 22 12 L 12 21 L 2 12 Z"
    else if (kind == "undo") "M 8 5 L 3 10 L 8 15 M 3 10 H 14 Q 21 10 21 17 V 20"
    else if (kind == "redo") "M 16 5 L 21 10 L 16 15 M 21 10 H 10 Q 3 10 3 17 V 20"
    else if (kind == "duplicate") "M 8 8 H 21 V 21 H 8 Z M 16 8 V 3 H 3 V 16 H 8"
    else if (kind == "delete") "M 4 6 H 20 M 9 6 V 3 H 15 V 6 M 6 6 L 7 21 H 17 L 18 6 M 10 10 V 17 M 14 10 V 17"
    else "M 5 5 H 19 V 19 H 5 Z"
pub fn icon(kind) => <svg width:"20",height:"20",viewBox:"0 0 24 24",'aria-hidden':"true",
    (if (kind == "ellipse") <ellipse cx:12,cy:12,rx:9,ry:6,fill:"none",stroke:"currentColor",'stroke-width':1.5>
    else <path d:icon_path(kind),fill:"none",stroke:"currentColor",'stroke-width':1.5,'stroke-linejoin':"round",'stroke-linecap':"round">)>
pub fn button(target,label,action,graphic="") => <button id:target,type:"button",'data-action':action,title:label,'aria-label':label,
    (if (graphic != "") (icon(graphic)) else null);<span label>>
fn field(key,label,kind="text") => <label class:"field",'for':"prop-" ++ key,<span label>
    <input id:"prop-" ++ key,type:kind,'data-property':key>>
pub fn tree() => <main id:"draw",'data-error':"",
    <header class:"topbar",
        <div class:"brand",<div class:"brand-mark",(icon("connect"))><h1 "Draw"><span class:"native-badge","LAMBDA">>
        <div class:"document-title",<strong "Untitled diagram"><span id:"save-state","All changes saved">>
        <div class:"file-actions",(button("open","Open","open"));(button("save","Save","save"));(button("export","Export SVG","export"))>
    >
    <nav class:"toolbar",'aria-label':"Diagram tools",
        <div class:"tool-group",(button("tool-select","Select","tool-select","select"));(button("tool-connect","Connect","tool-connect","connect"));(button("tool-pan","Pan","tool-pan","pan"))>
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
            <div class:"shape-library",*[for (kind in model.kinds) <button id:"add-" ++ kind,type:"button",'data-action':"add-" ++ kind,
                (icon(kind));<strong (if (kind == "rect") "Process" else if (kind == "ellipse") "Start / End" else "Decision")>>
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
                <label class:"field",'for':"prop-label",<span "Label"><textarea id:"prop-label",rows:"3",maxlength:"200",'data-property':"label">>
                <div id:"shape-properties",
                    <p class:"property-heading","Fill color"><div class:"swatches",*[for (i,color in model.colors)
                        <button id:"color-" ++ string(i),type:"button",'data-color':color,style:"background:" ++ color,
                            title:color,'aria-label':"Fill " ++ color>]>
                    <p class:"property-heading","Position & size"><div class:"geometry-fields",*[
                        field("x","X","number"),field("y","Y","number"),field("width","Width","number"),field("height","Height","number")]>
                    <p class:"property-heading","Arrange"><div class:"arrange-actions",(button("back","To back","back"));(button("front","To front","front"))>
                >
                <div id:"edge-properties",style:"display:none",<label class:"field",'for':"prop-routing",<span "Connector style">
                    <select id:"prop-routing",'data-property':"routing",<option value:"orthogonal","Elbow"><option value:"straight","Straight">>>>
                ; (button("apply-properties","Apply changes","apply-properties"))
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
