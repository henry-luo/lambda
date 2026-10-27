// Keep font discovery as metadata; only the windowed popup creates font previews.
import dom
import context: ~~.mod_context_menu

pub let row_height = 40
pub let visible_rows = 8
pub let overscan = 2
pub let preview_batch = 2
pub let window_size = visible_rows+2*overscan
// Immutable metadata stays outside reactive state; preview frames copy only integer indexes and their window.
pub let families = sort(unique([for (face in (dom.query_local_fonts(null) ^ {[]})) face.family]),(family)=>lower(family))
let catalog = [for (family in families) {value:family,label:family,key:lower(family)}]
fn choices(current) => if (contains(families,current)) catalog else
    sort([*catalog,{value:current,label:current,key:lower(current)}],(entry)=>entry.key)
fn window_entries(current,rows,first) {
    let available = choices(current);
    [for (index in slice(rows,first,min(len(rows),first+window_size))) available[index]]
}
pub fn retained(ready,entries) => [for (entry in entries where contains(ready,entry.value)) entry.value]
pub fn next_ready(ready,entries) {
    let kept = retained(ready,entries);
    [*kept,*take([for (entry in entries where not contains(kept,entry.value)) entry.value],preview_batch)]
}
pub fn window_start(count,y) => max(0,min(int(floor(y/row_height))-overscan,max(0,count-window_size)))
pub fn matching(values,query) {
    let needle = lower(trim(query));
    [for (i,entry in values where needle=="" or contains(entry.key or lower(entry.label),needle)) i]
}
pub fn next_index(index,key,count) => if (count==0) -1 else max(0,min(count-1,
    if (key=="Home") 0 else if (key=="End") count-1 else
    index+(if (key=="ArrowUp") -1 else if (key=="ArrowDown") 1 else if (key=="PageUp") -visible_rows else visible_rows)))
pub fn revealed_y(index,y,height=visible_rows*row_height) => if (index*row_height<y) index*row_height
    else if ((index+1)*row_height>y+height) (index+1)*row_height-height else y
fn option_id(index) => "word-font-option-" ++ string(index)
fn trigger_id(id) => id ++ "-trigger"
fn font_style(family) => "font-family:" ++ format(family,'json')^ ++ ",sans-serif;"
fn owner(node) => dom.get_element_by_id(dom.root_node(node),"word-font-picker")
pub pn reposition(node) {
    let target = owner(node)
    if (target!=null) dom.dispatch(target,{type:"word_font_layout",bubbles:false})
}
pn schedule(node,previous,needed=true) {
    if (previous!=null) dom.cancel_frame(node,previous)
    return if (needed) dom.request_frame(node,"word_font_preview") else null
}
pn preview_window(node,previous,current,rows,first,ready) {
    let entries = window_entries(current,rows,first)
    // Preserve active faces when filtering or scrolling keeps them in the window.
    let kept = retained(ready,entries)
    return {ready:kept,frame:schedule(node,previous,len(kept)<len(entries))}
}
pn request(node,id,query="") {
    let target = owner(node)
    if (target!=null) dom.dispatch(target,{type:"word_font_open",bubbles:false,
        detail:{id:id,query:query}})
}
// A hidden native select retains the existing form/change contract without activating a second popup.
view <font_trigger> state seen:null, shown:null {
    let label = if (seen==~.current and shown!=null) shown else ~.current;
    <span class:"font-picker-trigger",
        <span class:"font-picker-native",["aria-hidden"]:"true",~.control>;
        <button id:trigger_id(~.id),type:"button",class:"font-picker-button",["aria-label"]:"Font: " ++ label,
            ["aria-haspopup"]:"listbox",["aria-expanded"]:"false",["aria-controls"]:"word-font-list",
            *:if (~.disabled) {disabled:""} else {},
            <span class:"font-picker-value",label>; <span class:"font-picker-chevron",["aria-hidden"]:"true","▾">>>
}
on render(evt) {
    if (evt.event_phase==2 and seen!=~.current) { seen = ~.current; shown = ~.current }
    return 'pass'
}
on change(evt) {
    // Dialog selects are uncontrolled; their visible trigger must still reflect a user commit.
    if (dom.get_attribute(evt.target,"id")==~.id) { seen = ~.current; shown = dom.get_state(evt.target,"value") }
    return 'pass'
}
on mousedown() { return 'prevent-default' }
on click(evt) {
    if (not ~.disabled) request(evt.target,~.id)
    return 'prevent-default'
}
on keydown(evt) {
    if (~.disabled or evt.metaKey or evt.ctrlKey or evt.altKey) { return 'pass' }
    if (evt.key in ["ArrowDown","ArrowUp","Enter"," "] or len(evt.key)==1) {
        request(evt.target,~.id,if (len(evt.key)==1 and evt.key!=" ") evt.key else "")
        return 'prevent-default'
    }
    return 'pass'
}
pub fn wrap(control,id,current,disabled=false) =>
    apply(<font_trigger control:control,id:id,current:current,disabled:disabled>)

