// D4.5.1v4 / S12.1.3: geometry stays mounted while simulation updates poses and care meters.
import dom
import scene_dom: lambda.scene3d.dom
import cage: .mod_cage
import model: .mod_model
import sim: .mod_sim
import scene: .mod_scene
import ui: .mod_ui
import items: .mod_items

fn percent(value) => string(int(round(value*100))) ++ "%"
pub pn camera_sync(owner,st) { scene_dom.camera_sync(scene_dom.node(owner,"cage-camera"),model.camera_state(st)) }
pub pn animate(owner,st) {
    for (h in st.pets) {
        let node = scene_dom.node(owner,h.id)
        let sleeping = h.activity == "sleep"
        let active = h.next != "" and h.activity != "waiting" or h.activity == "exercise"
        let scale = sim.size(h)
        let phase = st.elapsed*(if (h.activity == "exercise") 15.0 else 10.0)
        let position = sim.position(st.modules,h)
        scene_dom.attr(node,"position",[position[0],position[1]+(if (active) 0.025*sin(phase) else 0.0),position[2]])
        scene_dom.attr(node,"rotation",[0.0,if (h.next != "") sim.heading(st.modules,h) else if (h.activity == "drink") math.pi else 0.0,0.0])
        scene_dom.attr(node,"scale",[scale,scale*(if (sleeping) 0.7 else 1.0),scale])
        for (key,value in {activity:h.activity,node:h.node,next:h.next,age:h.age,food:h.food,energy:h.energy,walked:h.walked}) scene_dom.attr(node,"data-" ++ string(key),value)
        let blink = sleeping or st.elapsed%5.0>4.85
        scene_dom.attr(scene_dom.node(owner,h.id ++ "-eyes"),"visible",not blink)
        scene_dom.attr(scene_dom.node(owner,h.id ++ "-closed-eyes"),"visible",blink)
        scene_dom.attr(scene_dom.node(owner,h.id ++ "-head"),"rotation",[0.0,if (active or sleeping) 0.0 else 0.045*sin(st.elapsed*2),
            if (contains(["eat","drink","bathe","play"],h.activity)) 0.1*sin(st.elapsed*6) else 0.0])
        for (i in 0 to 3) {
            let limb = scene_dom.node(owner,h.id ++ "-paw-" ++ string(i))
            scene_dom.attr(limb,"position",scene.limb_position(i,phase,active))
            scene_dom.attr(limb,"rotation",[0.0,0.0,if (active) 0.18*sin(phase+i*math.pi) else 0.0])
        }
    }
    for (m in st.modules where items.has(m,"wheel"))
        scene_dom.attr(scene_dom.node(owner,m.id ++ "-spin"),"rotation",[0.0,0.0,if (st.spins[m.id] == null) 0.0 else st.spins[m.id]])
    if (st.follow) camera_sync(owner,st)
    scene_dom.attr(owner,"data-elapsed",st.elapsed)
}
pub pn life(owner,st) {
    for (h in st.pets) {
        scene_dom.text(owner,"name-" ++ h.id,h.name)
        scene_dom.text(owner,"activity-" ++ h.id,if (h.activity == "waiting") "Giving a neighbor room" else if (h.activity == "walking") "On a little adventure" else if (h.activity == "explore") "Watching the world" else h.activity)
    }
    let h = model.pet(st,st.pet_selected)
    scene_dom.show(owner,"pet-detail",h != null)
    if (h != null) {
        for (key in ["food","water","energy","comfort","bond"]) {
            dom.style_set_property(scene_dom.node(owner,"bar-" ++ key),"width",percent(h[key]))
            scene_dom.text(owner,"need-" ++ key,percent(h[key]))
        }
        scene_dom.text(owner,"pet-age","DAY " ++ string(round(h.age*10)/10))
        scene_dom.text(owner,"pet-activity",if (h.activity == "waiting") "Waiting for a clear path…" else if (h.activity == "walking") "Finding a little adventure…" else if (h.activity == "sleep") "Dreaming in the sleep nook…"
            else if (h.activity == "exercise") "Going absolutely nowhere, very fast." else if (h.activity == "eat") "A very important seed break."
            else if (h.activity == "drink") "A sip of fresh water." else if (h.activity == "bathe") "A little sand, a lot of joy."
            else if (h.activity == "play") "Investigating a favorite toy." else "Taking it all in.")
        scene_dom.text(owner,"pet-growth",string(int(round(sim.size(h)*100))) ++ "% grown · " ++ string(round(h.walked*10)/10) ++ " steps explored")
    }
    scene_dom.text(owner,"day-label","DAY " ++ string(1+int(floor(st.elapsed/120))))
    for (key in ["food","water","clean"]) scene_dom.text(owner,"supply-" ++ key,percent(st[key]))
}
pub pn sync(owner,previous,st) {
    if (previous == null or previous.modules != st.modules)
        scene_dom.replace(scene_dom.node(owner,"cage-modules"),[for (m in st.modules) scene.module_tree(st.modules,m,st)])
    let residents = scene_dom.node(owner,"cage-residents")
    let old = [for (child in dom.child_nodes(residents)) child]
    for (node in old) if (model.pet(st,dom.get_attribute(node,"id")) == null) dom.remove_child(residents,node)
    for (h in st.pets) if (scene_dom.matching_child(residents,h.id,"data-coat",h.coat) == null) scene_dom.mount(residents,scene.hamster(h,st.modules))
    if (previous == null or [for (h in previous.pets) [h.id,h.coat]] != [for (h in st.pets) [h.id,h.coat]])
        dom.set_inner_html(scene_dom.node(owner,"resident-list"),join([for (row in ui.residents(st)) format(row,'xml')],""))
    for (h in st.pets) scene_dom.attr(scene_dom.node(owner,"choose-" ++ h.id),"aria-pressed",h.id == st.pet_selected)
    // retain each cell button: replacing the grid discards focus and loses the next keyboard shortcut.
    for (cell in ui.grid(st)) {
        let node = scene_dom.node(owner,cell.id)
        for (key,value in cell where key is string or key is symbol) scene_dom.attr(node,string(key),value)
        dom.set_inner_html(node,join([for (child in content(cell)) format(child,'xml')],""))
    }
    for (m in st.modules) scene_dom.attr(dom.query_selector(scene_dom.node(owner,m.id ++ "-floor"),"material"),"color",if (m.id == st.selected) "#f1d38d" else "#e5d1a7")
    for (entry in cage.catalog) scene_dom.attr(scene_dom.node(owner,"tool-" ++ entry.kind),"aria-pressed",st.tool == entry.kind)
    for (entry in items.catalog) scene_dom.attr(scene_dom.node(owner,"item-" ++ entry.kind),"aria-pressed",st.item_tool == entry.kind)
    for (tab in ["modules","furnishings"]) scene_dom.attr(scene_dom.node(owner,"tab-" ++ tab),"aria-selected",st.tab == tab)
    dom.style_set_property(scene_dom.node(owner,"module-palette"),"display",if (st.tab == "modules") "grid" else "none")
    dom.style_set_property(scene_dom.node(owner,"item-palette"),"display",if (st.tab == "furnishings") "grid" else "none")
    for (id,pressed in {select:st.tool == "select" and st.item_tool == "",delete:st.tool == "delete",move:st.moving,'floor-0':st.level == 0,'floor-1':st.level == 1,play:st.playing,follow:st.follow})
        scene_dom.attr(scene_dom.node(owner,string(id)),"aria-pressed",pressed)
    for (key,value in {selected:st.selected,pet:st.pet_selected,modules:len(st.modules),residents:len(st.pets),playing:st.playing,
        level:st.level,tool:st.tool,tab:st.tab,items:items.count(st.modules),'item-tool':st.item_tool})
        scene_dom.attr(owner,"data-" ++ string(key),value)
    let selected = cage.lookup(st.modules,st.selected)
    scene_dom.show(owner,"module-details",selected != null)
    if (selected != null) {
        scene_dom.text(owner,"module-title",upper(cage.entry(selected.kind).title))
        scene_dom.text(owner,"module-info","Cell " ++ string(selected.x) ++ ", " ++ string(selected.z) ++ " · " ++ string(selected.turn*90) ++ "° · " ++
            string(len(cage.neighbors(st.modules,selected.id))) ++ " doors · " ++ string(len(items.contents(selected))) ++ " furnishings")
        dom.set_inner_html(scene_dom.node(owner,"furnishing-list"),join([for (row in ui.furnishings(st)) format(row,'xml')],""))
    }
    scene_dom.text(owner,"build-hint",if (st.tool == "delete") "Click a module to delete it. Residents need a free connected room. Undo restores the room and its contents."
        else if (st.tab == "furnishings") (if (st.item_tool == "") "Select a room to see its furnishings. Choose an item, then click a room to add it."
            else items.entry(st.item_tool).title ++ " · Click an existing room. Remove items under In this room.")
        else if (st.moving) "Choose an empty square to move this module."
        else if (st.tool == "select") "Choose a module, then an empty square. Matching doors connect." else
        cage.entry(st.tool).title ++ " · " ++ string(st.turn*90) ++ "° · Click an empty square on the plan or in the scene.")
    let h = model.pet(st,st.pet_selected)
    if (h != null and dom.get_state(scene_dom.node(owner,"pet-name"),"value") != h.name) dom.set_state(scene_dom.node(owner,"pet-name"),"value",h.name)
    scene_dom.text(owner,"play",if (st.playing) "Ⅱ Pause" else "▷ Play")
    scene_dom.text(owner,"speed",string(st.speed) ++ "×")
    scene_dom.text(owner,"save-state",if (st.saved == st.modules) "CAGE SAVED" else "UNSAVED CAGE")
    let missing = model.missing_rooms(st)
    scene_dom.text(owner,"connection-note",if (len(missing)>0) "This resident needs a path to: " ++ join(missing,", ") ++ "."
        else string(len(st.modules)) ++ " modules · " ++ string(items.count(st.modules)) ++ " furnishings · " ++ string(len(st.pets)) ++ " tiny residents")
    scene_dom.text(owner,"status",st.status)
    dom.set_state(scene_dom.node(owner,"undo"),"disabled",len(st.hist.undo) == 0)
    dom.set_state(scene_dom.node(owner,"redo"),"disabled",len(st.hist.redo) == 0)
    camera_sync(owner,st)
    animate(owner,st)
    life(owner,st)
}
