// D4.5.1v4 / S12.1.3: retain geometry and native controls across frame updates.
import dom
import scene_dom: lambda.scene3d.dom
import model: .mod_model
import scene: .mod_scene
import ui: .mod_ui

pub fn node(owner,id) => dom.get_element_by_id(owner,id)

pub pn camera_for(owner,st) {
    return scene_dom.measured_camera(node(owner,"tank-scene"),st.camera)
}
pub pn camera_sync(owner,st) {
    scene_dom.camera_sync(node(owner,"tank-camera"),st.camera)
}
pub pn animate(owner,st) {
    for (o in st.doc) {
        let target = node(owner,o.id)
        let pose = model.pose(o,st.time,o.id == st.selected)
        scene_dom.attr(target,"position",pose.position)
        scene_dom.attr(target,"rotation",pose.rotation)
        if (model.species(o.kind).category == "fish") scene_dom.attr(node(owner,o.id ++ "-tail"),"rotation",[0.0,pose.tail,0.0])
    }
    let chosen = model.lookup(st.doc,st.selected)
    let selection = node(owner,"selection-box")
    scene_dom.attr(selection,"visible",chosen != null)
    if (chosen != null) {
        scene_dom.attr(selection,"position",chosen.position)
        scene_dom.attr(selection,"scale",[for (v in model.species(chosen.kind).pick) v*chosen.size*1.12])
        scene_dom.attr(selection,"rotation",[0.0,chosen.yaw*math.pi/180.0,0.0])
    }
}
pub pn sync(owner,st) {
    let slot = node(owner,"tank-objects")
    var structure_changed = false
    // child_nodes is live; snapshot before removal so no sibling is skipped.
    let previous_children = [for (child in dom.child_nodes(slot)) child]
    for (child in previous_children) {
        if (model.lookup(st.doc,dom.get_attribute(child,"id")) == null) {
            dom.remove_child(slot,child)
            structure_changed = true
        }
    }
    for (o in st.doc) {
        var target = scene_dom.matching_child(slot,o.id,"data-kind",o.kind)
        if (target == null) { target = scene_dom.mount(slot,scene.object_tree(o,st.time,o.id == st.selected)); structure_changed = true }
        // authored positions remain separate from swimming poses and are what save/load preserves.
        scene_dom.attr(target,"data-anchor",o.position)
        scene_dom.attr(target,"data-size",o.size)
        scene_dom.attr(target,"scale",[o.size,o.size,o.size])
    }
    let resident_list = node(owner,"object-list")
    if (structure_changed or len(dom.query_selector_all(resident_list,"button")) != len(st.doc))
        dom.set_inner_html(resident_list,join([for (row in ui.object_list(st)) format(row,'xml')],""))
    for (o in st.doc) scene_dom.attr(node(owner,"choose-" ++ o.id),"aria-pressed",o.id == st.selected)
    for (entry in model.catalog) scene_dom.show(owner,"add-" ++ entry.kind,entry.category == st.category,"flex")
    for (category in ["fish","plant","rock","decor"]) scene_dom.attr(node(owner,"tab-" ++ category),"aria-pressed",category == st.category)
    for (key,value in {selected:st.selected,playing:st.playing,count:len(st.doc)}) scene_dom.attr(owner,"data-" ++ string(key),value)
    let chosen = model.lookup(st.doc,st.selected)
    scene_dom.show(owner,"empty-selection",chosen == null)
    scene_dom.show(owner,"properties",chosen != null)
    scene_dom.text(owner,"selection-title",if (chosen == null) "Every detail is yours." else model.species(chosen.kind).name)
    if (chosen != null) {
        scene_dom.text(owner,"selection-kind",upper(model.species(chosen.kind).category) ++ " / " ++ chosen.id)
        for (key,value in {x:chosen.position[0],y:chosen.position[1],z:chosen.position[2],size:chosen.size,yaw:chosen.yaw}) {
            scene_dom.text(owner,"value-" ++ string(key),string(round(value*100)/100) ++ (if (key == "size") "×" else if (key == "yaw") "°" else ""))
            let control = node(owner,"prop-" ++ string(key))
            // hidden controls acquire live form state during layout; seed their attribute first.
            if (dom.range_value(control) == null) scene_dom.attr(control,"value",string(value))
            else if (abs(dom.range_value(control)-value) > 0.00001) dom.set_state(control,"value",string(value))
        }
    }
    for (category,target in {fish:"fish-count",plant:"plant-count"})
        scene_dom.text(owner,target,string(len([for (o in st.doc where model.species(o.kind).category == string(category)) o])))
    scene_dom.text(owner,"decor-count",string(len([for (o in st.doc where contains(["rock","decor"],model.species(o.kind).category)) o])))
    scene_dom.text(owner,"object-count",string(len(st.doc)))
    scene_dom.text(owner,"save-state",if (st.doc == st.saved) "ALL SAVED" else "UNSAVED CHANGES")
    scene_dom.text(owner,"play",if (st.playing) "Ⅱ Pause" else "▷ Play")
    scene_dom.text(owner,"play-label",if (st.playing) "LIVING" else "PAUSED")
    scene_dom.attr(node(owner,"play"),"aria-pressed",st.playing)
    dom.set_state(node(owner,"undo"),"disabled",len(st.hist.undo) == 0)
    dom.set_state(node(owner,"redo"),"disabled",len(st.hist.redo) == 0)
    scene_dom.text(owner,"status",st.status)
    camera_sync(owner,st)
    animate(owner,st)
}
