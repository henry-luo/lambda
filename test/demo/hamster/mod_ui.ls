import cage: .mod_cage
import model: .mod_model
import sim: .mod_sim
import scene: .mod_scene
import items: .mod_items

fn button(id,label,options={}) => <button id:id,type:"button",'data-action':id,*:options,label>
// small recessed ears and broad cheek pouches keep the hamster silhouette clear at icon sizes.
pub fn portrait(coat=0) => <svg class:"hamster-face",viewBox:"0 0 64 64",'aria-hidden':"true",
    for (x in [17,47]) <g
        <ellipse cx:x,cy:17,rx:7.5,ry:8,fill:sim.coats[coat]>
        <ellipse cx:x,cy:17,rx:4.5,ry:5,fill:"#e1b1a7">>;
    <path d:"M 17 23 C 17 16 24 13 32 13 C 40 13 47 16 47 23 C 51 27 54 32 55 38 C 60 44 57 52 50 56 C 41 62 23 62 14 56 C 7 52 4 44 9 38 C 10 32 13 27 17 23 Z",fill:sim.coats[coat]>
    <path d:"M 25 15 L 28 11 L 30 15 L 34 11 L 36 15 L 40 13 L 39 19",fill:sim.coats[coat]>
    <path d:"M 13 34 C 16 30 20 34 23 38 C 26 41 29 41 32 41 C 35 41 38 41 41 38 C 44 34 48 30 51 34 C 59 42 55 53 47 56 C 39 60 25 60 17 56 C 9 53 5 42 13 34 Z",fill:"#fbecd0">
    for (x in [20,44]) <ellipse cx:x,cy:46,rx:9,ry:10,fill:"#fff2dc">;
    <ellipse cx:32,cy:46,rx:9,ry:8,fill:"#fff4df">
    for (x in [21,43]) <g
        <ellipse cx:x,cy:32,rx:3.1,ry:3.7,fill:"#382d27">
        <circle cx:x-0.9,cy:30.7,r:1.05,fill:"#fffdf6">>;
    <path d:"M 28.7 41 Q 32 38.8 35.3 41 Q 35 43 32 44.5 Q 29 43 28.7 41 Z",fill:"#cf9790">
    <rect x:29.7,y:48.3,width:4.6,height:3.6,rx:0.8,fill:"#fffdf6",stroke:"#bca084",'stroke-width':0.65>
    <path d:"M 32 44.5 V 47.5 M 32 47.5 Q 29.5 50 27 48 M 32 47.5 Q 34.5 50 37 48 M 32 49 V 51.5",
        fill:"none",stroke:"#9c7962",'stroke-width':1.05,'stroke-linecap':"round">
    for (side in [0,1]) <g transform:if (side == 0) "" else "translate(64 0) scale(-1 1)",
        <path d:"M 15 42 L 3 39 M 14 46 L 2 46 M 16 50 L 5 53",fill:"none",stroke:"#9f856a",
            'stroke-width':1.05,'stroke-linecap':"round",opacity:0.85>>>
fn module_icon(kind,color) => <svg viewBox:"0 0 64 48",'aria-hidden':"true",
    <path d:"M 9 18 L 31 7 L 55 18 L 55 38 L 32 47 L 9 35 Z",fill:color>
    <path d:"M 9 18 L 32 28 L 55 18 M 32 28 V 47",fill:"none",stroke:"#7f7860",'stroke-width':1,opacity:"0.4">
    if (kind == "wheel") <circle cx:33,cy:27,r:13,fill:"none",stroke:"#fcf7e9",'stroke-width':4>
    else if (kind == "ramp") <path d:"M 12 37 L 51 15 M 15 38 L 54 16",stroke:"#fff6dd",'stroke-width':4>
    else if (kind == "kitchen") <g <ellipse cx:26,cy:32,rx:9,ry:5,fill:"#fcf3d7"><rect x:38,y:16,width:7,height:16,rx:3,fill:"#d1e5df">>
    else if (kind == "sand") <ellipse cx:32,cy:29,rx:15,ry:8,fill:"#f7e5b8">
    else <path d:"M 27 44 V 34 A 6 6 0 0 1 39 34 V 44",fill:"#fbf2dc">>
pub fn palette() => [for (entry in cage.catalog)
    <button id:"tool-" ++ entry.kind,type:"button",class:"module-card",'data-tool':entry.kind,'aria-pressed':"false",title:entry.note,
        (module_icon(entry.kind,entry.color));<span entry.title><small entry.tag>>]
