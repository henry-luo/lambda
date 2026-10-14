import dom
import model: .mod_model
import scene: .mod_scene

pub fn viewport(owner) => dom.bounding_box(dom.get_element_by_id(owner,"stage"))
pub fn fit(owner,st) {
    let b = viewport(owner);
    {*:st,zoom:model.clamp(min((b.width-48)/model.width,(b.height-48)/model.height),0.1,2),pan:{x:0.0,y:0.0}}
}
pub fn point(owner,st,x,y) {
    let b = dom.bounding_box(dom.get_element_by_id(owner,"paper"));
    {x:(x-b.x)/st.zoom,y:(y-b.y)/st.zoom}
}
pub pn sync(owner,st) {
    let stage = viewport(owner)
    let paper = dom.get_element_by_id(owner,"paper")
    let chosen = model.lookup(st.doc,st.selected).shape
    let shape_selected = chosen.tag == 'shape'
    let selected_box = model.box(chosen)
    let shape_count = len([for (node in model.objects(st.doc) where node.tag == 'shape') node]);
    for (key,value in {width:model.width*st.zoom,height:model.height*st.zoom,
        left:(stage.width-model.width*st.zoom)/2+st.pan.x,top:(stage.height-model.height*st.zoom)/2+st.pan.y})
        dom.style_set_property(paper,string(key),string(value) ++ "px")
    dom.style_set_property(dom.get_element_by_id(owner,"stage"),"cursor",if (st.tool == "pan") "grab"
        else if (st.tool == "connect" or contains(model.kinds,st.tool)) "crosshair" else "default")
    dom.set_inner_html(paper,format(scene.svg(st.doc,st.selected,st.source,st.grid),'xml'))
    for (target,shown in {properties:chosen != null,'empty-selection':chosen == null,
        'shape-properties':shape_selected,'edge-properties':chosen.tag == 'connector','file-dialog':st.dialog != ""})
        dom.style_set_property(dom.get_element_by_id(owner,string(target)),"display",if (shown) "block" else "none")
    for (target,text in {'zoom-label':string(round(st.zoom*100)) ++ "%",status:st.status,
        'selection-title':if (chosen == null) "The big picture" else if (shape_selected) "Shape properties" else "Connector properties",
        'object-count':string(shape_count) ++ " shapes · " ++ string(len(model.objects(st.doc))-shape_count) ++ " connectors",
        'history-label':string(len(st.hist.undo)) ++ " edits",'save-state':if (st.doc == st.saved) "No unsaved edits" else "Unsaved changes",
        'tool-hint':if (contains(model.kinds,st.tool)) "Click on the page to place your shape"
            else if (st.tool == "connect") (if (st.source == "") "Click the source shape" else "Click the destination shape")
            else if (st.tool == "pan") "Drag the canvas to pan · Scroll to zoom" else "Drag to move · Bottom-right handle resizes",
        'dialog-title':if (st.dialog == "open") "Open diagram" else if (st.dialog == "export") "Export SVG" else "Save diagram",
        'dialog-copy':if (st.dialog == "open") "Open a diagram JSON file saved by Draw."
            else if (st.dialog == "export") "Export a clean vector diagram, ready to share." else "Save an editable diagram to a JSON file.",
        'dialog-error':st.error})
        dom.set_text_content(dom.get_element_by_id(owner,string(target)),text)
    dom.set_text_content(dom.query_selector(owner,"#file-confirm span"),if (st.dialog == "open") "Open file" else if (st.dialog == "export") "Export SVG" else "Save file")
    for (target,disabled in {undo:len(st.hist.undo)==0,redo:len(st.hist.redo)==0,
        duplicate:not shape_selected,delete:chosen==null}) {
        let button = dom.get_element_by_id(owner,string(target))
        if (disabled) dom.set_attribute(button,"disabled","")
        else if (dom.has_attribute(button,"disabled")) dom.remove_attribute(button,"disabled")
    }
    for (target,pressed in {'tool-select':st.tool=="select",'tool-connect':st.tool=="connect",'tool-pan':st.tool=="pan",grid:st.grid,
        'add-rect':st.tool=="rect",'add-ellipse':st.tool=="ellipse",'add-diamond':st.tool=="diamond"})
        dom.set_attribute(dom.get_element_by_id(owner,string(target)),"aria-pressed",string(pressed))
    if (chosen != null) {
        for (key in ["label","x","y","width","height"]) {
            let control = dom.get_element_by_id(owner,"prop-" ++ key)
            let value = if (key == "label") model.attr(chosen,key) else selected_box[key]
            if (value != null and dom.get_state(control,"value") != string(value)) dom.set_state(control,"value",string(value))
        }
        if (chosen.tag == 'connector') dom.set_selected_index(dom.get_element_by_id(owner,"prop-routing"),
            if (model.attr(chosen,"routing") == 'straight') 1 else 0)
        for (button in dom.query_selector_all(owner,"[data-color]"))
            dom.set_attribute(button,"aria-pressed",string(dom.get_attribute(button,"data-color") == model.attr(chosen,"fill")))
    }
    for (key,value in {'data-selected':st.selected,'data-tool':st.tool,'data-shapes':string(shape_count),
        'data-edges':string(len(model.objects(st.doc))-shape_count),'data-history':string(len(st.hist.undo)),
        'data-error':st.error,'data-dialog':st.dialog,'data-grid':string(st.grid)}) dom.set_attribute(owner,string(key),value)
}
