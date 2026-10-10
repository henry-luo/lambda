import dom
import c: lambda.ui.core.component
import collection: lambda.ui.core.collection
import interaction: lambda.ui.core.interaction
import locale: .locale
import general: .general

pub fn choice(kind, props) element^ {
    let valid = collection.validate(props.items, string(kind))^
    let identity = collection.require_id(props,kind)^
    let flags = c.boolean_props(props,["keep_mounted","block"],kind)^;
    if (not c.enum_valid(props.orientation, ["horizontal", "vertical"])^) raise c.fail(kind, "invalid orientation")
    else if (kind == 'segmented' and not c.enum_valid(props.shape,["default","round"])^) raise c.fail(kind,"invalid shape")
    else if (kind == 'segmented' and props.name != null and not (props.name is string)) raise c.fail(kind,"name must be a string")
    else if (c.has(props,'value') and not any([for (item in collection.enabled(props.items)) item.key == props.value])) raise c.fail(kind,"value must identify an enabled item")
    else if (c.has(props,'default_value') and not any([for (item in collection.enabled(props.items)) item.key == props.default_value])) raise c.fail(kind,"default_value must identify an enabled item")
    else c.node(kind, props, null, ["items", "value", "default_value", "orientation", "disabled",*(if (kind == 'tabs') ["keep_mounted"] else if (kind == 'segmented') ["size","block","shape","name"] else [])])^
}
fn selected(node, current) => c.option(node.props, "value", current)
fn choice_id(node, index) => node.props.id ++ "-item-" ++ string(index)
fn panel_id(node, index) => node.props.id ++ "-panel-" ++ string(index)
fn choice_content(node, item) => if (node.kind != 'segmented') c.render(item.label) else
    [*(if (item.icon == null) [] else [<span class:"dtna-choice-icon",c.render(item.icon)>]),
        *(if (item.label == null) [] else [<span class:"dtna-choice-label",c.render(item.label)>])]
