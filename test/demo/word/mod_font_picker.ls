// Keep font discovery as metadata; only the windowed popup creates font previews.
import dom
import context: ~~.mod_context_menu
import publish: ~~.mod_publish

pub let css = "
.font-picker-trigger { display:inline-flex; width:143px; min-width:0; }
.font-picker-native { display:none; }
.font-picker-trigger>.font-picker-button { display:flex; align-items:center; justify-content:space-between; gap:8px; width:100%; height:26px;
  padding:0 8px; border:1px solid #d5deeb; border-radius:5px; background:#fff; color:var(--ink,#1d1d1f); font-size:13px; text-align:left; }
.font-picker-value { overflow:hidden; white-space:nowrap; text-overflow:ellipsis; }
.font-picker-chevron { flex-shrink:0; font:12px var(--font,Arial,sans-serif); }
.dialog-field .font-picker-trigger,.shape-field .font-picker-trigger { display:flex; width:100%; }
.font-picker-layer { position:fixed; inset:0; z-index:120; pointer-events:none; font-family:var(--font,Arial,sans-serif); }
.font-picker-backdrop { position:absolute; inset:0; z-index:0; pointer-events:auto; }
.font-picker-menu { display:block; position:absolute; width:320px; max-width:calc(100vw - 16px); padding:10px; border:1px solid #cbd5e1;
  z-index:1; border-radius:8px; background:#fff; color:var(--ink,#1d1d1f); box-shadow:0 8px 28px #17274330; pointer-events:auto; }
.font-picker-search { display:block; width:100%; height:30px; padding:4px 8px; margin-bottom:8px; border:1px solid #cbd5e1; border-radius:5px; }
.font-picker-list { display:block; width:100%; height:min(256px,calc(100vh - 110px)); overflow:auto; overscroll-behavior:contain; }
.font-picker-space { position:relative; width:100%; }
.font-picker-row { position:absolute; left:0; width:100%; height:32px; padding:0 9px; border:0; border-radius:4px;
  text-align:left; font-size:16px; font-weight:400; line-height:32px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; background:#fff; color:var(--ink,#1d1d1f); }
.font-picker-row:hover { background:#f0f6ff; }
.font-picker-row[aria-selected=true] { background:#e4efff; color:#155da8; }
.font-picker-count { margin-top:7px; color:var(--muted,#6e6e73); font-size:11px; line-height:16px; }
.font-picker-search:focus,.font-picker-button:focus { outline:none; border-color:#0a84ff; }
"

pub let row_height = 32
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
// Native names are literal families; authored values can already be CSS fallback stacks.
fn font_style(family) => "font-family:" ++ (if (contains(families,family)) format(family,'json')^ else family) ++ ",sans-serif;"
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
pub pn show(node,id,query="") {
    let target = owner(node)
    if (target!=null) dom.dispatch(target,{type:"word_font_open",bubbles:false,
        detail:{id:id,query:query}})
}
pub fn field(control,id,label,disabled=false) => <span class:"font-picker-trigger",
    <span class:"font-picker-native",["aria-hidden"]:"true",control>;
    <button id:trigger_id(id),type:"button",class:"font-picker-button",["data-font-picker"]:id,["aria-label"]:"Font: " ++ label,
        ["aria-haspopup"]:"listbox",["aria-expanded"]:"false",["aria-controls"]:"word-font-list",
        *:if (disabled) {disabled:""} else {},
        <span id:id ++ "-value",class:"font-picker-value",label>; <span class:"font-picker-chevron",["aria-hidden"]:"true","▾">>>
// Imperative ribbon hosts update the adapter and its visible field together.
pub pn sync(node,id) {
    let root = dom.root_node(node)
    let control = dom.get_element_by_id(root,id)
    let trigger = dom.get_element_by_id(root,trigger_id(id))
    if (control==null or trigger==null) { return }
    let value = dom.get_state(control,"value")
    publish.text(root,id ++ "-value",value)
    publish.attr(trigger,"aria-label","Font: " ++ value)
}
pub fn opens_with(evt) => not (evt.metaKey or evt.ctrlKey or evt.altKey) and
    (evt.key in ["ArrowDown","ArrowUp","Enter"," "] or len(evt.key)==1)
pub pn trigger_key(node,id,evt,disabled=false) {
    if (not disabled and opens_with(evt)) {
        show(node,id,if (len(evt.key)==1 and evt.key!=" ") evt.key else "")
        return 'prevent-default'
    }
    return 'pass'
}
// A hidden native select retains the existing form/change contract without activating a second popup.
view <font_trigger> state seen:null, shown:null {
    let label = if (seen==~.current and shown!=null) shown else ~.current;
    field(~.control,~.id,label,~.disabled)
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
    if (not ~.disabled) show(evt.target,~.id)
    return 'prevent-default'
}
on keydown(evt) { return trigger_key(evt.target,~.id,evt,~.disabled) }
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
                        (if (preview) font_style(entry.value) else ""),entry.label>
            }]>>
}
on scroll(evt) {
    dom.dispatch(owner(evt.target),{type:"word_font_scroll",bubbles:false,detail:{y:dom.scroll_state(evt.target).y}})
    return 'handled'
}

pn choose(node,id,value) {
    // closing can retire the clicked row; retain its document before dismissing the popup.
    let root = dom.root_node(node)
    dom.dispatch(owner(node),{type:"word_font_close",bubbles:false})
    let control = dom.get_element_by_id(root,id)
    if (control!=null) {
        // Native hosts may retain extra options or append an imported family out of order.
        let index = [for (i,option in dom.query_selector_all(control,"option") where dom.get_attribute(option,"value")==value) i][0]
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
    let selected = [for (i,index in rows where available[index].value==current) i][0]
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
    if (opened) context.fit(evt.target,"word-font-menu",~.host)
    return 'handled'
}
on word_font_preview(evt) {
    if (not opened or evt.detail!=frame) { return 'handled' }
    frame = null
    if (focus_pending) {
        // measure after the popup's initial layout, never from its render notification.
        context.fit(evt.target,"word-font-menu",~.host)
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
    if (row!=null) choose(evt.target,source_id,choices(current)[rows[int(dom.get_attribute(row,"data-font-index"))]].value)
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
        if (active>=0) choose(evt.target,source_id,choices(current)[rows[active]].value)
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
pub fn widget(host="word") => apply(menu,{host:host})
