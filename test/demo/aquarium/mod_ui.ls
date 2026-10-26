import model: .mod_model
import scene: .mod_scene

fn button(id,label,options={}) => <button id:id,type:"button",'data-action':id,*:options,label>
pub fn icon(entry) => <svg viewBox:"0 0 80 54",width:80,height:54,'aria-hidden':"true",
    if (entry.category == "fish") <g fill:entry.color,
        <path d:"M 22 27 L 9 15 L 9 39 Z">
        <ellipse cx:42,cy:27,rx:23,ry:if (entry.kind == "angel") 20 else 13>
        <path d:"M 29 18 L 35 7 L 49 19 Z",opacity:"0.7">
        <circle cx:55,cy:23,r:3,fill:"#173b42">
        if (entry.kind == "tetra") <path d:"M 23 30 H 53",stroke:"#f3777b",'stroke-width':5>>
    else if (entry.category == "plant") <g fill:entry.color,
        <path d:"M 40 49 Q 12 24 22 5 Q 40 18 40 49 M 40 49 Q 36 13 52 2 Q 61 27 40 49 M 40 49 Q 54 21 70 20 Q 69 44 40 49 M 40 49 Q 22 32 11 32 Q 12 47 40 49">>
    else if (entry.kind == "wood") <g fill:entry.color,<path d:"M 8 40 L 70 20 L 73 29 L 12 48 Z M 37 34 L 33 9 L 40 7 L 44 31 Z">>
    else <g fill:entry.color,
        <path d:if (entry.kind == "slate") "M 10 43 L 16 21 L 37 8 L 61 15 L 72 42 Z" else "M 9 39 Q 5 18 35 18 Q 71 12 73 37 Q 70 49 35 47 Z">
        <path d:"M 18 35 L 37 19 L 59 25 L 48 31 Z",fill:"#ffffff",opacity:"0.15">>>

fn catalog_card(entry) => <button id:"add-" ++ entry.kind,type:"button",class:"specimen",'data-add':entry.kind,
    'data-category':entry.category,title:"Add " ++ entry.name,
    <span class:"specimen-art",(icon(entry))>
    <span class:"specimen-copy",<strong entry.name><span entry.hint>>
    <span class:"add-mark","+">>
fn tab(key,label) => <button id:"tab-" ++ key,type:"button",'data-category':key,class:"category",'aria-pressed':string(key == "fish"),label>
fn property(key,label,low,high,step) => <label class:"property",'for':"prop-" ++ key,
    <span class:"property-heading",<span label><output id:"value-" ++ key,"—">>
    <input id:"prop-" ++ key,type:"range",min:string(low),max:string(high),step:string(step),value:"0",'data-property':key>>

pub fn object_list(st) => [for (o in st.doc,let entry = model.species(o.kind))
    <button id:"choose-" ++ o.id,type:"button",class:"object-row",'data-select':o.id,'aria-pressed':string(st.selected == o.id),
        <span class:"object-dot",style:"background:" ++ entry.color>
        <span entry.name>
        <small "#" ++ slice(o.id,5)>>]

pub fn tree() {
    let st = model.initial();
    <main id:"aquarium",'data-selected':"",'data-playing':"true",'data-count':string(len(st.doc)),
        <header class:"masthead",
            <div class:"brand",<span class:"brand-mark","λ"><div <h1 "Aquarium"><p "A LITTLE WORLD, MADE BY YOU">>>
            <div class:"file-tools",
                <label 'for':"tank-file","Tank file">
                <input id:"tank-file",type:"text",value:"temp/aquarium.json",'aria-label':"Tank file path",spellcheck:"false">;
                (button("open","Open")); (button("save","Save tank",{class:"primary"}))>>
        <div class:"workspace",
            <aside class:"library",'aria-label':"Aquarium library",
                <div class:"section-title",<span class:"eyebrow","THE COLLECTION"><h2 "Make room for life."><p "Choose a little something for your tank.">>
                <div class:"categories",role:"group",'aria-label':"Object categories",
                    (tab("fish","Fish")); (tab("plant","Plants")); (tab("rock","Rocks")); (tab("decor","Wood"))>
                <div id:"catalog",for (entry in model.catalog) catalog_card(entry)>
                <div class:"library-note",<span class:"note-symbol","≈"><p "Start with a stone. Add a few leaves. Let the rest find its rhythm.">>
                <div class:"library-footer",<span "LAMBDA / NATIVE 3D"><span "NO. 001">>>
            <section class:"studio",'aria-label':"Aquarium canvas",
                <div class:"canvas-heading",<div <span class:"eyebrow","YOUR AQUASCAPE"><h2 "The quiet reef">>
                    <span class:"live-badge",<span id:"live-dot"><span id:"play-label","LIVING">>>
                <div class:"scene-wrap",(scene.viewport(st));
                    <span class:"tank-label","FRESHWATER / 12 × 7 × 6">
                    <span class:"scene-tag","A moment under the surface.">>
                <div class:"canvas-toolbar",
                    <div class:"tool-group",(button("play","Ⅱ Pause",{'aria-pressed':"true"}));
                        (button("undo","↶",{title:"Undo (Ctrl/Cmd+Z)",'aria-label':"Undo"}));
                        (button("redo","↷",{title:"Redo (Ctrl/Cmd+Shift+Z)",'aria-label':"Redo"}))>
                    <div class:"tool-group",(button("front","Front"));(button("home","Perspective"));
                        (button("zoom-in","+",{'aria-label':"Zoom in"}));(button("zoom-out","−",{'aria-label':"Zoom out"}))>>
                <p class:"canvas-hint","Click to select · Drag an object to move · Drag water to orbit · Scroll to zoom">
                <div class:"tank-summary",<span <b id:"fish-count","5">;" fish"><span <b id:"plant-count","5">;" plants">
                    <span <b id:"decor-count","4">;" hardscape"><span id:"save-state","ALL SAVED">>>
            <aside class:"inspector",'aria-label':"Object inspector",
                <div class:"section-title",<span class:"eyebrow","THE DETAILS"><h2 id:"selection-title","Every detail is yours.">>
                <div id:"empty-selection",<div class:"empty-icon","⌁"><p "Pick a fish, plant or stone in the tank, or choose one from the list below."><p class:"muted","Selected fish hold still while you arrange them.">>
                <div id:"properties",style:"display:none",
                    <p id:"selection-kind",class:"muted","">
                    <h3 "POSITION">;
                    (property("x","Left / right",-6,6,0.05));
                    (property("y","Height",0,7,0.05));
                    (property("z","Back / front",-3,3,0.05));
                    <h3 "TRANSFORM">;
                    (property("size","Size",0.35,1.8,0.05));
                    (property("yaw","Rotation",0,359,1));
                    <div class:"selection-actions",(button("duplicate","Duplicate"));(button("remove","Remove",{class:"danger"}))>>
                <div class:"residents-heading",<h3 "IN YOUR TANK"><span id:"object-count",string(len(st.doc))>>
                <div id:"object-list",(object_list(st))>
                <div class:"tank-actions",(button("starter","Restore starter"));(button("clear","Empty tank"))>>>
        <footer class:"statusbar",<span id:"status",role:"status",st.status><span "AQUARIUM STUDIO · λ">>>
}
