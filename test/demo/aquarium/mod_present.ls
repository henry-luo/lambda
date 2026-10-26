// D4.5.1v4 / S12.1.3: retain geometry and native controls across frame updates.
import dom
import scene_dom: lambda.scene3d.dom
import camera: lambda.scene3d.camera
import model: .mod_model
import scene: .mod_scene
import ui: .mod_ui

pub fn node(owner,id) => dom.get_element_by_id(owner,id)
pub pn attr(target,key,value) {
    let text = scene_dom.attribute(value)
    if (dom.get_attribute(target,key) != text) dom.set_attribute(target,key,text)
}
pn text(owner,id,value) {
    let target = node(owner,id)
    if (dom.text_content(target) != value) dom.set_text_content(target,value)
}
pn show(owner,id,visible,display="block") {
    let target = node(owner,id)
    let desired = if (visible) display else "none"
    if (dom.style_get_property(target,"display") != desired) dom.style_set_property(target,"display",desired)
}
pub pn camera_for(owner,st) {
    let bounds = dom.bounding_box(node(owner,"tank-scene"))
    return {camera:camera.prepare(st.camera,max(1.0,bounds.width),max(1.0,bounds.height)),bounds:bounds}
}
pub pn camera_sync(owner,st) {
    let target = node(owner,"tank-camera")
    attr(target,"position",st.camera.position)
    attr(target,"target",st.camera.target)
    attr(target,"up",st.camera.up)
}
pub pn animate(owner,st) {
    for (o in st.doc) {
        let target = node(owner,o.id)
        let pose = model.pose(o,st.time,o.id == st.selected)
        attr(target,"position",pose.position)
        attr(target,"rotation",pose.rotation)
        if (model.species(o.kind).category == "fish") attr(node(owner,o.id ++ "-tail"),"rotation",[0.0,pose.tail,0.0])
    }
    let chosen = model.lookup(st.doc,st.selected)
    let selection = node(owner,"selection-box")
    attr(selection,"visible",chosen != null)
    if (chosen != null) {
        attr(selection,"position",chosen.position)
        attr(selection,"scale",[for (v in model.species(chosen.kind).pick) v*chosen.size*1.12])
        attr(selection,"rotation",[0.0,chosen.yaw*math.pi/180.0,0.0])
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
        var target = node(owner,o.id)
        if (target != null and dom.get_attribute(target,"data-kind") != o.kind) {
            dom.remove_child(slot,target)
            target = null
        }
        if (target == null) { target = scene_dom.mount(slot,scene.object_tree(o,st.time,o.id == st.selected)); structure_changed = true }
        // authored positions remain separate from swimming poses and are what save/load preserves.
        attr(target,"data-anchor",o.position)
        attr(target,"data-size",o.size)
        attr(target,"scale",[o.size,o.size,o.size])
    }
    let resident_list = node(owner,"object-list")
    if (structure_changed or len(dom.query_selector_all(resident_list,"button")) != len(st.doc))
        dom.set_inner_html(resident_list,join([for (row in ui.object_list(st)) format(row,'xml')],""))
    for (o in st.doc) attr(node(owner,"choose-" ++ o.id),"aria-pressed",o.id == st.selected)
    for (entry in model.catalog) show(owner,"add-" ++ entry.kind,entry.category == st.category,"flex")
    for (category in ["fish","plant","rock","decor"]) attr(node(owner,"tab-" ++ category),"aria-pressed",category == st.category)
    for (key,value in {selected:st.selected,playing:st.playing,count:len(st.doc)}) attr(owner,"data-" ++ string(key),value)
    let chosen = model.lookup(st.doc,st.selected)
    show(owner,"empty-selection",chosen == null)
    show(owner,"properties",chosen != null)
    text(owner,"selection-title",if (chosen == null) "Every detail is yours." else model.species(chosen.kind).name)
    if (chosen != null) {
        text(owner,"selection-kind",upper(model.species(chosen.kind).category) ++ " / " ++ chosen.id)
        for (key,value in {x:chosen.position[0],y:chosen.position[1],z:chosen.position[2],size:chosen.size,yaw:chosen.yaw}) {
            text(owner,"value-" ++ string(key),string(round(value*100)/100) ++ (if (key == "size") "×" else if (key == "yaw") "°" else ""))
            let control = node(owner,"prop-" ++ string(key))
            // hidden controls acquire live form state during layout; seed their attribute first.
            if (dom.range_value(control) == null) attr(control,"value",string(value))
            else if (abs(dom.range_value(control)-value) > 0.00001) dom.set_state(control,"value",string(value))
        }
    }
    for (category,target in {fish:"fish-count",plant:"plant-count"})
        text(owner,target,string(len([for (o in st.doc where model.species(o.kind).category == string(category)) o])))
    text(owner,"decor-count",string(len([for (o in st.doc where contains(["rock","decor"],model.species(o.kind).category)) o])))
    text(owner,"object-count",string(len(st.doc)))
    text(owner,"save-state",if (st.doc == st.saved) "ALL SAVED" else "UNSAVED CHANGES")
    text(owner,"play",if (st.playing) "Ⅱ Pause" else "▷ Play")
    text(owner,"play-label",if (st.playing) "LIVING" else "PAUSED")
    attr(node(owner,"play"),"aria-pressed",st.playing)
    dom.set_state(node(owner,"undo"),"disabled",len(st.hist.undo) == 0)
    dom.set_state(node(owner,"redo"),"disabled",len(st.hist.redo) == 0)
    text(owner,"status",st.status)
    camera_sync(owner,st)
    animate(owner,st)
}