fn choices(node, active) {
    let tabs = node.kind == 'tabs'
    let menu = node.kind == 'menu'
    let segmented = node.kind == 'segmented';
    <div *:c.attrs(node.props), class:c.classes(node.kind, node.props) ++ (if (c.text(node.props.orientation) == "vertical") " dtna-vertical" else "") ++ (if (segmented and node.props.block) " dtna-segmented-block" else "") ++
            (if (segmented and c.text(node.props.shape) == "round") " dtna-segmented-round" else ""), style:node.props.style,
        *:(if (segmented) {role:c.option(node.props,"role","radiogroup"),['aria-label']:node.props.label,
            ['aria-orientation']:c.text(c.option(node.props,"orientation","horizontal")),['aria-disabled']:c.aria(node.props.disabled)} else {}),
        *[<div class:"dtna-choice-list",*:(if (segmented) {} else {role:if (tabs) "tablist" else "menubar",
            ['aria-label']:node.props.label,['aria-orientation']:c.text(c.option(node.props,"orientation","horizontal"))}),
            *[for (index, item in node.props.items) <button id:choice_id(node,index), type:"button",
                class:"dtna-choice" ++ (if (item.key == active) " dtna-choice-active" else ""),
                ["data-dtna-choice"]:string(index), role:if (tabs) "tab" else if (menu) "menuitem" else "radio",
                tabindex:if (item.key == active) "0" else "-1",
                *:c.boolean_attr("disabled", node.props.disabled or item.disabled),
                *:(if (tabs) {'aria-selected':c.aria(item.key == active),'aria-controls':panel_id(node,index)}
                    else if (menu) {} else {'aria-checked':c.aria(item.key == active)}),
                *:(if (item.title == null) {} else {title:c.text(item.title)}),*c.children(choice_content(node,item))>]>,
        if (tabs) [for (index, item in node.props.items where item.key == active or node.props.keep_mounted)
            <div id:panel_id(node,index), class:"dtna-tab-panel", role:"tabpanel", tabindex:"0",
                *:c.boolean_attr("hidden",item.key != active), style:if (item.key != active) "display:none;" else null,
                ["aria-labelledby"]:choice_id(node,index), c.render(item.children)>] else null,
        if (segmented and node.props.name != null) <input type:"hidden",name:node.props.name,value:c.text(active),
            *:c.boolean_attr("disabled",node.props.disabled)> else null]>
}
pn clicked(node, evt) {
    let target = interaction.target(node, evt, "[data-dtna-choice]")
    let index = if (target == null) null else (int(dom.get_attribute(target, "data-dtna-choice")) or null);
    if (index == null or node.props.disabled or node.props.items[index].disabled) null else node.props.items[index].key
}
pn focus_choice(node, evt, key) {
    let indices = [for (index, item in node.props.items where item.key == key) index];
    if (len(indices) == 1) {
        interaction.focus(node,evt,"[data-dtna-choice='" ++ string(indices[0]) ++ "']")
    }
}
view dtna_choices: <dtna kind: 'tabs' | 'segmented' | 'menu'> state current:collection.initial(~.props.items, ~.props) {
    choices(~, selected(~,current))
}
on click(evt) {
    let key = clicked(~,evt)
    if (key == null or (~.kind == 'segmented' and key == selected(~,current))) { return 'pass' }
    current = key
    emit("ui_change", c.action(~, 'select', key))
    'pass'
}
on keydown(evt) {
    if (~.props.disabled or interaction.target(~,evt,"[data-dtna-choice]") == null or not contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","Home","End"],evt.key)) { return 'pass' }
    let key = collection.move(~.props.items, selected(~,current), evt.key)
    if (key == null) { return 'pass' }
    if (~.kind == 'segmented' and key == selected(~,current)) { return 'prevent-default' }
    current = key
    focus_choice(~,evt,key)
    emit("ui_change", c.action(~, 'select', key))
    'prevent-default'
}

pub fn pagination(props) element^ {
    let total = c.option(props,"total",0)
    let size = c.option(props,"page_size",c.option(props,"default_page_size",10))
    let current = c.option(props,"current",c.option(props,"default_current",1))
    let flags = c.boolean_props(props,["show_size_changer","show_quick_jumper","hide_on_single_page"],"pagination")^
    let labels = locale.resolve(c.option(props,"locale","en-US"))^
    let options = c.option(props,"page_size_options",[10,20,50,100]);
    if (not (total is int) or total < 0 or not (size is int) or size < 1 or not (current is int) or current < 1)
        raise c.fail("pagination", "total must be nonnegative; page_size and current must be positive ints")
    else if (c.has(props,"current") and c.has(props,"default_current")) raise c.fail("pagination","current and default_current are mutually exclusive")
    else if (c.has(props,"page_size") and c.has(props,"default_page_size")) raise c.fail("pagination","page_size and default_page_size are mutually exclusive")
    else if (not (options is array) or len(options) == 0 or len(unique(options)) != len(options) or not all([for (entry in options) entry is int and entry > 0]))
        raise c.fail("pagination","page_size_options must be unique positive ints")
    else if (c.has(props,"simple") and not (props.simple is bool or props.simple is map)) raise c.fail("pagination","simple must be bool or a read_only options map")
    else if (c.has(props,"show_total") and not (props.show_total is bool or props.show_total is fn)) raise c.fail("pagination","show_total must be bool or a pure function")
    else (
        let simple = if (props.simple is map) (c.properties(props.simple,["read_only"],"pagination.simple")^,
            c.boolean_props(props.simple,["read_only"],"pagination.simple")^) else true,
        let summary = pagination_summary(props,min(collection.page_count(total,size),current),size)^,
        c.node('pagination',props,null,["total","page_size","default_page_size","current","default_current","disabled","size",
            "show_size_changer","show_quick_jumper","page_size_options","locale","simple","show_total","hide_on_single_page"])^
    )
}
fn page_direction_button(page, total, disabled, labels, previous) =>
    <button type:"button",["data-dtna-page"]:string(page + (if (previous) -1 else 1)),
        ["aria-label"]:if (previous) labels.previous else labels.next,
        *:c.boolean_attr("disabled",disabled or (if (previous) page == 1 else page == total)),if (previous) "‹" else "›">
pub fn page_controls(page, total, disabled = false, labels = locale.resolve()^) {
    let window = collection.page_window(page,total);
    [page_direction_button(page,total,disabled,labels,true),
        *[for (index,page_number in window) (
            if (index > 0 and page_number - window[index - 1] > 1) <button type:"button",class:"dtna-page-jump",
                ["data-dtna-page"]:string(if (page_number < page) max(1,page - 5) else min(total,page + 5)),
                ["aria-label"]:if (page_number < page) labels.jump_previous else labels.jump_next,*:c.boolean_attr("disabled",disabled),"…"> else null,
            <button type:"button", ["data-dtna-page"]:string(page_number),
                class:if (page_number == page) "dtna-page-active" else null,
                ["aria-label"]:labels.page ++ " " ++ string(page_number),
                ["aria-current"]:if (page_number == page) "page" else null, *:c.boolean_attr("disabled",disabled), string(page_number)>)],
        page_direction_button(page,total,disabled,labels,false)]
}
fn pagination_summary(props, page, size) {
    let total = c.option(props,"total",0)
    let bounds = if (total == 0) [0,0] else [(page-1)*size+1,min(total,page*size)]
    let labels = locale.resolve(c.option(props,"locale","en-US"))^;
    if (props.show_total is fn) props.show_total(total,bounds)^
    else if (props.show_total) labels.total_prefix ++ string(total) ++ labels.total_suffix else null
}
fn pagination_simple(node, page, total, labels) =>
    [page_direction_button(page,total,node.props.disabled,labels,true),
        <span class:"dtna-page-simple",*[if (node.props.simple is map and node.props.simple.read_only)
            <span class:"dtna-page-current",string(page)> else <input class:"dtna-input",type:"text",inputmode:"numeric",size:"3",value:string(page),
                ["data-dtna-page-input"]:"",["data-dtna-page-simple"]:"",["aria-label"]:labels.jump_to,*:c.boolean_attr("disabled",node.props.disabled)>,
            <span "/">,string(total)]>,page_direction_button(page,total,node.props.disabled,labels,false)]
fn pagination_size(node, paging) => c.option(node.props,"page_size",paging.page_size)
fn pagination_current(node, paging) => min(collection.page_count(c.option(node.props,"total",0),pagination_size(node,paging)),c.option(node.props,"current",paging.current))
fn pagination_view(node, paging) {
    let size = pagination_size(node,paging)
    let total = collection.page_count(c.option(node.props,"total",0),size)
    let page = pagination_current(node,paging)
    let labels = locale.resolve(c.option(node.props,"locale","en-US"))^
    let sizes = sort(unique([*c.option(node.props,"page_size_options",[10,20,50,100]),size]));
    if (node.props.hide_on_single_page and total == 1) null else
    <nav *:c.styled(node), ["aria-label"]:c.option(node.props,"label","Pagination"),
        *[if (node.props.show_total) <span class:"dtna-page-total",*c.children(c.render(pagination_summary(node.props,page,size)^))> else null,
            *(if (node.props.simple) pagination_simple(node,page,total,labels) else page_controls(page,total,node.props.disabled == true,labels)),
            if (node.props.show_size_changer) <select class:"dtna-select dtna-page-size",["data-dtna-page-size"]:"",
                ["aria-label"]:labels.page_size,*:c.boolean_attr("disabled",node.props.disabled),
                *[for (entry in sizes) <option value:string(entry),*:c.boolean_attr("selected",entry == size),string(entry) ++ " " ++ labels.items_per_page>]> else null,
            if (node.props.show_quick_jumper and not node.props.simple and total > 1) <label class:"dtna-page-quick",*[labels.jump_to,
                <input class:"dtna-input",type:"text",["data-dtna-page-input"]:"",["aria-label"]:labels.jump_to,
                    inputmode:"numeric",*:c.boolean_attr("disabled",node.props.disabled)>]> else null]>
}
pn pagination_sync_input(node, evt, page) {
    let owner = interaction.root(node,evt)
    let input = if (owner == null) null else dom.query_selector(owner,"[data-dtna-page-simple]")
    // UA state owns the editable draft; committing a page restores the authoritative display.
    if (input != null) { dom.set_state(input,"value",string(c.option(node.props,"current",page))) }
}
pn pagination_event(node, evt, paging, event_kind) {
    if (node.props.disabled) { return paging }
    let selector = if (event_kind == 'click') "[data-dtna-page]" else if (event_kind == 'change') "[data-dtna-page-size]" else "[data-dtna-page-input]"
    let target = interaction.target(node,evt,selector)
    if (target == null or dom.get_state(target,"disabled")) { return paging }
    let simple = dom.has_attribute(target,"data-dtna-page-simple")
    if ((event_kind == 'blur' and not simple) or (event_kind == 'keydown' and evt.key != "Enter" and not (simple and contains(["ArrowUp","ArrowDown"],evt.key)))) { return paging }
    let requested = int(if (event_kind == 'click') dom.get_attribute(target,"data-dtna-page") else dom.get_state(target,"value")) or null
    let old_size = pagination_size(node,paging)
    let old_page = pagination_current(node,paging)
    if (not (requested is int) or (requested < 1 and not simple)) {
        if (simple) { pagination_sync_input(node,evt,old_page) }
        return paging
    }
    let size = if (event_kind == 'change') requested else old_size
    let total = collection.page_count(c.option(node.props,"total",0),size)
    let adjustment = if (simple and event_kind == 'keydown') (if (evt.key == "ArrowUp") -1 else if (evt.key == "ArrowDown") 1 else 0) else 0
    let page = if (event_kind == 'change') min(total,int((old_page - 1) * old_size / size) + 1) else max(1,min(total,requested + adjustment))
    pagination_sync_input(node,evt,page)
    if (page == old_page and size == old_size) { return paging }
    let result = {current:page,page_size:size}
    emit("ui_change",c.action(node,'page',result))
    result
}

view dtna_pagination: <dtna kind:'pagination'> state paging:{current:c.option(~.props,"default_current",1),page_size:c.option(~.props,"default_page_size",10)} {
    pagination_view(~,paging)
}
on click(evt) { paging = pagination_event(~,evt,paging,'click'); 'pass' }
on change(evt) { paging = pagination_event(~,evt,paging,'change'); 'pass' }
on blur(evt) { paging = pagination_event(~,evt,paging,'blur'); 'pass' }
on keydown(evt) {
    paging = pagination_event(~,evt,paging,'keydown')
    if (interaction.target(~,evt,"[data-dtna-page-input]") != null and
        (evt.key == "Enter" or (~.props.simple and contains(["ArrowUp","ArrowDown"],evt.key)))) 'prevent-default' else 'pass'
}

pub fn steps(props) element^ {
    let node = c.node('steps',props,null,["items","current","default_current","direction","responsive","clickable","disabled","size","status"])^
    let items = c.option(props,"items",[])
    let flags = c.boolean_props(props,["clickable","responsive"],"steps")^
    let current = c.option(props,"current",c.option(props,"default_current",0));
    if (not (items is array) or not (current is int) or current < 0 or (len(items) > 0 and current >= len(items)))
        raise c.fail("steps","items must be an array and current must identify a step")
    else if (c.has(props,"current") and c.has(props,"default_current")) raise c.fail("steps","current and default_current are mutually exclusive")
    else if (not c.enum_valid(props.direction,["horizontal","vertical"])^) raise c.fail("steps","invalid direction")
    else (
        let identity = if (props.clickable) collection.require_id(props,"steps")^ else true,
        let valid = [for (item in items) (
            let names = c.properties(item,["title","description","subtitle","icon","status","disabled"],"steps.item")^,
            let flags = c.boolean_props(item,["disabled"],"steps.item")^,
            if (not c.enum_valid(item.status,["wait","process","finish","error"])^) raise c.fail("steps","invalid item status") else true
        )],
        node
    )
}
fn step_body(index,item,status) => [
    <span class:"dtna-step-number",if (item.icon != null) c.render(item.icon) else if (status == "finish" or status == "error")
        general.icon({name:if (status == "finish") "check" else "close"})^ else string(index + 1)>,
    <div class:"dtna-step-content",<div class:"dtna-step-title",*[c.render(item.title),
        if (item.subtitle != null) <span class:"dtna-step-subtitle",c.render(item.subtitle)> else null]>
        <div class:"dtna-step-description",c.render(item.description)>>]
view dtna_steps: <dtna kind:'steps'> state current:c.option(~.props,"default_current",0) {
    let active = c.option(~.props,"current",current);
    <ol *:c.styled(~,(if (c.text(~.props.direction) == "vertical") "dtna-steps-vertical" else if (~.props.responsive != false) "dtna-steps-responsive" else "")),
        *[for (index,item in c.option(~.props,"items",[])) (
            let status = c.text(c.option(item,"status",if (index < active) "finish" else if (index == active) c.option(~.props,"status","process") else "wait")),
            <li class:"dtna-step dtna-step-status-" ++ status ++ (if (index == active) " dtna-step-current" else "") ++ (if (status == "finish") " dtna-step-finished" else ""),
                ["aria-current"]:if (index == active) "step" else null,
                *[if (~.props.clickable) <button type:"button",class:"dtna-step-button",["data-dtna-step"]:string(index),
                    *:c.boolean_attr("disabled",~.props.disabled or item.disabled),*step_body(index,item,status)>
                    else step_body(index,item,status)]>
        )]>
}
on click(evt) {
    let target = interaction.target(~,evt,"[data-dtna-step]")
    if (target == null or ~.props.disabled or dom.get_state(target,"disabled")) { return 'pass' }
    let index = int(dom.get_attribute(target,"data-dtna-step")) or null
    if (index == null or index == c.option(~.props,"current",current)) { return 'pass' }
    current = index
    emit("ui_change",c.action(~,'step',index))
    'pass'
}