view <font_scrollport> {
    <div id:"word-font-list",class:"font-picker-list",role:"listbox",["aria-label"]:"Font families",
        <div class:"font-picker-space",style:"height:" ++ string(~.total*row_height) ++ "px;",
            *[for (at,entry in ~.window) {
                let i = ~.first+at;
                let preview = contains(~.ready,entry.value);
                <button id:option_id(i),type:"button",role:"option",tabindex:"-1",class:"font-picker-row",
                    ["aria-selected"]:string(i==~.active),["aria-posinset"]:string(i+1),["aria-setsize"]:string(~.total),
                    ["data-font-index"]:string(i),["data-font-family"]:entry.value,
                    ["data-font-preview"]:string(preview),title:entry.label,
                    ["aria-label"]:entry.label,
                    style:"top:" ++ string(i*row_height) ++ "px;" ++
                        (if (preview) font_style(entry.value) else ""),
                    <span class:"font-picker-preview",["aria-hidden"]:"true",entry.label>;
                    <span class:"font-picker-readable",["aria-hidden"]:"true",entry.label>>
            }]>>
}
on scroll(evt) {
    dom.dispatch(owner(evt.target),{type:"word_font_scroll",bubbles:false,detail:{y:dom.scroll_state(evt.target).y}})
    return 'handled'
}