fn item_icon(kind,color) => <svg viewBox:"0 0 64 48",'aria-hidden':"true",
    if (kind == "bottle") <g <rect x:24,y:5,width:18,height:29,rx:7,fill:color>
        <rect x:25,y:17,width:16,height:14,rx:5,fill:"#7eafb5"><path d:"M 33 34 V 40 L 24 44",fill:"none",stroke:"#9a9c90",'stroke-width':3>>
    else if (kind == "bowl" or kind == "sand") <g <path d:"M 10 25 Q 12 43 32 43 Q 52 43 54 25 Z",fill:color>
        <ellipse cx:32,cy:25,rx:22,ry:9,fill:if (kind == "bowl") "#d6b875" else "#f0dfb8">
        for (i in 0 to 6) <ellipse cx:19+(i%4)*8,cy:23+floor(i/4)*5,rx:3,ry:1.5,fill:"#bb9d66">>
    else if (kind == "wheel") <g <path d:"M 18 45 L 25 28 H 39 L 46 45",fill:"none",stroke:"#a5aa92",'stroke-width':4>
        <circle cx:32,cy:22,r:18,fill:"none",stroke:color,'stroke-width':5>
        <path d:"M 14 22 H 50 M 23 7 L 41 37 M 23 37 L 41 7",stroke:color,'stroke-width':2>>
    else if (kind == "hideout") <g <path d:"M 11 22 L 32 8 L 53 22 V 43 H 11 Z",fill:color>
        <path d:"M 24 43 V 31 A 8 8 0 0 1 40 31 V 43",fill:"#fbf1dc"><path d:"M 7 23 L 32 5 L 57 23",fill:"none",stroke:"#9e7e5a",'stroke-width':4>>
    else if (kind == "bridge") <g <path d:"M 8 42 Q 32 0 56 42",fill:"none",stroke:color,'stroke-width':9>
        for (i in 0 to 5) <path d:"M 17 20 L 47 20",transform:"translate(0 " ++ string(i*4) ++ ")",stroke:"#ead9b9",'stroke-width':1.5>>
    else <g for (i in 0 to 2) <rect x:14+i*11,y:9+i*3,width:8,height:31,rx:3,fill:color,transform:"rotate(-22 32 26)">;
        <path d:"M 18 17 L 23 19 M 28 26 L 33 28 M 39 36 L 44 38",stroke:"#ead9b9",'stroke-width':2>>>
pub fn item_palette() => [for (entry in items.catalog)
    <button id:"item-" ++ entry.kind,type:"button",class:"module-card",'data-item':entry.kind,'aria-pressed':"false",title:entry.note,
        (item_icon(entry.kind,entry.color));<span entry.title><small entry.tag>>]
pub fn furnishings(st) => (let room = cage.lookup(st.modules,st.selected),let contents = items.contents(room),
    if (len(contents) == 0) [<p class:"hint","No furnishings here. Choose one from the Furnishings tab, then click this room.">]
    else [for (kind in contents) <div class:"furnishing-row",<span items.entry(kind).title>;
        (button("remove-item-" ++ kind,"Remove",{'data-remove-item':kind,'aria-label':"Remove " ++ lower(items.entry(kind).title)}))>])
pub fn cell_id(x,z) => "cell-" ++ string(x+2) ++ "-" ++ string(z+2)
pub fn grid(st) => [for (z in -2 to 2) for (x in -2 to 2,let m = cage.at(st.modules,x,z,st.level))
    <button id:cell_id(x,z),type:"button",class:if (m == null) "grid-cell empty" else "grid-cell filled",
        'data-x':string(x),'data-z':string(z),'data-module':if (m == null) "" else m.id,
        'aria-label':if (m == null) "Build at " ++ string(x) ++ ", " ++ string(z) else cage.entry(m.kind).title ++ " at " ++ string(x) ++ ", " ++ string(z),
        'aria-pressed':string(m != null and m.id == st.selected),
        style:if (m == null) "" else "background:" ++ cage.entry(m.kind).color,
        if (m == null) <span "+"> else <span class:"cell-kind",upper(slice(m.kind,0,3))>;
        if (m != null) for (side in 0 to 3 where cage.open_side(st.modules,m,side)) <i class:"port port-" ++ string(side)>>]
pub fn residents(st) => [for (h in st.pets)
    <button id:"choose-" ++ h.id,type:"button",class:"resident",'data-pet':h.id,'aria-pressed':string(st.pet_selected == h.id),
        <span class:"portrait",portrait(h.coat)>
        <span class:"resident-copy",<strong id:"name-" ++ h.id,h.name><small id:"activity-" ++ h.id,h.activity>>
        <span class:"resident-arrow","›">>]
fn meter(key,label) => <div class:"need",<span class:"need-label",label><span class:"meter",<i id:"bar-" ++ key>>;
    <output id:"need-" ++ key,"—">>

