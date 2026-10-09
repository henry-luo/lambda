import dom
import c: lambda.ui.core.component
import collection: lambda.ui.core.collection
import interaction: lambda.ui.core.interaction
import general: .general

pub fn collapse(props) element^ {
    let node = c.node('collapse',props,null,["items","active_keys","default_active_keys","accordion","disabled"])^
    let identity = collection.require_id(props,"collapse")^
    let items = collection.validate(props.items,"collapse")^
    let flags = c.boolean_props(props,["accordion"],"collapse")^
    let keys = collection.key_props(props.items,props,"active_keys","default_active_keys","collapse")^;
    if (props.accordion and (len(c.option(props,"active_keys",[])) > 1 or len(c.option(props,"default_active_keys",[])) > 1)) raise c.fail("collapse","accordion allows at most one active key")
    else node
}
fn active(node, current) => c.option(node.props,"active_keys",current)
view dtna_collapse: <dtna kind:'collapse'> state current:c.option(~.props,"default_active_keys",[]) {
    let keys = active(~,current);
    <div *:c.attrs(~.props), class:c.classes('collapse',~.props), style:~.props.style,
        *[for (index,item in ~.props.items) <section class:"dtna-collapse-item",
            *[<div class:"dtna-collapse-header", *[
                <button id:~.props.id ++ "-header-" ++ string(index), type:"button", ["data-dtna-collapse"]:string(index),
                    ["aria-expanded"]:c.aria(contains(keys,item.key)), ["aria-controls"]:~.props.id ++ "-panel-" ++ string(index),
                    *:c.boolean_attr("disabled",~.props.disabled or item.disabled),
                    *[general.icon({name:"chevron-down",style:if (contains(keys,item.key)) "" else "transform:rotate(-90deg);"})^,c.render(item.label)]>,
                <span class:"dtna-collapse-extra",c.render(item.extra)>]>,
            <div id:~.props.id ++ "-panel-" ++ string(index), role:"region", class:"dtna-collapse-panel",
                ["aria-labelledby"]:~.props.id ++ "-header-" ++ string(index),
                *:c.boolean_attr("hidden",not contains(keys,item.key)),style:if (contains(keys,item.key)) null else "display:none;",
                c.render(item.children)>]>]>
}
on click(evt) {
    let target = interaction.target(~,evt,"[data-dtna-collapse]")
    if (target == null or ~.props.disabled or dom.get_state(target,"disabled")) { return 'pass' }
    let index = int(dom.get_attribute(target,"data-dtna-collapse")) or 0
    let key = ~.props.items[index].key
    let next = collection.toggle(active(~,current),key,not ~.props.accordion)
    current = next
    emit("ui_change",c.action(~,'expand',{key:key,active_keys:next,expanded:contains(next,key)}))
    'pass'
}
on keydown(evt) {
    let target = interaction.target(~,evt,"[data-dtna-collapse]")
    if (target == null or ~.props.disabled or not contains(["ArrowUp","ArrowDown","Home","End"],evt.key)) { return 'pass' }
    let index = int(dom.get_attribute(target,"data-dtna-collapse")) or 0
    let key = collection.move(~.props.items,~.props.items[index].key,evt.key)
    let indices = [for (i,item in ~.props.items where item.key == key) i]
    if (len(indices) == 1) { interaction.focus(~,evt,"[data-dtna-collapse='" ++ string(indices[0]) ++ "']") }
    'prevent-default'
}
