import dom
import model: .mod_model
import scene: .mod_scene
import paint: .mod_paint
import geom: lambda.editor.draw.geometry
import viewport_math: lambda.editor.draw.viewport
import drawing_scene: lambda.editor.draw.scene

pub pn viewport(owner) { return dom.bounding_box(dom.get_element_by_id(owner,"stage")) }
pub pn fit(owner,st) {
    let b = viewport(owner);
    {*:st,zoom:model.clamp(min((b.width-48)/model.width,(b.height-48)/model.height),0.1,2),pan:{x:0.0,y:0.0}}
}
pub pn camera(owner,st) {
    let stage = viewport(owner)
    let page = dom.bounding_box(dom.get_element_by_id(owner,"paper"));
    {*:st,camera_dirty:false,pan:{x:page.x-stage.x-(stage.width-page.width)/2,y:page.y-stage.y-(stage.height-page.height)/2}}
}
pub pn point(owner,st,x,y) { return viewport_math.point(dom.bounding_box(dom.get_element_by_id(owner,"paper")),st.zoom,x,y) }
pub pn apply_camera(owner,st) {
    let stage = viewport(owner)
    let surface = dom.bounding_box(dom.get_element_by_id(owner,"canvas-space"));
    // geometry reads expose the last committed layout; scroll only after the new surface is laid out.
    dom.set_scroll_state(dom.get_element_by_id(owner,"stage"),
        model.clamp(model.width*st.zoom/2-st.pan.x,0,max(surface.width-stage.width,0)),
        model.clamp(model.height*st.zoom/2-st.pan.y,0,max(surface.height-stage.height,0)))
    sync_menu(owner,st)
    return if (st.drag.page == null) st else {*:st,drag:{*:st.drag,
        page:dom.bounding_box(dom.get_element_by_id(owner,"paper")),scroll:dom.scroll_state(dom.get_element_by_id(owner,"stage"))}}
}
pn control_value(owner,key,requested) {
    let control = dom.get_element_by_id(owner,"prop-" ++ key)
    if (dom.matches(control,".arrow-trigger")) {
        if (dom.get_attribute(control,"value") != string(requested)) {
            dom.set_attribute(control,"value",string(requested))
            dom.set_inner_html(dom.get_element_by_id(owner,key ++ "-preview"),format(
                paint.arrow_preview(requested,if (key == "start-arrow") "start" else "end","preview-" ++ key),'xml'))
        }
        let label = dom.get_attribute(control,"data-label") ++ ": " ++ string(requested)
        dom.set_attribute(control,"aria-label",label)
        dom.set_attribute(control,"title",label)
        for (option in dom.query_selector_all(dom.get_element_by_id(owner,key ++ "-options"),"[data-arrow-value]"))
            dom.set_attribute(option,"aria-selected",string(dom.get_attribute(option,"data-arrow-value") == requested))
    } else if (dom.matches(control,"select")) {
        let options = dom.query_selector_all(control,"option")
        let indexes = [for (i,option in options where dom.get_attribute(option,"value") == string(requested)) i]
        // option replacement can leave the old painted index; compare the live DOM value.
        if (len(indexes)>0 and dom.get_state(control,"value") != string(requested)) dom.set_selected_index(control,indexes[0])
    } else if (dom.get_state(control,"value") != string(requested)) { dom.set_state(control,"value",string(requested)) }
}
// HTML formatting produces a document wrapper; select children require bare option fragments.
fn option_markup(value,label) string | error => format(<option value:value,label>,'xml')
pn object_options(owner,st) {
    let options = option_markup("","Free endpoint") ++ join([for (node in model.objects(st.doc) where node.tag == 'shape')
        option_markup(model.id(node),model.id(node) ++ " · " ++ model.attr(node,"label"))],"")
    let all_options = option_markup("","Select an object…") ++ join([for (node in model.objects(st.doc))
        option_markup(model.id(node),(if (node.tag == 'connector') "↗ " else "□ ") ++ model.id(node) ++ " · " ++ model.attr(node,"label"))],"");
    for (target in ["object-list","prop-from-shape","prop-to-shape"]) {
        let control = dom.get_element_by_id(owner,target)
        let markup = if (target == "object-list") all_options else options
        if (dom.get_attribute(control,"data-options") != markup) {
            dom.set_inner_html(control,markup)
            dom.set_attribute(control,"data-options",markup)
        }
    }
    let positions = [for (i,node in model.objects(st.doc) where model.id(node) == st.selected) i+1]
    dom.set_selected_index(dom.get_element_by_id(owner,"object-list"),if (len(positions)>0) positions[0] else 0)
}
pub pn sync_connector(owner,st) {
    let node = model.lookup(st.doc,st.selected).shape
    let ids = model.selected_ids(st)
    if (node.tag != 'connector') { return st }
    if (dom.get_element_by_id(owner,"edge-" ++ st.selected) == null and st.drag.mode == "connect") {
        // install a draft once; later moves reuse the same local edge update as endpoint edits.
        dom.set_inner_html(dom.get_element_by_id(owner,"connector-draft"),format(<g
            <defs (paint.marker(node,"start"));(paint.marker(node,"end"))>
            ; (drawing_scene.edge_view(st.doc,node,ids))>,'xml'))
    }
    // keep static shapes, definitions, grid and inspector nodes out of the pointer hot path.
    dom.set_inner_html(dom.get_element_by_id(owner,"edge-" ++ st.selected),
        drawing_scene.inner_xml(drawing_scene.edge_view(st.doc,node,ids)))
    dom.set_inner_html(dom.get_element_by_id(owner,"selection-overlay"),
        drawing_scene.inner_xml(drawing_scene.selection_view(st.doc,ids,st.zoom)))
    for (key in ["from-shape","to-shape","from-port","to-port"]) control_value(owner,key,model.attr(node,key))
    dom.set_attribute(owner,"data-hover",st.hover)
    dom.set_attribute(owner,"data-selected",st.selected)
    return st
}
pub pn sync_menu(owner,st) {
    let chosen = model.lookup(st.doc,st.selected).shape
    let ids = model.selected_ids(st)
    let menu = dom.get_element_by_id(owner,"connector-menu")
    let shown = chosen.tag == 'connector' and len(ids)==1 and st.drag == null and st.dialog == "" and st.tool == "select";
    dom.style_set_property(menu,"display",if (shown) "flex" else "none")
    if (shown) {
        let center = model.point_on_route(model.visible_route(st.doc,chosen),0.5)
        let paper = dom.bounding_box(dom.get_element_by_id(owner,"paper"))
        let stage = viewport(owner)
        let root = dom.bounding_box(owner)
        let x = model.clamp(paper.x+center.x*st.zoom-52,stage.x+6,stage.x+stage.width-110)
        let y = model.clamp(paper.y+center.y*st.zoom-50,stage.y+6,stage.y+stage.height-40);
        dom.style_set_property(menu,"left",string(x-root.x) ++ "px")
        dom.style_set_property(menu,"top",string(y-root.y) ++ "px")
        for (key in ["start-arrow","end-arrow"]) {
            let trigger = dom.get_element_by_id(owner,"connector-" ++ key)
            let value = model.attr(chosen,key);
            if (dom.get_attribute(trigger,"value") != value) {
                dom.set_attribute(trigger,"value",value)
                dom.set_inner_html(trigger,format(paint.arrow_preview(value,if (key == "start-arrow") "start" else "end","floating-" ++ key),'xml'))
            }
            dom.set_attribute(trigger,"title",(if (key == "start-arrow") "Start arrowhead: " else "End arrowhead: ") ++ value)
            dom.set_attribute(trigger,"aria-expanded",string(st.arrow_menu == key))
        }
        let kind = string(model.attr(chosen,"routing"))
        let route = dom.get_element_by_id(owner,"connector-routing");
        dom.set_attribute(route,"title","Route: " ++ kind ++ ". Click to cycle orthogonal, straight, curved.")
        dom.set_attribute(route,"aria-label","Route: " ++ kind ++ ". Change connector routing")
        dom.set_attribute(route,"data-routing",kind)
        dom.set_attribute(dom.query_selector(route,"path"),"d",if (kind == "straight") "M 4 16 L 20 8"
            else if (kind == "curved") "M 4 18 V 12 Q 4 6 10 6 H 20" else "M 4 18 V 6 H 20")
    }
}
pub pn sync(owner,st) {
    let stage = viewport(owner)
    let paper = dom.get_element_by_id(owner,"paper")
    let primary = model.lookup(st.doc,st.selected).shape
    let ids = model.selected_ids(st)
    let selected_shapes = [for (node in model.objects(st.doc) where node.tag == 'shape' and contains(ids,model.id(node))) node]
    let chosen = if (len(ids)>1 and len(selected_shapes)>0) selected_shapes[last] else primary
    let shape_selected = chosen.tag == 'shape'
    let shape_count = len([for (node in model.objects(st.doc) where node.tag == 'shape') node]);
    let page_width = model.width*st.zoom
    let page_height = model.height*st.zoom
    let surface = dom.get_element_by_id(owner,"canvas-space");
    // a half-viewport margin lets every page corner be centered without negative scroll offsets.
    for (key,value in {width:stage.width+page_width,height:stage.height+page_height})
        dom.style_set_property(surface,string(key),string(value) ++ "px")
    for (key,value in {width:page_width,height:page_height,left:stage.width/2,top:stage.height/2})
        dom.style_set_property(paper,string(key),string(value) ++ "px")
    dom.style_set_property(dom.get_element_by_id(owner,"stage"),"cursor",if (st.tool == "pan") "grab"
        else if (contains(["connect","marquee"],st.tool) or contains(model.kinds,st.tool)) "crosshair" else "default")
    dom.set_inner_html(paper,format(scene.svg(st.doc,st.selected,if (st.hover != "") st.hover else st.source,st.grid,false,ids,st.zoom,
        if (st.drag.mode == "marquee") geom.rect_from_points(st.drag.start,st.drag.current) else null),'xml'))
    for (target,shown in {properties:chosen != null,'empty-selection':chosen == null,
        'group-properties':len(ids)>1,'shape-properties':len(selected_shapes)>0,'edge-properties':chosen.tag == 'connector' and len(ids)==1,'file-dialog':st.dialog != ""})
        dom.style_set_property(dom.get_element_by_id(owner,string(target)),"display",if (shown) "block" else "none")
    for (target,text in {'zoom-label':string(round(st.zoom*100)) ++ "%",status:st.status,
        'selection-title':if (len(ids)>1) string(len(ids)) ++ " objects selected" else if (chosen == null) "The big picture" else if (shape_selected) "Shape properties" else "Connector properties",
        'object-count':string(shape_count) ++ " shapes · " ++ string(len(model.objects(st.doc))-shape_count) ++ " connectors",
        'history-label':string(len(st.hist.undo)) ++ " edits",'save-state':if (st.doc == st.saved) "No unsaved edits" else "Unsaved changes",
        'tool-hint':if (contains(model.kinds,st.tool)) "Click on the page to place your shape"
            else if (st.tool == "connect") (if (st.source == "") "Drag from a shape or empty point · Or click two shapes" else "Drag or click the destination shape")
            else if (st.tool == "pan") "Drag or scroll to pan · Ctrl/Cmd + scroll to zoom"
            else if (st.tool == "marquee") "Drag a rectangle to select · Shift adds to selection"
            else if (chosen.tag == 'connector') "Green × confirms attachment · Drag cyan segments · Three icons configure the line" else "Drag to move · Empty canvas selects a group · Shift-click adds",
        'dialog-title':if (st.dialog == "open") "Open diagram" else if (st.dialog == "export") "Export SVG" else "Save diagram",
        'dialog-copy':if (st.dialog == "open") "Open a diagram JSON file saved by Draw."
            else if (st.dialog == "export") "Export a clean vector diagram, ready to share." else "Save an editable diagram to a JSON file.",
        'dialog-error':st.error})
        {
            let node = dom.get_element_by_id(owner,string(target))
            if (dom.text_content(node) != text) dom.set_text_content(node,text)
        }
    dom.set_text_content(dom.query_selector(owner,"#file-confirm span"),if (st.dialog == "open") "Open file" else if (st.dialog == "export") "Export SVG" else "Save file")
    for (target,disabled in {undo:len(st.hist.undo)==0,redo:len(st.hist.redo)==0,
        duplicate:chosen==null,delete:chosen==null,'paste-style':st.clipboard==null}) {
        let button = dom.get_element_by_id(owner,string(target))
        if (disabled) dom.set_attribute(button,"disabled","")
        else if (dom.has_attribute(button,"disabled")) dom.remove_attribute(button,"disabled")
    }
    for (target,pressed in {'tool-select':st.tool=="select",'tool-marquee':st.tool=="marquee",'tool-connect':st.tool=="connect",'tool-pan':st.tool=="pan",grid:st.grid})
        dom.set_attribute(dom.get_element_by_id(owner,string(target)),"aria-pressed",string(pressed))
    for (kind in model.kinds) dom.set_attribute(dom.get_element_by_id(owner,"add-" ++ kind),"aria-pressed",string(st.tool == kind))
    sync_menu(owner,st)
    for (key in ["start-arrow","end-arrow"]) {
        let opened = st.arrow_menu == key and chosen.tag == 'connector' and len(ids)==1
        let trigger = dom.get_element_by_id(owner,"prop-" ++ key)
        let menu = dom.get_element_by_id(owner,key ++ "-options")
        let anchor = if (st.arrow_anchor == "connector-" ++ key) dom.get_element_by_id(owner,st.arrow_anchor) else trigger
        let b = dom.bounding_box(anchor)
        let root = dom.bounding_box(owner)
        let menu_height = len(model.arrows)*34+10
        let above = root.y+root.height-b.y-b.height < menu_height+4
        dom.set_attribute(trigger,"aria-expanded",string(opened))
        dom.style_set_property(menu,"display",if (opened) "block" else "none")
        dom.style_set_property(menu,"left",string(b.x-root.x) ++ "px")
        dom.style_set_property(menu,"top",string(if (above) max(4,b.y-root.y-menu_height-3) else b.y-root.y+b.height+3) ++ "px")
        dom.style_set_property(menu,"width",string(max(74,b.width)) ++ "px")
    }
    object_options(owner,st)
    if (chosen != null) {
        for (key in model.editable_keys(chosen) where key != st.editing.key) control_value(owner,key,model.attr(chosen,key))
        for (key in ["label","variant","x","y","width","height"]) {
            let control = dom.get_element_by_id(owner,"prop-" ++ key)
            if (len(ids)>1) dom.set_attribute(control,"disabled","")
            else if (dom.has_attribute(control,"disabled")) dom.remove_attribute(control,"disabled")
        }
        for (mode in ["left","center","right","top","middle","bottom","distribute-x","distribute-y"]) {
            let control = dom.get_element_by_id(owner,"align-" ++ mode)
            if (len(selected_shapes)<(if (starts_with(mode,"distribute-")) 3 else 2)) dom.set_attribute(control,"disabled","")
            else if (dom.has_attribute(control,"disabled")) dom.remove_attribute(control,"disabled")
        }
        for (key in ["flip-x","flip-y"]) dom.set_attribute(dom.get_element_by_id(owner,key),"aria-pressed",string(model.attr(chosen,key)))
        for (button in dom.query_selector_all(owner,"[data-color]"))
            dom.set_attribute(button,"aria-pressed",string(dom.get_attribute(button,"data-color") == model.attr(chosen,"fill")))
    }
    for (key,value in {'data-drag':if (st.drag == null) "" else st.drag.mode,'data-selected':st.selected,'data-selection':join(ids,","),'data-selection-count':string(len(ids)),'data-tool':st.tool,'data-shapes':string(shape_count),
        'data-edges':string(len(model.objects(st.doc))-shape_count),'data-history':string(len(st.hist.undo)),
        'data-error':st.error,'data-dialog':st.dialog,'data-grid':string(st.grid),'data-hover':st.hover}) dom.set_attribute(owner,string(key),value)
    dom.request_frame(owner,"draw_camera")
}