pub fn tree() {
    let st = model.initial();
    <main id:"hamster",'data-modules':"11",'data-residents':"3",'data-playing':"true",'data-selected':"",'data-pet':"ham-1",
        <header class:"masthead",
            <div class:"brand",<span class:"brand-pet",portrait(0)><div <h1 "Hamlet"><p "A TINY NEIGHBORHOOD, A LITTLE LIFE">>>
            <div class:"file-tools",<input id:"cage-file",type:"text",value:"temp/hamster.json",'aria-label':"Cage file path">;
                (button("open","Open"));(button("save","Save home",{class:"primary"}))>>
        <div class:"workspace",
            <aside class:"builder",'aria-label':"Cage builder",
                <div class:"section-title",<span class:"eyebrow","01 / BUILD THEIR WORLD"><h2 "Room to be tiny."><p "A room, a tunnel, a new little possibility.">>
                <div class:"builder-tabs",role:"tablist",'aria-label':"Build catalog",
                    (button("tab-modules","Modules",{role:"tab",'aria-selected':"true",'aria-controls':"module-palette"}));
                    (button("tab-furnishings","Furnishings",{role:"tab",'aria-selected':"false",'aria-controls':"item-palette"}))>
                <div id:"module-palette",class:"palette",role:"tabpanel",'aria-labelledby':"tab-modules",palette()>
                <div id:"item-palette",class:"palette",role:"tabpanel",'aria-labelledby':"tab-furnishings",style:"display:none",item_palette()>
                <div class:"plan-heading",<h3 "THE FLOOR PLAN"><div class:"floor-tabs",(button("floor-0","Ground",{'aria-pressed':"true"}));(button("floor-1","Upper",{'aria-pressed':"false"}))>>
                <div id:"floor-plan",'aria-label':"Five by five module plan",grid(st)>
                <div class:"build-tools",(button("select","Select",{'aria-pressed':"true"}));(button("rotate","Rotate ↻"));
                    (button("delete","Delete",{class:"danger",'aria-pressed':"false",title:"Click a module to delete it"}))>
                <p id:"build-hint",class:"hint","Choose a module, then an empty square. Matching doors connect.">
                <div id:"module-details",style:"display:none",<h3 id:"module-title","ROOM">
                    <p id:"module-info",class:"hint","">;
                    <div class:"build-tools",(button("move","Move"));(button("remove","Delete module",{class:"danger"}))>
                    <h3 "IN THIS ROOM"><div id:"furnishing-list",furnishings(st)>>>
            <section class:"studio",'aria-label':"Hamster cage canvas",
                <div class:"canvas-heading",<div <span class:"eyebrow","THE CARDBOARD COMMONS"><h2 "Small rooms. Big little lives.">>
                    <span class:"day-badge",<i><span id:"day-label","DAY 01">>>
                <div class:"scene-wrap",scene.viewport(st);
                    <span class:"scene-caption","A home that grows with them.">
                    <span class:"scene-edition","HAMLET / NATIVE 3D">>
                <div class:"canvas-toolbar",
                    <div class:"tool-group",(button("play","Ⅱ Pause",{'aria-pressed':"true"}));(button("speed","1×"));(button("skip","+30s"));
                        (button("undo","↶",{'aria-label':"Undo cage edit"}));(button("redo","↷",{'aria-label':"Redo cage edit"}))>
                    <div class:"tool-group",(button("home","Overview"));(button("top","Plan view"));
                        (button("zoom-in","+",{'aria-label':"Zoom in"}));(button("zoom-out","−",{'aria-label':"Zoom out"}))>>
                <p class:"canvas-hint","Drag to orbit · Scroll to zoom · Click a room or a resident · Build on the selected floor">
                <div class:"care-toolbar",<div <span class:"eyebrow","LITTLE DAILY RITUALS"><p "Good days start with the basics.">>
                    <div class:"care-actions",(button("feed","Refill seeds"));(button("water","Fresh water"));(button("clean","Clean bedding"))>>
                <div class:"supplies",<span "SEEDS ";<b id:"supply-food","100%">><span "WATER ";<b id:"supply-water","100%">>
                    <span "BEDDING ";<b id:"supply-clean","100%">><span id:"save-state","STARTER HOME">>
                <p id:"connection-note",class:"connection-note","11 connected modules · 3 tiny residents">>
            <aside class:"residents-panel",'aria-label':"Hamster residents and care",
                <div class:"residents-heading",<div <span class:"eyebrow","02 / MEET THE NEIGHBORS"><h2 "Little lives.">>;
                    (button("adopt","+",{'aria-label':"Adopt a hamster",title:"Adopt a virtual hamster"}))>
                <div id:"resident-list",residents(st)>
                <div id:"pet-detail",
                    <div class:"pet-detail-heading",<span class:"eyebrow","GETTING TO KNOW YOU"><span id:"pet-age","DAY 0.5">>
                    <label class:"name-label",'for':"pet-name","Name your little neighbor">
                    <input id:"pet-name",type:"text",value:"Pip",maxlength:"24",'aria-label':"Hamster name">
                    <p id:"pet-activity",class:"pet-activity","Exploring">;
                    (meter("food","Fullness"));(meter("water","Hydration"));(meter("energy","Energy"));(meter("comfort","Contentment"));(meter("bond","Trust"));
                    <div class:"pet-actions",(button("treat","Give a treat",{class:"primary"}));(button("follow","Follow",{'aria-pressed':"false"}))>
                    <p id:"pet-growth",class:"hint","Growing a little every day.">;
                    (button("rehome","Rehome resident",{class:"quiet"}))>
                <div class:"field-note",<span class:"eyebrow","A NOTE FROM THE KEEPER"><p "Tiny feet follow connected doors. Add a ramp to open a whole new floor.">>;
                (button("starter","Restore starter",{class:"quiet"})); " · "; (button("blank","Start empty",{class:"quiet"}))>>
        <footer class:"statusbar",<span id:"status",role:"status",st.status><span "λ · A VIRTUAL PET SANDBOX">>>
}
