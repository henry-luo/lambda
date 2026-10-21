import dom
import tree: lambda.dom.tree
import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import surface: lambda.editor.raster.view
import scene: .mod_scene
import tools: lambda.editor.raster.tools
import colors: lambda.slide.color
import effects: lambda.editor.raster.effects
import styles: lambda.editor.raster.styles
import channels: lambda.editor.raster.channels
import tone: lambda.editor.raster.tone
import guides: lambda.editor.raster.guides

// selects use selectedness; text/range controls use the canonical value writer.
pub pn set_value(control,value) {
    if (dom.matches(control,"select")) {
        let options = tree.options_of(control)
        let index = model.value([for (i,option in options where dom.get_attribute(option,"value") == value) i][0],-1)
        dom.set_selected_index(control,index)
    } else dom.set_state(control,"value",value)
}
pub fn node(owner,id) => dom.get_element_by_id(owner,id)
// these nodes are non-void markup fragments; the HTML document formatter adds a shell.
fn markup(nodes) => join([for (child in nodes) format(child,'xml')],"")
pub pn publish(owner,st) any^ {
    let previous_dialog = dom.get_attribute(owner,"data-dialog")
    let painted = surface.publish(owner,st)^
    dom.set_close_guard(owner,editor.dirty(st))
    if (st.painted_doc == null or st.histogram_raster != painted.painted_raster) {
        dom.set_inner_html(node(owner,"histogram"),format(scene.histogram_chart(img.histogram(painted.painted_raster)^),'xml'))
    }
    let rows = editor.layers(st.doc)
    let active = if (len(st.selected)==1) editor.lookup(st.doc,st.selected[0]) else null
    let bounds = model.selection_bounds(st)
    let active_effect = effects.items(active.node)[st.effect_index]
    let tone_owner = if (active == null) "" else active.id ++ "/" ++ string(st.effect_index)
    let tone_changed = active_effect != st.tone_effect or tone_owner != st.tone_owner
    let curves = if (tone_changed) model.value(active_effect.options.curves,{}) else st.curve_draft
    let primary = model.primary(active.node)
    let active_style = styles.items(active.node)[st.style_index]
    for (id,requested in {status:st.status,'document-name':st.name,dirty:if (editor.dirty(st)) "●" else "",
        dimensions:string(st.doc.width) ++ " × " ++ string(st.doc.height) ++ " · RGB/8",
        zoom:if (st.zoom == 0) "Fit" else string(round(st.zoom*100)) ++ "%",
        'layer-count':string(len(rows)),'target-label':if (len(st.selected)>1) string(len(st.selected)) ++ " selected layers" else if (active == null) "Select a layer" else
            (if (st.target == "mask") "Layer mask · " else "Pixels · ") ++ active.node.name}) {
        let target = node(owner,string(id))
        if (dom.text_content(target) != requested) dom.set_text_content(target,requested)
    }
    for (key,requested in {'data-error':st.error,'data-layers':string(len(rows)),
        'data-history':string(len(st.hist.undo)),'data-selected':join(st.selected,","),'data-target':st.target,
        'data-x':if (bounds == null) "" else string(bounds.x),
        'data-y':if (bounds == null) "" else string(bounds.y),
        'data-bounds-width':if (bounds == null) "" else string(bounds.width),
        'data-bounds-height':if (bounds == null) "" else string(bounds.height),
        'data-guides':string(len(guides.items(st.doc))),'data-guides-visible':string(st.guides_visible),
        'data-snapping':string(st.snap_enabled),'data-snap-x':string(st.gesture.snapped.x),'data-snap-y':string(st.gesture.snapped.y),
        'data-curve-points':string(len(tone.points(curves,st.curve_channel))),
        'data-dirty':string(editor.dirty(st)),'data-dialog':st.dialog,'data-tool':st.tool,
        'data-recording':if (st.recording == null) "off" else string(len(st.recording.steps)),
        'data-actions':string(len(st.actions)),
        'data-kind':string(name(primary)),
        'data-styles':string(len(styles.items(active.node))),
        'data-channels':string(len(channels.items(st.doc))),'data-channel-view':st.channel_view,
        'data-batch-succeeded':string(model.value(st.batch_report.succeeded,0)),
        'data-batch-failed':string(model.value(st.batch_report.failed,0)),
        'data-retouch-source':if (st.retouch.source == null) "none" else "ready",
        'data-effects':string(len(effects.items(active.node))),
        'data-effect-enabled':string(active_effect.enabled != false),
        'data-blend':string(model.value(active.node.blend,'normal')),
        'data-transform':if (active.node.transform == null) "none" else "active",
        'data-selection':if (st.selection == null) "none" else "active",
        'data-drag':if (st.gesture == null) "" else st.gesture.mode})
        dom.set_attribute(owner,string(key),requested)
    let guide_html = markup([for (g in guides.items(st.doc)) <button type:"button",'data-action':"guide-remove",'data-guide':g.id,
        (if (g.axis == "x") "Vertical " else "Horizontal ") ++ string(g.position) ++ " px · Remove">])
    if (dom.get_attribute(node(owner,"guide-list"),"data-items") != guide_html) {
        dom.set_inner_html(node(owner,"guide-list"),guide_html)
        dom.set_attribute(node(owner,"guide-list"),"data-items",guide_html)
    }
    dom.set_text_content(node(owner,"guide-show"),if (st.guides_visible) "Hide guides" else "Show guides")
    dom.set_text_content(node(owner,"guide-snap"),if (st.snap_enabled) "Disable snapping" else "Enable snapping")
    let layer_html = markup(scene.layer_rows(st))
    if (dom.get_attribute(node(owner,"layers"),"data-rows") != layer_html) {
        dom.set_inner_html(node(owner,"layers"),layer_html)
        dom.set_attribute(node(owner,"layers"),"data-rows",layer_html)
    }
    let effect_html = markup(scene.effect_rows(st))
    if (dom.get_attribute(node(owner,"effects"),"data-items") != effect_html) {
        dom.set_inner_html(node(owner,"effects"),effect_html)
        dom.set_attribute(node(owner,"effects"),"data-items",effect_html)
    }
    if (tone_changed or st.painted_doc == null) for (control in tone.levels)
        set_value(node(owner,"levels-" ++ control.key),string(model.value(active_effect.options[control.key],control.value)*control.scale))
    if (st.painted_doc == null or curves != st.painted_curves or st.curve_channel != st.painted_curve_channel or st.curve_point != st.painted_curve_point) {
        dom.set_inner_html(node(owner,"curve-graph"),format(scene.curve_chart(curves,st.curve_channel,st.curve_point),'xml'))
        if (st.painted_doc == null or curves != st.painted_curves or st.curve_channel != st.painted_curve_channel)
            set_value(node(owner,"curve-points"),format(tone.points(curves,st.curve_channel),'mark'))
    }
    if (dom.get_state(node(owner,"curve-channel"),"value") != st.curve_channel)
        set_value(node(owner,"curve-channel"),st.curve_channel)
    if (st.curve_point<=0 or st.curve_point>=len(tone.points(curves,st.curve_channel))-1)
        dom.set_attribute(node(owner,"curve-remove"),"disabled","")
    else dom.remove_attribute(node(owner,"curve-remove"),"disabled")
    let style_html = markup(scene.style_rows(st))
    if (dom.get_attribute(node(owner,"styles"),"data-items") != style_html) {
        dom.set_inner_html(node(owner,"styles"),style_html)
        dom.set_attribute(node(owner,"styles"),"data-items",style_html)
    }
    let channel_html = markup([for (channel in channels.items(st.doc)) <option value:channel.id,channel.name>])
    if (dom.get_attribute(node(owner,"saved-channel"),"data-items") != channel_html) {
        let chosen = dom.get_state(node(owner,"saved-channel"),"value")
        dom.set_inner_html(node(owner,"saved-channel"),channel_html)
        dom.set_attribute(node(owner,"saved-channel"),"data-items",channel_html)
        set_value(node(owner,"saved-channel"),if (channels.lookup(st.doc,chosen) != null) chosen else channels.items(st.doc)[0].id)
    }
    if (primary != st.artwork_node and name(primary) in ['text','vector']) {
        for (id,requested in {'art-width':primary.width,'art-height':primary.height,'art-fill':colors.css(model.value(primary.fill,[0,0,0,1])),
            'art-stroke':colors.css(model.value(primary.stroke,[0,0,0,0])),'art-stroke-width':model.value(primary.stroke_width,0),
            'art-shape':model.value(primary.shape,"rectangle"),'art-radius':model.value(primary.radius,0),
            'art-text':if (name(primary) == 'text') model.value(primary[0],"") else "Hello Photos",'art-font':model.value(primary.font,"photos-font"),
            'art-size':model.value(primary.size,32),'art-leading':model.value(primary.line_height,1.2),'art-align':model.value(primary.align,"left"),
            'art-path':if (primary.path == null) "[]" else format(primary.path,'mark')}) set_value(node(owner,string(id)),string(requested))
    }
    if (active_style != st.style_node and active_style != null) {
        for (id,requested in {'style-kind':active_style.kind,'style-color':colors.css(active_style.color),
            'style-radius':model.value(active_style.radius,0),'style-dx':model.value(active_style.dx,0),'style-dy':model.value(active_style.dy,0),
            'style-opacity':model.value(active_style.opacity,1)}) set_value(node(owner,string(id)),string(requested))
    }
    // focused fields keep native drafts until change; publication never interrupts typing.
    for (id,requested in {'layer-name':if (active == null) "" else active.node.name,
        opacity:if (active == null) 1 else model.value(active.node.opacity,1),
        'image-width':st.size_draft.width,'image-height':st.size_draft.height,
        'position-x':if (active == null) 0 else model.bounds(active.node).x,
        'position-y':if (active == null) 0 else model.bounds(active.node).y,
        foreground:colors.css(st.foreground),background:colors.css(st.background),
        'brush-size':st.brush.size,'brush-hardness':st.brush.hardness,'brush-spacing':st.brush.spacing,
        'brush-flow':st.brush.flow,'brush-opacity':st.brush.opacity,tolerance:st.tolerance,
        'selection-mode':st.selection_mode,'gradient-kind':st.gradient_kind,
        'straighten-axis':st.straighten_axis,
        'channel-view':if (st.channel_view in ["r","g","b","a"]) st.channel_view else "composite",
        blend:model.value(active.node.blend,'normal'),
        'retouch-strength':st.retouch.strength,'retouch-radius':st.retouch.radius,'retouch-aligned':if (st.retouch.aligned) "aligned" else "reset",
        *:reduce([{},*[for (control in effects.controls) {["effect-" ++ control.key]:model.value(active_effect.options[control.key],model.value(st.effect_draft[control.key],control.value))}]],
            (result,entry) => {*:result,*:entry})}) {
        let control = node(owner,string(id))
        if (not dom.matches(control,":focus") and dom.get_state(control,"value") != string(requested))
            set_value(control,string(requested))
    }
    dom.set_text_content(node(owner,"action-status"),if (st.recording != null) "Recording · " ++ string(len(st.recording.steps)) ++ " steps"
        else if (len(st.actions)>0) st.actions[last].name ++ " · " ++ string(len(st.actions[last].steps)) ++ " steps" else "Record edits on one selected layer")
    dom.set_text_content(node(owner,"retouch-source"),if (st.retouch.source == null) "No source selected" else
        "Source: " ++ string(st.retouch.source.point[0]) ++ ", " ++ string(st.retouch.source.point[1]))
    for (tool in tools.catalog) dom.set_attribute(node(owner,"tool-" ++ tool.id),"class",if (st.tool == tool.id) "active" else "")
    dom.set_text_content(node(owner,"tool-label"),[for (tool in tools.catalog where tool.id == st.tool) tool.label][0])
    for (entry in [{id:"undo",disabled:not editor.can_undo(st)^},{id:"redo",disabled:not editor.can_redo(st)^}]) {
        if (entry.disabled) dom.set_attribute(node(owner,entry.id),"disabled","")
        else if (dom.get_attribute(node(owner,entry.id),"disabled") != null) dom.remove_attribute(node(owner,entry.id),"disabled")
    }
    let history_html = markup([for (tx in reverse(st.hist.undo)) <p (tx.meta[0].value)>])
    if (dom.get_attribute(node(owner,"history"),"data-items") != history_html) {
        dom.set_inner_html(node(owner,"history"),history_html)
        dom.set_attribute(node(owner,"history"),"data-items",history_html)
    }
    dom.style_set_property(node(owner,"file-dialog"),"display",if (st.dialog in ["open","open-tab","place","save","export","psd","action-save","action-open"]) "block" else "none")
    dom.style_set_property(node(owner,"export-options"),"display",if (st.dialog == "export") "block" else "none")
    dom.style_set_property(node(owner,"export-quality-row"),"display",if (dom.get_state(node(owner,"export-format"),"value") == "jpeg") "flex" else "none")
    dom.style_set_property(node(owner,"export-background-row"),"display",if (dom.get_state(node(owner,"export-alpha"),"value") == "flatten") "block" else "none")
    dom.style_set_property(node(owner,"crop-controls"),"display",if (st.tool == "crop") "flex" else "none")
    dom.set_attribute(owner,"data-crop",if (st.crop == null) "none" else "draft")
    dom.style_set_property(node(owner,"discard-dialog"),"display",if (st.dialog == "discard") "block" else "none")
    if (st.dialog == "discard" and previous_dialog != "discard") dom.focus_set(node(owner,"discard-cancel"),false)
    return {*:painted,artwork_node:primary,style_node:active_style,histogram_raster:painted.painted_raster,
        tone_effect:active_effect,tone_owner:tone_owner,curve_draft:curves,painted_curves:curves,
        painted_curve_channel:st.curve_channel,painted_curve_point:st.curve_point}
}
