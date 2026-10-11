import dom
import c: lambda.ui.core.component
import collection: lambda.ui.core.collection
import interaction: lambda.ui.core.interaction
import general: .general

pub fn flatten(items, parent = null, ancestors = []) array^ {
    let valid = collection.validate(items,"tree")^
    let flags = [for (item in items) c.boolean_props(item,["disable_checkbox"],"tree item")^]
    let children = [for (item in items where item.children != null and not (item.children is array)) item];
    if (len(children) > 0) raise c.fail("tree","children must be arrays")
    else [for (position,item in items) for (entry in [
        {key:item.key,item:item,disabled:item.disabled == true,parent:parent,ancestors:ancestors,
            level:len(ancestors)+1,position:position+1,siblings:len(items),branch:len(item.children) > 0},
        *flatten(if (item.children == null) [] else item.children,item.key,[*ancestors,item.key])^]) entry]
}
pub fn visible(rows, expanded) => [for (row in rows where all([for (key in row.ancestors) contains(expanded,key)])) row]
fn children(rows, key) => [for (row in rows where row.parent == key) row]
fn checkable(row) => not row.disabled and row.item.disable_checkbox != true
fn check_state(rows, row, keys, strict) {
    let eligible = [for (child in children(rows,row.key) where checkable(child)) child]
    let states = if (strict or not checkable(row)) [] else [for (child in eligible) check_state(rows,child,keys,false)];
    if (len(states) == 0) (if (contains(keys,row.key)) "true" else "false")
    else if (all([for (status in states) status == "true"])) "true"
    else if (any([for (status in states) status != "false"])) "mixed"
    else "false"
}
fn conducted(rows, key) {
    let matches = [for (row in rows where row.key == key) row]
    let row = matches[0];
    if (row == null or not checkable(row)) []
    else [key,*[for (child in children(rows,key)) for (candidate in conducted(rows,child.key)) candidate]]
}
fn checked_result(rows, keys, strict) {
    let states = [for (row in rows) {key:row.key,state:check_state(rows,row,keys,strict)}];
    {checked_keys:[for (row in states where row.state == "true") row.key],
     half_checked_keys:[for (row in states where row.state == "mixed") row.key]}
}
pub fn checks(rows, keys, strict = false) {
    let expanded = if (strict) keys else unique([*keys,*[for (key in keys) for (candidate in conducted(rows,key)) candidate]]);
    checked_result(rows,expanded,strict)
}
pub fn toggle_check(rows, keys, key, strict = false) {
    let current = checks(rows,keys,strict)
    let row = [for (entry in rows where entry.key == key) entry][0]
    let affected = if (strict) [key] else conducted(rows,key)
    let next = collection.set_keys(current.checked_keys,affected,not contains(current.checked_keys,key))
    // remove stale parent keys before recalculating conduction from their children.
    let raw = if (strict) next else [for (entry in next where not contains(row.ancestors,entry)) entry];
    if (row == null or not checkable(row)) current else checked_result(rows,raw,strict)
}
pub fn tree(props) element^ {
    let node = c.node('tree',props,null,["items","expanded_keys","default_expanded_keys","selected_keys","default_selected_keys",
        "checked_keys","default_checked_keys","checkable","check_strictly","multiple","disabled"])^
    let identity = collection.require_id(props,"tree")^
    let flags = c.boolean_props(props,["checkable","check_strictly","multiple"],"tree")^
    let rows = flatten(props.items)^
    let unique_keys = collection.validate(rows,"tree")^
    let selections = [for (name in ["expanded","selected","checked"]) collection.key_props(rows,props,name ++ "_keys","default_" ++ name ++ "_keys","tree")^];
    if (not props.multiple and (len(c.option(props,"selected_keys",[])) > 1 or len(c.option(props,"default_selected_keys",[])) > 1))
        raise c.fail("tree","single selection allows at most one key")
    else node
}
fn keys(node, name, current) => c.option(c.props(node),name ++ "_keys",current)
fn transition(node, rows, expanded, selected, checked, key, action) {
    let next_expanded = if (action == 'expand') collection.toggle(expanded,key) else expanded
    let next_selected = if (action == 'select') (if (c.props(node).multiple) collection.toggle(selected,key) else [key]) else selected
    let next_checked = if (action == 'check') toggle_check(rows,checked,key,c.props(node).check_strictly == true) else checks(rows,checked,c.props(node).check_strictly == true);
    {expanded:next_expanded,selected:next_selected,checked:next_checked.checked_keys,
        value:if (action == 'expand') {key:key,expanded_keys:next_expanded,expanded:contains(next_expanded,key)}
        else if (action == 'select') {key:key,selected_keys:next_selected,selected:contains(next_selected,key)}
        else {key:key,*:next_checked,checked:contains(next_checked.checked_keys,key)}}
}
pn focus_key(node, evt, rows, key) {
    let found = [for (index,row in rows where row.key == key) index];
    if (len(found) == 1) interaction.focus(node,evt,"[data-dtna-tree-node='" ++ string(found[0]) ++ "']") else false
}
view dtna_tree: <dtna.tree> state expanded:c.option(c.props(~),"default_expanded_keys",[]),
    selected:c.option(c.props(~),"default_selected_keys",[]),checked:c.option(c.props(~),"default_checked_keys",[]),focused:null {
    let rows = flatten(c.props(~).items)^
    let opened = keys(~,"expanded",expanded)
    let chosen = keys(~,"selected",selected)
    let checked_model = checks(rows,keys(~,"checked",checked),c.props(~).check_strictly == true)
    let shown = visible(rows,opened)
    let tab_key = if (any([for (row in collection.enabled(shown)) row.key == focused])) focused else collection.enabled(shown)[0].key;
    <div *:c.attrs(c.props(~)),class:c.classes('tree',c.props(~)),style:c.props(~).style,role:"tree",
        ["aria-label"]:c.props(~).label,["aria-multiselectable"]:c.aria(c.props(~).multiple),
        *[for (index,row in rows where any([for (entry in shown) entry.key == row.key]))
            <div id:c.props(~).id ++ "-node-" ++ string(index),class:"dtna-tree-node" ++ (if (contains(chosen,row.key)) " dtna-tree-selected" else ""),
                role:"treeitem",tabindex:if (row.key == tab_key and not c.props(~).disabled) "0" else "-1",
                ["data-dtna-tree-node"]:string(index),["aria-level"]:string(row.level),["aria-posinset"]:string(row.position),["aria-setsize"]:string(row.siblings),
                ["aria-disabled"]:c.aria(c.props(~).disabled or row.disabled),["aria-selected"]:c.aria(contains(chosen,row.key)),
                *:(if (row.branch) {'aria-expanded':c.aria(contains(opened,row.key))} else {}),
                *:(if (c.props(~).checkable) {'aria-checked':if (contains(checked_model.half_checked_keys,row.key)) "mixed" else c.aria(contains(checked_model.checked_keys,row.key))} else {}),
                style:"padding-left:" ++ c.px((row.level - 1)*24), *[
                if (row.branch) <button type:"button",tabindex:"-1",class:"dtna-tree-expander",["data-dtna-tree-expand"]:"",
                    ["aria-label"]:if (contains(opened,row.key)) "Collapse" else "Expand",*:c.boolean_attr("disabled",c.props(~).disabled or row.disabled),
                    general.icon({name:"chevron-down",style:if (contains(opened,row.key)) "" else "transform:rotate(-90deg);"})^>
                else <span class:"dtna-tree-spacer">,
                if (c.props(~).checkable) <span class:"dtna-check",["data-dtna-tree-check"]:"",["aria-hidden"]:"true",
                    if (contains(checked_model.half_checked_keys,row.key)) "−" else if (contains(checked_model.checked_keys,row.key)) "✓" else ""> else null,
                <span class:"dtna-tree-label",c.render(row.item.label)>]>]>
}
on click(evt) {
    let target = interaction.target(~,evt,"[data-dtna-tree-node]")
    if (target == null or c.props(~).disabled) { return 'pass' }
    let rows = flatten(c.props(~).items)^
    let row = rows[int(dom.get_attribute(target,"data-dtna-tree-node")) or 0]
    if (row.disabled) { return 'pass' }
    let action = if (dom.closest(evt.target,"[data-dtna-tree-expand]") != null) 'expand'
        else if (c.props(~).checkable and dom.closest(evt.target,"[data-dtna-tree-check]") != null) 'check' else 'select'
    if (action == 'select' and dom.closest(evt.target,"input,textarea,select,button,a") != null) { return 'pass' }
    if (action == 'check' and not checkable(row)) { return 'pass' }
    let next = transition(~,rows,keys(~,"expanded",expanded),keys(~,"selected",selected),keys(~,"checked",checked),row.key,action)
    expanded = next.expanded
    selected = next.selected
    checked = next.checked
    focused = row.key
    dom.focus_set(target,false)
    emit("ui_change",c.action(~,action,next.value))
    'pass'
}
on keydown(evt) {
    let target = interaction.target(~,evt,"[data-dtna-tree-node]")
    if (target == null or c.props(~).disabled or dom.closest(evt.target,"input,textarea,select,button") != null) { return 'pass' }
    let rows = flatten(c.props(~).items)^
    let row = rows[int(dom.get_attribute(target,"data-dtna-tree-node")) or 0]
    if (row.disabled) { return 'pass' }
    let opened = keys(~,"expanded",expanded)
    let shown = visible(rows,opened)
    let action = if (evt.key == "Enter") 'select' else if (evt.key == " ") (if (c.props(~).checkable) 'check' else 'select')
        else if (row.branch and ((evt.key == "ArrowRight" and not contains(opened,row.key)) or (evt.key == "ArrowLeft" and contains(opened,row.key)))) 'expand' else null
    if (action != null) {
        if (action == 'check' and not checkable(row)) { return 'pass' }
        let next = transition(~,rows,opened,keys(~,"selected",selected),keys(~,"checked",checked),row.key,action)
        expanded = next.expanded
        selected = next.selected
        checked = next.checked
        emit("ui_change",c.action(~,action,next.value))
    } else {
        let enabled = collection.enabled(shown)
        let here = [for (index,entry in enabled where entry.key == row.key) index][0]
        let candidates = [for (offset in 1 to len(enabled),let entry = enabled[(here+offset) % len(enabled)]
            where starts_with(lower(c.text(entry.item.label)),lower(evt.key))) entry.key]
        let next_key = if (contains(["ArrowUp","ArrowDown","Home","End"],evt.key)) collection.move(shown,row.key,evt.key)
            else if (evt.key == "ArrowLeft") row.parent
            else if (evt.key == "ArrowRight" and row.branch) collection.enabled(children(rows,row.key))[0].key
            else if (len(evt.key) == 1 and evt.key != " ") candidates[0] else null
        if (next_key == null or not any([for (entry in collection.enabled(shown)) entry.key == next_key])) { return 'pass' }
        focused = next_key
        focus_key(~,evt,rows,next_key)
    }
    'prevent-default'
}
