// pure SVG and chrome; handlers retain the stage and native control owners (S12.1.3).
import m: .mod_model
import geom: lambda.editor.draw.geometry

pub fn n(v) => string(round(v*100)/100)
let ICONS = {
    select:"M5 3L19 13 12 14 9 21Z",nodes:"M5 18L10 6 19 16M3 16H7V20H3ZM8 4H12V8H8ZM17 14H21V18H17Z",
    marquee:"M3 8V3H8M16 3H21V8M21 16V21H16M8 21H3V16",
    brush:"M14 4L20 10 10 20 4 20 4 14ZM12 6L18 12M3 22H10",pencil:"M4 16L16 4 20 8 8 20 3 21ZM13 7L17 11",
    pen:"M12 3L20 16 12 21 4 16ZM12 3V12M10 14A2 2 0 1 0 14 14 2 2 0 1 0 10 14",
    rect:"M4 4H20V20H4Z",ellipse:"M20 12A8 8 0 1 1 4 12 8 8 0 1 1 20 12",line:"M4 20L20 4",
    text:"M4 5H20M12 5V21M8 21H16",eraser:"M3 15L14 4 21 11 10 22H8ZM7 11L14 18M10 22H22",
    eyedropper:"M14 3L21 10M16 5L4 17 3 21 7 20 19 8M6 15L9 18",
    pan:"M7 12V6A2 2 0 0 1 11 6V11M11 10V4A2 2 0 0 1 15 4V11M15 10V6A2 2 0 0 1 19 6V12M19 11A2 2 0 0 1 23 11V16C23 23 10 24 7 19L3 14A2 2 0 0 1 6 12L9 15",
    undo:"M8 5L3 10 8 15M3 10H14Q21 10 21 18",redo:"M16 5L21 10 16 15M21 10H10Q3 10 3 18",
    eye:"M2 12Q12 0 22 12Q12 24 2 12ZM9 12A3 3 0 1 0 15 12 3 3 0 1 0 9 12",
    lock:"M7 10V7A5 5 0 0 1 17 7V10M5 10H19V21H5Z",unlock:"M7 10V7A5 5 0 0 1 17 7M5 10H19V21H5Z",
    plus:"M12 5V19M5 12H19",up:"M5 15L12 8 19 15",down:"M5 9L12 16 19 9",
    duplicate:"M8 8H21V21H8ZM16 8V3H3V16H8",delete:"M4 6H20M9 6V3H15V6M6 6L7 21H17L18 6M10 10V17M14 10V17",
    fit:"M3 8V3H8M16 3H21V8M21 16V21H16M8 21H3V16M7 7H17V17H7Z",
    save:"M4 3H18L21 6V21H3V3ZM7 3V10H17V3M7 21V14H17V21",
    export:"M12 16V3M7 8L12 3 17 8M4 14V21H20V14",grid:"M4 4H20V20H4ZM4 9H20M4 15H20M9 4V20M15 4V20",
    image:"M3 3H21V21H3ZM3 17L9 11 14 16 17 13 21 17M14 7H17V10H14Z",
    sketch:"M4 18L9 6 14 18 19 6M3 21H21"
}
pub fn icon(kind) => <svg width:20,height:20,viewBox:"0 0 24 24",'aria-hidden':"true",
    <path d:ICONS[kind],fill:"none",stroke:"currentColor",'stroke-width':1.6,'stroke-linecap':"round",'stroke-linejoin':"round">>
pub fn button(id,label,action,glyph="",extra={}) => <button id:id,type:"button",'data-action':action,title:label,'aria-label':label,*:extra,
    if (glyph != "") (icon(glyph)); if (glyph == "") <span label>>
fn field(key,label,kind="text",more={}) => <label class:"field",'for':"prop-" ++ key,<span label>
    <input id:"prop-" ++ key,type:kind,'data-property':key,*:more>>
fn heading(label,tail="") => <div class:"section-heading",<h2 label><span tail>>

