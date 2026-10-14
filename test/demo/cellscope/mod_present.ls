import dom
import data: .mod_data
import model: .mod_model
import assets: .mod_assets

pub fn node(owner,id) => dom.query_selector(owner,"#" ++ id)
pub pn attr(target,key,value) {
    let text = string(value)
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
fn vector(values) => join([for (v in values) string(v)]," ")

pub pn camera(owner,st) {
    for (id in ["cell-scene","peer-scene"]) {
        let camera = node(node(owner,id),"camera")
        attr(camera,"position",vector(model.camera(st)))
        attr(camera,"target",vector([st.pan_x,st.pan_y,0.0]))
        attr(camera,"fov",model.fov(st))
    }
}
pub fn pending(owner,st) => [for (job in [
    {viewport:"cell-scene",cell:st.cell},
    * (if (st.compare) [{viewport:"peer-scene",cell:st.peer}] else [])
] where assets.needs_load(node(owner,job.viewport),job.cell)) job]

pub pn loading(owner,id) {
    attr(owner,"data-loading",id != null)
    attr(node(owner,"scene-pair"),"aria-busy",id != null)
    if (id != null) text(owner,"loading-specimen",data.cell(id).name)
    show(owner,"model-loading",id != null,"flex")
}

pn scene(owner,id,selected) {
    let viewport = node(owner,id)
    attr(viewport,"aria-label",data.cell(selected).name ++ " interactive 3D model")
    let slot = node(viewport,"imported-specimen")
    let ready = not assets.needs_load(viewport,selected)
    attr(slot,"visible",ready)
    return if (ready) dom.get_attribute(slot,"data-error") else ""
}

pub pn sync(owner,st) {
    let cell = data.cell(st.cell)
    for (key,value in {cell:st.cell,part:st.part,compare:st.compare}) attr(owner,"data-" ++ string(key),value)
    text(owner,"cell-title",cell.name)
    text(owner,"cell-kind",cell.type)
    text(owner,"scene-label",upper(cell.name) ++ " / TEXTURED 3D MODEL")
    text(owner,"occurrence-title",cell.occurrence.title)
    text(owner,"occurrence-body",cell.occurrence.body)
    text(owner,"favorite",if (contains(st.favorites,st.cell)) "★" else "☆")
    attr(node(owner,"favorite"),"aria-label","Save " ++ cell.name)
    text(owner,"rotate",if (st.rotating) "Stop rotation" else "Auto rotate")
    for (id,pressed in {rotate:st.rotating,
        compare:st.compare,favorite:contains(st.favorites,st.cell),library:st.library,gallery:not st.library})
        attr(node(owner,string(id)),"aria-pressed",pressed)
    for (c in data.cells) {
        show(owner,"choose-" ++ c.id,model.matches(st,c),"flex")
        attr(node(owner,"choose-" ++ c.id),"aria-pressed",c.id == st.cell)
        show(owner,"info-" ++ c.id,c.id == st.cell)
        for (o in c.organelles) {
            show(owner,"detail-" ++ c.id ++ "-" ++ o.id,o.id == st.part)
            attr(node(owner,"part-" ++ c.id ++ "-" ++ o.id),"aria-pressed",o.id == st.part)
        }
    }
    show(owner,"empty",len([for (c in data.cells where model.matches(st,c)) c]) == 0)
    show(owner,"peer-wrap",st.compare)
    attr(node(owner,"scene-pair"),"class",if (st.compare) "scene-pair comparing" else "scene-pair")
    text(owner,"peer-label",upper(data.cell(st.peer).name))
    let peer_index = ([for (i,c in data.cells where c.id == st.peer) i])[0]
    if (dom.selected_index(node(owner,"peer")) != peer_index) dom.set_selected_index(node(owner,"peer"),peer_index)
    let problem = scene(owner,"cell-scene",st.cell)
    let peer_problem = if (st.compare) scene(owner,"peer-scene",st.peer) else ""
    text(owner,"model-error-text",if (problem != "") problem else peer_problem)
    show(owner,"model-error",problem != "" or peer_problem != "")
    camera(owner,st)
    text(owner,"status",cell.name ++ " · " ++ data.organelle(st.cell,st.part).name ++ " selected")
}