pn choose(node,id,index) {
    // closing can retire the clicked row; retain its document before dismissing the popup.
    let root = dom.root_node(node)
    dom.dispatch(owner(node),{type:"word_font_close",bubbles:false})
    let control = dom.get_element_by_id(root,id)
    if (control!=null) {
        if (index==null or index==dom.selected_index(control)) { return }
        dom.set_selected_index(control,index)
        dom.dispatch(control,{type:"change",bubbles:true})
    }
}
view <font_menu> state opened:false, source_id:null, current:"Georgia", rows:[], query:"", active:0, first:0,
    ready:[], frame:null, focus_pending:false, scroll_y:0, left:0, top:0 {
    <div id:"word-font-picker",class:"font-picker-layer",["data-open"]:string(opened),["data-window-start"]:string(first),
        ["data-total"]:string(len(rows)),["data-preview-count"]:string(len(ready)),
        if (opened) <div id:"word-font-backdrop",class:"font-picker-backdrop"> else null;
        if (opened) <div id:"word-font-menu",class:"font-picker-menu",style:"left:" ++ string(left) ++ "px;top:" ++ string(top) ++ "px;",
                <input id:"word-font-search",type:"search",class:"font-picker-search",placeholder:"Search fonts",value:query,
                    role:"combobox",["aria-label"]:"Search fonts",["aria-autocomplete"]:"list",["aria-expanded"]:"true",
                    ["aria-controls"]:"word-font-list",["aria-activedescendant"]:if (active>=0) option_id(active) else null>;
                apply(<font_scrollport window:window_entries(current,rows,first),
                    total:len(rows),first:first,active:active,ready:ready>);
                <div class:"font-picker-count",if (len(rows)==0) "No matching fonts" else string(len(rows)) ++ " fonts">> else null>
}
on word_font_open(evt) {
    let control = dom.get_element_by_id(dom.root_node(evt.target),evt.detail.id)
    if (control==null or dom.get_attribute(control,"disabled")!=null) { return 'handled' }
    let anchor = dom.get_element_by_id(dom.root_node(evt.target),trigger_id(evt.detail.id))
    let box = dom.bounding_box(anchor)
    if (source_id!=null) dom.set_attribute(dom.get_element_by_id(dom.root_node(evt.target),trigger_id(source_id)),"aria-expanded","false")
    source_id = evt.detail.id
    current = dom.get_state(control,"value")
    query = evt.detail.query
    let available = choices(current)
    rows = matching(available,query)
    let selected = if (query=="") dom.selected_index(control) else
        [for (i,index in rows where available[index].value==current) i][0]
    active = if (len(rows)==0) -1 else selected or 0
    scroll_y = max(0,(active-visible_rows/2)*row_height)
    first = window_start(len(rows),scroll_y)
    ready = []
    left = box.x
    top = box.y+box.height+4
    opened = true
    focus_pending = true
    dom.set_attribute(anchor,"aria-expanded","true")
    frame = schedule(evt.target,frame)
    return 'handled'
}
on word_font_layout(evt) {
    if (opened) context.fit(evt.target,"word-font-menu","word")
    return 'handled'
}
on word_font_preview(evt) {
    if (not opened or evt.detail!=frame) { return 'handled' }
    frame = null
    if (focus_pending) {
        // measure after the popup's initial layout, never from its render notification.
        context.fit(evt.target,"word-font-menu","word")
        dom.set_scroll_state(dom.get_element_by_id(dom.root_node(evt.target),"word-font-list"),0,scroll_y)
        dom.focus_set(dom.get_element_by_id(dom.root_node(evt.target),"word-font-search"),false)
        focus_pending = false
    }
    // Spread cold font decoding across frames; stale windows cancel their pending work.
    let entries = window_entries(current,rows,first)
    ready = next_ready(ready,entries)
    if (len(ready)<len(entries)) { frame = schedule(evt.target,null) }
    return 'handled'
}
on word_font_scroll(evt) {
    scroll_y = evt.detail.y
    let next = window_start(len(rows),scroll_y)
    if (next!=first) {
        first = next
        let preview = preview_window(evt.target,frame,current,rows,first,ready)
        ready = preview.ready
        frame = preview.frame
    }
    return 'handled'
}
on input(evt) {
    if (dom.get_attribute(evt.target,"id")!="word-font-search") { return 'pass' }
    query = dom.get_state(evt.target,"value")
    rows = matching(choices(current),query)
    active = if (len(rows)==0) -1 else 0
    first = 0
    if (scroll_y!=0) dom.set_scroll_state(dom.get_element_by_id(dom.root_node(evt.target),"word-font-list"),0,0)
    scroll_y = 0
    let preview = preview_window(evt.target,frame,current,rows,first,ready)
    ready = preview.ready
    frame = preview.frame
    return 'handled'
}
on word_font_close(evt) {
    if (frame!=null) dom.cancel_frame(evt.target,frame)
    frame = null
    opened = false
    rows = []
    ready = []
    let control = dom.get_element_by_id(dom.root_node(evt.target),trigger_id(source_id))
    if (control!=null) {
        dom.set_attribute(control,"aria-expanded","false")
        dom.focus_set(control,false)
    }
    return 'handled'
}
on mousedown(evt) { return if (dom.closest(evt.target,"[data-font-index]")!=null) 'prevent-default' else 'pass' }
on click(evt) {
    let row = dom.closest(evt.target,"[data-font-index]")
    if (row!=null) choose(evt.target,source_id,rows[int(dom.get_attribute(row,"data-font-index"))])
    else if (dom.get_attribute(evt.target,"id")=="word-font-backdrop")
        dom.dispatch(evt.target,{type:"word_font_close",bubbles:false})
    return 'handled'
}
on keydown(evt) {
    if (evt.key in ["Escape","Tab"]) {
        dom.dispatch(evt.target,{type:"word_font_close",bubbles:false})
        return if (evt.key=="Escape") 'prevent-default' else 'pass'
    }
    if (evt.key=="Enter") {
        if (active>=0) choose(evt.target,source_id,rows[active])
        return 'prevent-default'
    }
    if (evt.key in ["ArrowUp","ArrowDown","PageUp","PageDown","Home","End"]) {
        active = next_index(active,evt.key,len(rows))
        if (active>=0) {
            let scroller = dom.get_element_by_id(dom.root_node(evt.target),"word-font-list")
            scroll_y = revealed_y(active,scroll_y,dom.bounding_box(scroller).height)
            let next = window_start(len(rows),scroll_y)
            if (next!=first) {
                first = next
                let preview = preview_window(evt.target,frame,current,rows,first,ready)
                ready = preview.ready
                frame = preview.frame
            }
            dom.set_scroll_state(scroller,0,scroll_y)
        }
        return 'prevent-default'
    }
    return 'pass'
}
let menu = <font_menu>
pub fn widget() => apply(menu)