pub let CONTEXT_ITEMS = [
    {id:"undo",label:"Undo",key:"Ctrl/Cmd Z",group:"history"},
    {id:"redo",label:"Redo",key:"Shift Z",group:"history"},
    {id:"cut",label:"Cut",key:"Ctrl/Cmd X",group:"clipboard"},
    {id:"copy",label:"Copy",key:"Ctrl/Cmd C",group:"clipboard"},
    {id:"paste",label:"Paste",key:"Ctrl/Cmd V",group:"clipboard"},
    {id:"paste-here",label:"Paste here",key:"",group:"clipboard"},
    {id:"import-image",label:"Import image…",key:"Shift Ctrl/Cmd I",group:"clipboard"},
    {id:"duplicate",label:"Duplicate",key:"Ctrl/Cmd D",group:"selection"},
    {id:"front",label:"Bring to front",key:"",group:"selection"},
    {id:"back",label:"Send to back",key:"",group:"selection"},
    {id:"delete",label:"Delete",key:"Delete",group:"selection"},
    {id:"select-all",label:"Select all",key:"Ctrl/Cmd A",group:"view"},
    {id:"deselect",label:"Deselect",key:"Esc",group:"view"},
    {id:"fit",label:"Fit artboard",key:"0",group:"view"}
]
pub fn context_enabled(st,id) => if (id=="undo" or id=="redo") len(st.hist[id])>0
    else if (id=="paste" or id=="paste-here") (if (st.context.image==true) m.can_add_image(st) else m.can_paste(st))
    else if (id=="import-image") m.can_add_image(st)
    else if (id=="select-all") len(m.selected_ids(m.select_ids(st,[for (o in m.objects(st.doc)) o.id])))>0
    else if (id=="fit") true
    else len(m.selected_ids(st))>0 and (id!="duplicate" or len(m.objects(st.doc))+len(m.selected_ids(st))<=1000)
pub fn context_menu(st) => <div id:"sketch-context-menu",class:"context-menu",role:"menu",'aria-label':"Canvas actions",
    style:"left:" ++ n(st.context.x) ++ "px;top:" ++ n(st.context.y) ++ "px",
    <p id:"context-label",class:"context-label",if (len(m.selected_ids(st))>0) string(len(m.selected_ids(st))) ++ " selected" else "Canvas actions">
    for (i,entry in CONTEXT_ITEMS) [
        if (i>0 and CONTEXT_ITEMS[i-1].group!=entry.group) <div class:"context-divider",role:"separator">,
        <button id:"context-" ++ entry.id,type:"button",role:"menuitem",tabindex:"-1",'data-action':entry.id,
            *:if (context_enabled(st,entry.id)) map() else {disabled:""},
            class:if (entry.id=="delete") "context-item danger" else "context-item",
            <span entry.label><small entry.key>>
    ]>

pub fn graphic(o,prefix="object-") => <g id:prefix ++ o.id,'data-kind':o.kind,opacity:n(o.opacity),
    if (o.kind == "text") <text x:0,y:n(o.font_size),fill:o.fill,
        transform:"translate(" ++ n(o.x) ++ " " ++ n(o.y) ++ ") scale(" ++ n(o.width/max(24.0,len(o.text)*o.font_size*.62)) ++ " " ++ n(o.height/(o.font_size*1.25)) ++ ")",
        'font-family':"Helvetica Neue, Arial, sans-serif",'font-weight':600,'font-size':n(o.font_size),o.text>
    else if (o.kind == "image") <image x:n(o.x),y:n(o.y),width:n(o.width),height:n(o.height),href:o.src,preserveAspectRatio:"none">
    else if (o.kind == "rect") <rect x:n(o.x),y:n(o.y),width:n(o.width),height:n(o.height),
        fill:o.fill,stroke:o.stroke,'stroke-width':n(o.size)>
    else if (o.kind == "ellipse") <ellipse cx:n(o.x+o.width/2),cy:n(o.y+o.height/2),rx:n(o.width/2),ry:n(o.height/2),
        fill:o.fill,stroke:o.stroke,'stroke-width':n(o.size)>
    else <path d:geom.polyline_to_svg_d(m.points(o)) ++ (if (o.closed) " Z" else ""),
        fill:if (o.closed) o.fill else "none",stroke:o.stroke,'stroke-width':n(o.size),'stroke-linecap':"round",'stroke-linejoin':"round">>
