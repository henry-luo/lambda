import dom
import c: lambda.ui.core.component
import collection: lambda.ui.core.collection

pub fn choice(kind, props) element^ {
    let valid = collection.validate(props.items, string(kind))^;
    if (not (props.id is string) or props.id == "") raise c.fail(kind, "a nonempty id is required")
    else if (not c.enum_valid(props.orientation, ["horizontal", "vertical"])^) raise c.fail(kind, "invalid orientation")
    else if (contains(props,'value') and not any([for (item in collection.enabled(props.items)) item.key == props.value])) raise c.fail(kind,"value must identify an enabled item")
    else if (contains(props,'default_value') and not any([for (item in collection.enabled(props.items)) item.key == props.default_value])) raise c.fail(kind,"default_value must identify an enabled item")
    else c.node(kind, props, null, ["items", "value", "default_value", "orientation", "disabled"])^
}
fn selected(node, current) => c.option(node.props, "value", current)
fn choice_id(node, index) => node.props.id ++ "-item-" ++ string(index)
fn panel_id(node, index) => node.props.id ++ "-panel-" ++ string(index)
fn choices(node, active) {
    let tabs = node.kind == 'tabs'
    let menu = node.kind == 'menu';
    <div *:c.attrs(node.props), class:c.classes(node.kind, node.props) ++ (if (c.text(node.props.orientation) == "vertical") " dtna-vertical" else ""), style:node.props.style,
        *[<div class:"dtna-choice-list", role:if (tabs) "tablist" else if (menu) "menubar" else "radiogroup",
            ["aria-label"]:node.props.label, ["aria-orientation"]:c.text(c.option(node.props, "orientation", "horizontal")),
            *[for (index, item in node.props.items) <button id:choice_id(node,index), type:"button",
                class:"dtna-choice" ++ (if (item.key == active) " dtna-choice-active" else ""),
                ["data-dtna-choice"]:string(index), role:if (tabs) "tab" else if (menu) "menuitem" else "radio",
                tabindex:if (item.key == active) "0" else "-1",
                *:c.boolean_attr("disabled", node.props.disabled or item.disabled),
                *:(if (tabs) {'aria-selected':c.aria(item.key == active),'aria-controls':panel_id(node,index)}
                    else if (menu) {} else {'aria-checked':c.aria(item.key == active)}), c.render(item.label)>]>,
        if (tabs) [for (index, item in node.props.items where item.key == active)
            <div id:panel_id(node,index), class:"dtna-tab-panel", role:"tabpanel", tabindex:"0",
                ["aria-labelledby"]:choice_id(node,index), c.render(item.children)>] else null]>
}
pn clicked(node, evt) {
    let target = dom.closest(evt.target, "[data-dtna-choice]")
    let index = if (target == null) null else (int(dom.get_attribute(target, "data-dtna-choice")) or null);
    if (index == null or node.props.disabled or node.props.items[index].disabled) null else node.props.items[index].key
}
pn focus_choice(node, evt, key) {
    let root = dom.closest(evt.target, ".dtna-" ++ string(node.kind))
    let indices = [for (index, item in node.props.items where item.key == key) index];
    if (root != null and len(indices) == 1) {
        dom.focus_set(dom.query_selector(root, "[data-dtna-choice='" ++ string(indices[0]) ++ "']"), true)
    }
}
view dtna_choices: <dtna kind: 'tabs' | 'segmented' | 'menu'> state current:collection.initial(~.props.items, ~.props) {
    choices(~, selected(~,current))
}
on click(evt) {
    let key = clicked(~,evt)
    if (key == null) { return 'pass' }
    current = key
    emit("ui_change", c.action(~, 'select', key))
    'pass'
}
on keydown(evt) {
    if (~.props.disabled or not contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","Home","End"],evt.key)) { return 'pass' }
    let key = collection.move(~.props.items, selected(~,current), evt.key)
    if (key == null) { return 'pass' }
    current = key
    focus_choice(~,evt,key)
    emit("ui_change", c.action(~, 'select', key))
    'prevent-default'
}

pub fn pagination(props) element^ {
    let total = c.option(props,"total",0)
    let size = c.option(props,"page_size",10)
    let current = c.option(props,"current",c.option(props,"default_current",1));
    if (not (total is int) or total < 0 or not (size is int) or size < 1 or not (current is int) or current < 1)
        raise c.fail("pagination", "total must be nonnegative; page_size and current must be positive ints")
    else c.node('pagination',props,null,["total","page_size","current","default_current","disabled"])^
}
view dtna_pagination: <dtna kind:'pagination'> state current:c.option(~.props,"default_current",1) {
    let total = collection.page_count(c.option(~.props,"total",0),c.option(~.props,"page_size",10))
    let page = min(total,c.option(~.props,"current",current));
    <nav *:c.attrs(~.props), class:c.classes('pagination',~.props), ["aria-label"]:c.option(~.props,"label","Pagination"),
        *[<button type:"button", ["data-dtna-page"]:string(page - 1), ["aria-label"]:"Previous page",
            *:c.boolean_attr("disabled",~.props.disabled or page == 1), "‹">,
        *[for (page_number in 1 to total where page_number == 1 or page_number == total or abs(page_number-page) <= 2)
            <button type:"button", ["data-dtna-page"]:string(page_number),
                class:if (page_number == page) "dtna-page-active" else null,
                ["aria-current"]:if (page_number == page) "page" else null, *:c.boolean_attr("disabled",~.props.disabled), string(page_number)>],
        <button type:"button", ["data-dtna-page"]:string(page + 1), ["aria-label"]:"Next page",
            *:c.boolean_attr("disabled",~.props.disabled or page == total), "›">]>
}
on click(evt) {
    let target = dom.closest(evt.target,"[data-dtna-page]")
    if (target == null or ~.props.disabled or dom.get_state(target,"disabled")) { return 'pass' }
    let page = int(dom.get_attribute(target,"data-dtna-page")) or 1
    current = page
    emit("ui_change",c.action(~,'page',{current:page,page_size:c.option(~.props,"page_size",10)}))
    'pass'
}
