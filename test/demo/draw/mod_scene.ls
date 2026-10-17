import dom
import model: .mod_model
import geom: lambda.editor.mod_geom
import router: lambda.editor.mod_router
import shapes: .mod_shapes
import paint: .mod_paint

// scroll does not bubble; synchronize the camera after native scrolling settles.
view <canvas_scrollport> { ~.rendered }
on scroll(evt) {
    dom.request_frame(dom.closest(evt.target,"#draw"),"canvas_scrolled")
    return 'handled'
}

import drawing_scene: lambda.editor.draw.scene
// compatibility exports; the drawing package owns the implementation.


pub fn svg(drawing,selected="",source="",grid=false,standalone=false,selection=[],zoom=1.0,marquee=null) => drawing_scene.svg(drawing,selected,source,grid,standalone,selection,zoom,marquee)

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
fn arrow_field(key,label,side) {
    let selected = if (side == "start") "none" else "triangle";
    <div class:"field arrow-picker",'data-arrow-key':key,<span id:key ++ "-label",label>
        <button id:"prop-" ++ key,class:"arrow-trigger",type:"button",value:selected,'data-label':label,
            'data-action':"arrow-open-" ++ key,'aria-label':label ++ ": " ++ selected,title:label ++ ": " ++ selected,
            'aria-haspopup':"listbox",'aria-expanded':"false",'aria-controls':key ++ "-options",
            <span id:key ++ "-preview",class:"arrow-current",(paint.arrow_preview(selected,side,"preview-" ++ key))>
            <svg class:"arrow-chevron",width:10,height:10,viewBox:"0 0 10 10",'aria-hidden':"true",<path d:"M 2 3 L 5 6 L 8 3",fill:"none",stroke:"currentColor",'stroke-width':1.3>>
        >
    >
}
// keep the popup outside the scrolling inspector so every option stays visible.
fn arrow_options(key) => <div id:key ++ "-options",class:"arrow-options",'data-arrow-key':key,role:"listbox",'aria-labelledby':key ++ "-label",style:"display:none",
    *[for (kind in model.arrows) <button id:key ++ "-option-" ++ kind,type:"button",role:"option",tabindex:"-1",
        'data-action':"arrow-set-" ++ key ++ "/" ++ kind,'data-arrow-value':kind,'aria-label':kind,title:kind,'aria-selected':"false",
        (paint.arrow_preview(kind,if (key == "start-arrow") "start" else "end","preview-" ++ key ++ "-" ++ kind))>]>
pub fn tree() => <main id:"draw",'data-error':"",
    <header class:"topbar",
        <div class:"brand",<div class:"brand-mark",(icon("connect"))><h1 "Draw"><span class:"native-badge","LAMBDA">>
        <div class:"document-title",<strong "Untitled diagram"><span id:"save-state","All changes saved">>
        <div class:"file-actions",(button("open","Open","open"));(button("save","Save","save"));(button("export","Export SVG","export"))>
    >
    <nav class:"toolbar",'aria-label':"Diagram tools",
        <div class:"tool-group",(button("tool-select","Select (V)","tool-select","select"));(button("tool-marquee","Rectangle select (M)","tool-marquee","marquee"));(button("tool-connect","Connector (C): drag to draw a line","tool-connect","connect"));(button("tool-pan","Pan (H): move the canvas","tool-pan","pan"))>
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
            <div class:"library-section",<p class:"eyebrow","MAKE CONNECTIONS"><p "Select Connector (C), then drag between shapes or empty points. You can also click two shapes.">;
                (button("connect-help","Connect shapes","tool-connect","connect"))>
            <div class:"library-section",<p class:"eyebrow","A FRESH PAGE">;(button("blank","New blank diagram","blank"));(button("example","Load example","example"))>
            <div class:"library-note",<strong "Ideas, connected."><p "Flowcharts, little plans, and the big picture."><span "BUILT WITH LAMBDA + RADIANT">>
        >
        <section class:"canvas-column",
            <div class:"canvas-caption",<span "DIAGRAM 01"><span "1120 × 760">>
            ; (apply(<canvas_scrollport rendered:<div id:"stage",tabindex:"0",role:"region",'aria-label':"Diagram canvas. Arrow keys move the selection; scrollbars navigate the page.",
                <div id:"canvas-space",<div id:"paper">>>>))
            ; <div class:"canvas-help",<span id:"tool-hint","Drag shapes to move · Drag the bottom-right handle to resize"><span "Esc cancels · Delete removes">>
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
                    <div class:"geometry-fields",(arrow_field("start-arrow","Start arrowhead","start"));(arrow_field("end-arrow","End arrowhead","end"))>
                    ;
                    (select_field("from-shape","From shape",[]));(select_field("from-port","Source attachment",model.ports));
                    (select_field("to-shape","To shape",[]));(select_field("to-port","Destination attachment",model.ports));
                    (field("label-position","Label position (0–1)","number"));
                    <div class:"arrange-actions",(button("reverse-edge","Reverse","reverse-edge"));(button("reset-bend","Reset bend","reset-bend"))>
                    <p class:"control-note","Drag endpoints onto a shape: a green × confirms attachment. Drag a cyan segment handle to reshape the line; the gold handle sets a bend.">
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
    ; <div id:"connector-menu",role:"toolbar",'aria-label':"Connector settings",style:"display:none",
        *[for (key in ["start-arrow","end-arrow"]) <button id:"connector-" ++ key,type:"button",'data-arrow-key':key,
            'data-action':"arrow-open-" ++ key,'aria-label':if (key == "start-arrow") "Start arrowhead" else "End arrowhead",
            'aria-haspopup':"listbox",'aria-expanded':"false",'aria-controls':key ++ "-options">]
        ; <button id:"connector-routing",type:"button",'data-action':"cycle-routing",'aria-label':"Change connector routing",
            <svg width:20,height:20,viewBox:"0 0 24 24",'aria-hidden':"true",<path d:"M 4 18 V 6 H 20",fill:"none",stroke:"currentColor",'stroke-width':1.6,'stroke-linejoin':"round">>>
    >
    ; *[for (key in ["start-arrow","end-arrow"]) arrow_options(key)]
    ; <div id:"file-dialog",class:"dialog-backdrop",style:"display:none",
        <section class:"file-dialog",role:"dialog",'aria-modal':"true",'aria-labelledby':"dialog-title",
            <p class:"eyebrow","YOUR DIAGRAM"><h2 id:"dialog-title","Save diagram">
            <p id:"dialog-copy","Save an editable diagram to a JSON file.">
            <label class:"field",'for':"file-path",<span "File path"><input id:"file-path",type:"text",value:"temp/draw/diagram.json">>
            <p id:"dialog-error",role:"alert">
            <div class:"dialog-actions",(button("file-cancel","Cancel","close-dialog"));(button("file-confirm","Save file","file-confirm"))>
        >
    >
>