pub fn artwork(doc,prefix="object-") => <g for (l in doc.layers where l.visible)
    <g id:prefix ++ l.id,opacity:n(l.opacity),for (o in l.objects) (graphic(o,prefix))>>
pub fn overlay(st) {
    let o = m.chosen(st)
    let picked = m.selected_objects(st)
    let box = m.selection_box(st)
    let size = 7.0/st.zoom
    ; <g 'pointer-events':"none",
        if (box != null and contains(["select","nodes","marquee"],st.tool)) <g
            if (len(picked)>1) (for (item in picked) <rect class:"selected-object",x:n(item.x),y:n(item.y),width:n(item.width),height:n(item.height),
                fill:"none",stroke:"#a89ae9",'stroke-width':n(1/st.zoom)>);
            <rect id:"selection-box",x:n(box.x),y:n(box.y),width:n(box.width),height:n(box.height),fill:"none",stroke:"#8170e8",'stroke-width':n(1.3/st.zoom)>
            if (o!=null and st.tool == "nodes") (for (i,p in m.points(o)) <rect id:"node-" ++ string(i),x:n(p.x-size/2),y:n(p.y-size/2),width:n(size),height:n(size),fill:"white",stroke:"#8170e8",'stroke-width':n(1.5/st.zoom)>)
            else if (o!=null and st.tool == "select") <rect id:"resize-handle",x:n(o.x+o.width-size/2),y:n(o.y+o.height-size/2),width:n(size),height:n(size),fill:"white",stroke:"#8170e8",'stroke-width':n(1.5/st.zoom)>>;
        if (st.drag.mode=="marquee") (let rect = geom.rect_from_points(st.drag.start,st.drag.end),
            <rect id:"marquee",x:n(rect.x),y:n(rect.y),width:n(rect.width),height:n(rect.height),
                fill:"#8170e8",'fill-opacity':"0.12",stroke:"#a995f0",'stroke-width':n(1/st.zoom),'stroke-dasharray':n(4/st.zoom) ++ " " ++ n(3/st.zoom)>);
        if (m.draft(st) != null) (graphic(m.draft(st),"preview-"));
        if (len(st.pen)>0) <g <path id:"pen-preview",d:geom.polyline_to_svg_d(st.pen),fill:"none",stroke:st.stroke,'stroke-width':n(st.size),'stroke-linecap':"round">
            for (i,p in st.pen) <circle cx:n(p.x),cy:n(p.y),r:n(4/st.zoom),fill:if (i==0) "#8170e8" else "white",stroke:"#8170e8",'stroke-width':n(1/st.zoom)>>>
}
pub fn svg(doc) => <svg xmlns:"http://www.w3.org/2000/svg",width:n(doc.width),height:n(doc.height),viewBox:"0 0 " ++ n(doc.width) ++ " " ++ n(doc.height),
    <title doc.title><defs <clipPath id:"export-clip",<rect width:n(doc.width),height:n(doc.height)>>>;
    <rect width:n(doc.width),height:n(doc.height),fill:doc.background>;
    <g 'clip-path':"url(#export-clip)",(artwork(doc,"export-"))>>
pub fn layers(st) => <div for (i in 0 to len(st.doc.layers)-1)
    (let l = st.doc.layers[len(st.doc.layers)-1-i], <div class:"layer-row" ++ (if (l.id==st.active) " active" else ""),
        (button("visible-" ++ l.id,if (l.visible) "Hide " ++ l.name else "Show " ++ l.name,"layer-visible/" ++ l.id,"eye",{'aria-pressed':string(l.visible),class:if (l.visible) "mini" else "mini off"}));
        <button id:"layer-" ++ l.id,class:"layer-pick",type:"button",'data-action':"layer-select/" ++ l.id,'aria-pressed':string(l.id==st.active),
            <span class:"layer-thumb",(icon(if (any([for (o in l.objects) o.kind=="image"])) "image" else if (len(l.objects)>0) "pen" else "rect"))>
            <span class:"layer-copy",<strong l.name><small string(len(l.objects)) ++ (if (len(l.objects)==1) " object" else " objects")>>>;
        (button("lock-" ++ l.id,if (l.locked) "Unlock " ++ l.name else "Lock " ++ l.name,"layer-locked/" ++ l.id,if (l.locked) "lock" else "unlock",{class:if (l.locked) "mini" else "mini off"}))>)>

