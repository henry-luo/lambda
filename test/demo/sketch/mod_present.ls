import dom
import m: .mod_model
import scene: .mod_scene

pub pn node(owner,id) { return dom.get_element_by_id(owner,id) }
pn text(owner,id,value) {
    let el = node(owner,id)
    if (dom.text_content(el) != value) dom.set_text_content(el,value)
}
pn attr(owner,id,key,value) {
    let el = node(owner,id)
    if (dom.get_attribute(el,key) != value) dom.set_attribute(el,key,value)
}
pn shown(owner,id,visible,display="block") { dom.style_set_property(node(owner,id),"display",if (visible) display else "none") }
pn enabled(owner,id,yes) {
    let el = node(owner,id)
    if (not yes) dom.set_attribute(el,"disabled","")
    else if (dom.has_attribute(el,"disabled")) dom.remove_attribute(el,"disabled")
}
pn value(owner,key,v) {
    let el = node(owner,"prop-" ++ key)
    // the focused input owns incomplete drafts until blur; other fields follow the document.
    if (not dom.matches(el,":focus") and dom.get_state(el,"value") != string(v)) dom.set_state(el,"value",string(v))
}
pub pn camera(owner,st) {
    let b = dom.bounding_box(node(owner,"stage"))
    let w = b.width/st.zoom
    let h = b.height/st.zoom
    return {x:(st.doc.width-w)/2+st.pan.x,y:(st.doc.height-h)/2+st.pan.y,width:w,height:h}
}
pub pn point(owner,st,x,y) {
    let b = dom.bounding_box(node(owner,"stage"))
    let cam = camera(owner,st)
    return {x:cam.x+(x-b.x)/st.zoom,y:cam.y+(y-b.y)/st.zoom}
}
pub pn client_point(owner,st,p) {
    let b = dom.bounding_box(node(owner,"stage"))
    let cam = camera(owner,st)
    return {x:b.x+(p.x-cam.x)*st.zoom,y:b.y+(p.y-cam.y)*st.zoom}
}
pub pn fit(owner,st) {
    let b = dom.bounding_box(node(owner,"stage"))
    return {*:st,zoom:m.clamp(min((b.width-90)/st.doc.width,(b.height-100)/st.doc.height),.1,3),pan:{x:0.0,y:0.0}}
}
pub pn sync(owner,st,previous=null) {
    // switch retained panels without retiring their native controls or changing document history.
    for (panel in ["properties","layers"]) {
        let active = st.dock_tab==panel
        shown(owner,"dock-panel-" ++ panel,active)
        attr(owner,"dock-tab-" ++ panel,"aria-selected",string(active))
        attr(owner,"dock-tab-" ++ panel,"tabindex",if (active) "0" else "-1")
    }
    let cam = camera(owner,st)
    attr(owner,"canvas","viewBox",scene.n(cam.x) ++ " " ++ scene.n(cam.y) ++ " " ++ scene.n(cam.width) ++ " " ++ scene.n(cam.height))
    for (id in ["paper","paper-shadow","grid-overlay","clip-rect"]) {
        attr(owner,id,"width",scene.n(st.doc.width))
        attr(owner,id,"height",scene.n(st.doc.height))
    }
    attr(owner,"paper","fill",st.doc.background)
    // transient ink updates only its overlay; committed edits refresh the retained artwork group.
    if (previous == null or previous.doc != st.doc) dom.set_inner_html(node(owner,"artwork"),format(scene.artwork(st.doc),'xml'))
    dom.set_inner_html(node(owner,"overlay"),format(scene.overlay(st),'xml'))
    shown(owner,"grid-overlay",st.grid)
    dom.style_set_property(node(owner,"stage"),"cursor",if (st.tool == "pan") "grab" else if (contains(["select","nodes"],st.tool)) "default" else "crosshair")
    let selected = m.chosen(st)
    let selection_count = len(m.selected_ids(st))
    let layer = m.layer_of(st.doc,st.active)
    let settings = if (selected != null) selected else m.style(st)
    for (key in ["stroke","fill","size","opacity","text","font_size"]) value(owner,key,settings[key])
    for (key in ["x","y","width","height"]) value(owner,key,if (selected != null) selected[key] else "")
    value(owner,"layer_name",layer.name)
    value(owner,"layer_opacity",layer.opacity)
    shown(owner,"geometry-fields",selected != null,"grid")
    shown(owner,"pen-actions",len(st.pen)>0,"flex")
    for (id in ["duplicate","delete","front","back"]) enabled(owner,id,selection_count>0)
    let image_selected = selected.kind=="image"
    for (key in ["text","font_size"]) enabled(owner,"prop-" ++ key,selection_count<=1 and not image_selected)
    for (key in ["stroke","fill","size"]) enabled(owner,"prop-" ++ key,not image_selected)
    enabled(owner,"undo",len(st.hist.undo)>0)
    enabled(owner,"redo",len(st.hist.redo)>0)
    enabled(owner,"layer-delete",len(st.doc.layers)>1 and not layer.locked)
    for (key in ["layer_name","layer_opacity"]) enabled(owner,"prop-" ++ key,not layer.locked)
    let tool = [for (t in m.TOOLS where t.id==st.tool) t][0]
    text(owner,"tool-title",tool.label)
    for (t in m.TOOLS) attr(owner,"tool-" ++ t.id,"aria-pressed",string(t.id==st.tool))
    attr(owner,"grid","aria-pressed",string(st.grid))
    dom.style_set_property(node(owner,"stroke-preview"),"background",settings.stroke)
    dom.style_set_property(node(owner,"fill-preview"),"background",settings.fill)
    text(owner,"color-label",upper(settings.stroke))
    text(owner,"selection-title",if (selection_count>1) string(selection_count) ++ " objects selected"
        else if (selected == null) "Your next mark" else upper(selected.kind) ++ " / " ++ selected.id)
    text(owner,"selection-note",if (selection_count>1) "Drag any selected object to move them together. Style changes apply to all."
        else if (selected == null) "Pick an object or drag a rectangle to select." else
        if (image_selected) selected.image_name ++ " · " ++ string(selected.image_width) ++ " × " ++ string(selected.image_height) ++ " px. Shift-resize stretches."
        else if (st.tool == "nodes") "Drag a square node to reshape this path." else "Drag to move. The corner handle resizes.")
    if (previous == null or previous.doc != st.doc or previous.active != st.active) dom.set_inner_html(node(owner,"layers"),format(scene.layers(st),'xml'))
    text(owner,"document-title",st.doc.title)
    text(owner,"tab-title",st.doc.title)
    text(owner,"save-state",if (st.doc==st.saved) "Saved" else "Unsaved changes")
    text(owner,"zoom-label",string(int(round(st.zoom*100))) ++ "%")
    text(owner,"zoom-in","+")
    text(owner,"zoom-out","−")
    text(owner,"status",if (st.error!="") st.error else st.status)
    text(owner,"object-count",string(len(m.objects(st.doc))) ++ " objects")
    text(owner,"document-size",scene.n(st.doc.width) ++ " × " ++ scene.n(st.doc.height) ++ " px")
    text(owner,"hint",if (st.tool == "pen") "Click nodes · Enter finishes · Click first node to close · Esc cancels"
        else if (st.tool == "nodes") "Select a path, then drag its square nodes."
        else if (st.tool == "select" or st.tool == "marquee") "Drag empty canvas to select · Shift adds · V moves selected objects · M selects over artwork"
        else if (st.tool == "eraser") "Erase whole vector objects · One gesture, one undo step"
        else if (st.tool == "pan") "Drag to pan · Wheel scrolls · Ctrl/Cmd + wheel zooms"
        else if (st.tool == "text") "Edit the text in Properties, then click to place it."
        else "Drag to draw · Shift constrains shapes · Every mark stays editable")
    shown(owner,"file-dialog",st.dialog!="","flex")
    text(owner,"dialog-title",if (st.dialog=="import-image") "Import image" else if (st.dialog=="open") "Open project" else if (st.dialog=="export") "Export SVG" else "Save project")
    text(owner,"dialog-description",if (st.dialog=="open") "Open a .sketch project. The current illustration remains available in Undo."
        else if (st.dialog=="import-image") "Enter a raster image path. A still image is embedded on a new layer; transparency is preserved."
        else if (st.dialog=="export") "Export standalone SVG with embedded images, without editor guides or selection handles."
        else "Save your layers, paths and shapes in an editable .sketch project.")
    text(owner,"file-confirm",if (st.dialog=="import-image") "Import image" else if (st.dialog=="open") "Open file" else if (st.dialog=="export") "Export SVG" else "Save file")
    text(owner,"dialog-error",st.error)
    // popup controls belong to one invocation; initialize their native state as they mount (D4.5.1v4).
    if (previous==null or previous.context!=st.context) {
        dom.set_inner_html(node(owner,"context-host"),if (st.context==null) "" else format(scene.context_menu(st),'xml'))
    }
    for (key,v in {'data-tool':st.tool,'data-selected':st.selected,'data-active':st.active,'data-objects':string(len(m.objects(st.doc))),
        'data-selection':join(m.selected_ids(st),","),'data-selection-count':string(selection_count),
        'data-layers':string(len(st.doc.layers)),'data-history':string(len(st.hist.undo)),'data-redo':string(len(st.hist.redo)),
        'data-drag':if (st.drag == null) "" else st.drag.mode,'data-pen':string(len(st.pen)),
        'data-dialog':st.dialog,'data-error':st.error,'data-dirty':string(st.doc!=st.saved),'data-dock-tab':st.dock_tab,
        'data-context':string(st.context!=null),'data-clipboard':string(len(st.clipboard))}) dom.set_attribute(owner,string(key),v)
    return st
}
