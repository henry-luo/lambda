// Use rendered group widths to choose the compact layout for both demos.
import dom

fn pixels(node,property) => float(replace(dom.computed_style(node,property) or "0","px","")) ^ {0.0}
fn groups(ribbon) => dom.query_selector_all(ribbon,".ribbon-group")
fn spacing(group) => pixels(group,"padding-left") + pixels(group,"padding-right") +
    pixels(group,"border-left-width") + pixels(group,"border-right-width")
pn expanded_width(group) {
    let compact = dom.get_attribute(group,"data-compact")=="true"
    // Reuse cached widths during resize; only refreshed content needs this synchronous measurement.
    if (compact) dom.remove_attribute(group,"data-compact")
    let width = dom.bounding_box(group).width
    if (compact) dom.set_attribute(group,"data-compact","true")
    return width
}

pub pn close(ribbon) {
    if (ribbon==null) { return }
    for (group in groups(ribbon)) {
        if (dom.get_attribute(group,"data-open")!=null) {
            dom.remove_attribute(group,"data-open")
            dom.set_attribute(dom.query_selector(group,".ribbon-group-trigger"),"aria-expanded","false")
        }
    }
}

pub pn begin_resize(ribbon,stage) {
    let focused = if (ribbon==null) null else [for (control in dom.query_selector_all(ribbon,
        ".ribbon-group-trigger,.group-tools button,.group-tools input,.group-tools select,.group-tools textarea")
        where dom.focused(control)) control][0] or null
    let group = if (focused==null) null else dom.closest(focused,".ribbon-group")
    // Release focus before a popup closes or its trigger becomes hidden.
    if (focused!=null and stage!=null) dom.focus_set(stage,false)
    close(ribbon)
    return {focused:focused,group:group}
}

pub pn end_resize(handoff) {
    if (handoff.group==null) { return }
    let trigger = dom.query_selector(handoff.group,".ribbon-group-trigger")
    let target = if (dom.get_attribute(handoff.group,"data-compact")=="true") trigger
        else if (handoff.focused==trigger) dom.query_selector(handoff.group,
            ".group-tools button,.group-tools input,.group-tools select,.group-tools textarea")
        else handoff.focused
    if (target!=null) dom.focus_set(target,false)
}

pub pn update(ribbon,tab,labels,force=false) {
    if (ribbon==null) { return }
    let all = groups(ribbon)
    if (len(all)==0) { return }
    let box = dom.bounding_box(ribbon)
    let key = string(box.width) ++ ":" ++ tab ++ ":" ++ labels
    if (not force and dom.get_attribute(ribbon,"data-layout-key")==key) { return }
    let refresh = force or dom.get_attribute(ribbon,"data-measured-tab")!=tab or
        dom.get_attribute(ribbon,"data-measured-labels")!=labels or
        any([for (group in all) dom.get_attribute(group,"data-natural-width")==null])
    // Measurement temporarily expands compact groups, so close their panel first.
    if (refresh) close(ribbon)
    // Responsive group padding changes without changing command contents.
    let outer = [for (group in all) spacing(group)]
    let natural = [for (i,group in all)
        if (refresh) expanded_width(group)
        else float(dom.get_attribute(group,"data-natural-width")) +
            outer[i] - (float(dom.get_attribute(group,"data-natural-spacing")) ^ {outer[i]})]
    for (i,group in all) {
        if (refresh or dom.get_attribute(group,"data-natural-spacing")!=string(outer[i])) {
            dom.set_attribute(group,"data-natural-width",string(natural[i]))
            dom.set_attribute(group,"data-natural-spacing",string(outer[i]))
        }
    }
    let compact = [for (group in all,let trigger=dom.query_selector(group,".ribbon-group-trigger"))
        pixels(trigger,"width") + spacing(group)]

    let available = box.width - pixels(ribbon,"padding-left") - pixels(ribbon,"padding-right") -
        pixels(ribbon,"gap") * (len(all)-1)
    var used = sum(natural)
    var index = len(all)-1
    var planned = []
    while (used > available and index >= 0) {
        if (compact[index] < natural[index]) {
            planned = [*planned,index]
            used = used - natural[index] + compact[index]
        }
        index = index-1
    }
    // Commit only changed groups after the full right-to-left fit has been calculated.
    let changed = any([for (i,group in all) contains(planned,i) !=
        (dom.get_attribute(group,"data-compact")=="true")])
    if (changed) {
        if (not refresh) close(ribbon)
        for (i,group in all) {
            let was_compact = dom.get_attribute(group,"data-compact")=="true"
            if (contains(planned,i) and not was_compact) dom.set_attribute(group,"data-compact","true")
            else if (not contains(planned,i) and was_compact) dom.remove_attribute(group,"data-compact")
            if (dom.get_attribute(group,"data-align")!=null) dom.remove_attribute(group,"data-align")
        }
    }
    if (dom.get_attribute(ribbon,"data-measured-tab")!=tab)
        dom.set_attribute(ribbon,"data-measured-tab",tab)
    if (dom.get_attribute(ribbon,"data-measured-labels")!=labels)
        dom.set_attribute(ribbon,"data-measured-labels",labels)
    // A width-only resize with the same fit needs no DOM mutation or second layout.
    if (changed or refresh or dom.get_attribute(ribbon,"data-layout-key")==null)
        dom.set_attribute(ribbon,"data-layout-key",key)
}

pub pn toggle(trigger) {
    let group = dom.closest(trigger,".ribbon-group")
    if (group==null or dom.get_attribute(group,"data-compact")!="true") { return }
    let was_open = dom.get_attribute(group,"data-open")=="true"
    let ribbon = dom.closest(group,".ribbon-groups,.editor-ribbon")
    close(ribbon)
    if (was_open) { return }
    dom.set_attribute(group,"data-open","true")
    dom.set_attribute(trigger,"aria-expanded","true")
    let panel = dom.query_selector(group,".group-tools")
    let edge = dom.bounding_box(ribbon)
    let width = max(pixels(panel,"min-width"),
        (float(dom.get_attribute(group,"data-natural-width") or "0") ^ {0.0}) + pixels(panel,"padding-left") + pixels(panel,"padding-right"))
    if (dom.bounding_box(group).x+width > edge.x+edge.width-8)
        dom.set_attribute(group,"data-align","right")
}