pub fn tree() => <main id:"sketch",'data-error':"",
    <header class:"topbar",
        <div class:"brand",<div class:"brand-mark",(icon("sketch"))><h1 "Sketch"><span class:"badge","LAMBDA">>
        <div class:"file-tools",(button("blank","New","blank"));(button("open","Open","open"));(button("import-image","Import image · Ctrl/Cmd Shift I","import-image","image"));(button("save","Save project","save","save"))>
        <div class:"document-heading",<strong id:"document-title","Somewhere, slowly"><span id:"save-state","Saved">>
        <div class:"header-end",(button("example","Sample illustration","example"));<button id:"export",class:"primary",type:"button",'data-action':"export",(icon("export"));<span "Export SVG">>>>
    <nav class:"options",'aria-label':"Tool options",
        <strong id:"tool-title","Brush"><span class:"divider">
        <label 'for':"prop-size",<span "Size"><input id:"prop-size",type:"number",min:"0.1",max:"4096",step:"1",'data-property':"size"><span "px">>
        <label 'for':"prop-opacity",<span "Opacity"><input id:"prop-opacity",type:"number",min:"0",max:"1",step:"0.1",'data-property':"opacity">>
        <span class:"divider"><div class:"history-tools",(button("undo","Undo · Ctrl/Cmd Z","undo","undo"));(button("redo","Redo · Ctrl/Cmd Shift Z","redo","redo"))>
        <div class:"options-end",(button("grid","Snap to 20 px grid","grid","grid",{'aria-pressed':"false"}));(button("fit","Fit artboard · 0","fit","fit"))>>
    <div class:"workspace",
        <nav class:"tool-rail",'aria-label':"Drawing tools",for (tool in m.TOOLS)
            (button("tool-" ++ tool.id,tool.label ++ " · " ++ tool.key,"tool-" ++ tool.id,tool.id,{'aria-pressed':string(tool.id=="brush"),class:"tool"}))>
        <section class:"canvas-column",
            <div class:"document-tab",<span class:"tab-dot"><strong id:"tab-title","Somewhere, slowly"><span ".sketch"><span class:"tab-note","VECTOR ARTBOARD">>
            <div id:"stage",tabindex:"0",role:"region",'aria-label':"Illustration canvas",
                <svg id:"canvas",xmlns:"http://www.w3.org/2000/svg",viewBox:"-100 -60 1160 920",preserveAspectRatio:"none",
                    <defs <clipPath id:"page-clip",<rect id:"clip-rect",width:960,height:800>>;
                        <pattern id:"grid-pattern",width:20,height:20,patternUnits:"userSpaceOnUse",<circle cx:0,cy:0,r:1,fill:"#8170e8",opacity:"0.35">>>;
                    <rect id:"paper-shadow",x:4,y:6,width:960,height:800,fill:"#000000",opacity:"0.12">;
                    <rect id:"paper",width:960,height:800,fill:"#fffaf1">;
                    <g id:"artwork",'clip-path':"url(#page-clip)">;
                    <rect id:"grid-overlay",width:960,height:800,fill:"url(#grid-pattern)",style:"display:none",'pointer-events':"none">;
                    <g id:"overlay">>
                <div class:"canvas-label","ARTBOARD 01"><div class:"canvas-navigation",
                    (button("zoom-out","Zoom out","zoom-out"));<span id:"zoom-label","65%">;(button("zoom-in","Zoom in","zoom-in"))>>
            <div class:"canvas-hint",<span id:"hint","Drag to draw. Every mark stays editable."><span "VECTOR / RGB">>>
        <aside class:"dock",
            <nav class:"dock-tabs",role:"tablist",'aria-label':"Inspector panels",for (panel in [{id:"properties",label:"Properties"},{id:"layers",label:"Layers"}])
                (button("dock-tab-" ++ panel.id,panel.label,"dock-" ++ panel.id,"",{role:"tab",'aria-controls':"dock-panel-" ++ panel.id,
                    'aria-selected':string(panel.id=="properties"),tabindex:if (panel.id=="properties") "0" else "-1"}))>
            <div class:"dock-scroll",
            <div id:"dock-panel-properties",role:"tabpanel",'aria-labelledby':"dock-tab-properties",
            <section class:"color-section",(heading("Color","STUDIO PALETTE"));
                <div class:"color-preview",<span id:"stroke-preview"><span id:"fill-preview"><strong id:"color-label","#284F59">>
                <div class:"swatches",for (i,color in m.COLORS) <button id:"swatch-" ++ string(i),type:"button",'data-action':"color-" ++ color,
                    title:"Set ink to " ++ color ++ "; Shift-click sets fill",'aria-label':"Ink " ++ color,style:"background:" ++ color>>
                <div class:"two-fields",(field("stroke","Stroke"));(field("fill","Fill"))>
                <p class:"note","Click a swatch for ink. Shift-click for fill.">>
            <section class:"property-section",(heading("Properties"));
                <p id:"selection-title","Your next mark"><p id:"selection-note",class:"note","Pick an object to edit its position and style.">
                <div id:"geometry-fields",class:"two-fields",(field("x","X","number"));(field("y","Y","number"));(field("width","Width","number"));(field("height","Height","number"))>
                ; (field("text","Text"));(field("font_size","Text size","number",{min:"1",max:"4096"}));
                <div class:"object-actions",(button("duplicate","Duplicate selection","duplicate","duplicate"));(button("back","Send to back","back","down"));
                    (button("front","Bring to front","front","up"));(button("delete","Delete selection","delete","delete"))>
                <div id:"pen-actions",(button("finish-path","Finish open path · Enter","finish-path"));(button("close-path","Close path","close-path"))>>>
            <section id:"dock-panel-layers",class:"layers-section",role:"tabpanel",'aria-labelledby':"dock-tab-layers",style:"display:none",
                (heading("Layers","TOP → BOTTOM"));<div id:"layers">;
                <div class:"layer-actions",(button("layer-add","Add layer","layer-add","plus"));(button("layer-up","Move layer up","layer-up","up"));
                    (button("layer-down","Move layer down","layer-down","down"));(button("layer-delete","Delete layer","layer-delete","delete"))>
                ; (field("layer_name","Layer name"));(field("layer_opacity","Layer opacity","number",{min:"0",max:"1",step:"0.1"}))>
            <div class:"dock-note",<span class:"little-mark","λ"><p "A little space for big ideas."><small "MADE WITH LAMBDA + RADIANT">>>>>
    <footer <span class:"status-dot"><span id:"status",role:"status",'aria-live':"polite","Ready to make something.">
        <span id:"object-count","14 objects"><span id:"document-size","960 × 800 px">>
    <div id:"context-host">;
    <div id:"file-dialog",class:"dialog-backdrop",style:"display:none",
        <section class:"file-dialog",role:"dialog",'aria-modal':"true",'aria-labelledby':"dialog-title",
            <p class:"eyebrow","YOUR ILLUSTRATION"><h2 id:"dialog-title","Save project"><p id:"dialog-description","Save an editable Sketch project.">
            <label class:"field",'for':"file-path",<span "File path"><input id:"file-path",type:"text",value:"temp/sketch/illustration.sketch">>
            <p id:"dialog-error",role:"alert"><div class:"dialog-actions",(button("file-cancel","Cancel","close-dialog"));(button("file-confirm","Save file","file-confirm"))>>>>
