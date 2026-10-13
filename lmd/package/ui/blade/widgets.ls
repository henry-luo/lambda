import c: lambda.ui.core.component
import interaction: lambda.ui.core.interaction
import shared: lambda.ui.core.catalog
import art: .art

pub let kinds = ["panel","meter","item-slot","ability","rpg-icon"]
let fields = {panel:["material","extra"],meter:["resource","value","max","show_value"],
    ["item-slot"]:["icon","rarity","selected","quantity","disabled","size"],
    ability:["icon","hotkey","cooldown","disabled","size"],["rpg-icon"]:["name","size"]}
let tags = {panel:(p,k) => <blade.panel *:p,*k>,meter:(p,k) => <blade.meter *:p,*k>,
    ["item-slot"]:(p,k) => <blade.item_slot *:p,*k>,ability:(p,k) => <blade.ability *:p,*k>,
    ["rpg-icon"]:(p,k) => <blade.rpg_icon *:p,*k>}
pub fn descriptor(kind, props = {}, child = null) element^ {
    let names = c.properties(props,[*c.common_props,*fields[string(kind)]],kind,true,"blade")^
    let flags = c.boolean_props(props,["disabled","selected","show_value"],kind,"blade")^
    let icon = if (kind == "rpg-icon") props.name else props.icon
    let maximum = c.option(props,"max",100)
    let value = c.option(props,"value",0);
    if (kind != "panel" and len(c.children(child)) > 0) raise c.fail(kind,"component does not accept children","blade")
    else if (c.has(props,"size") and (not c.finite(props.size) or props.size <= 0)) raise c.fail(kind,"size must be finite and positive","blade")
    else if ((icon != null or kind == "rpg-icon" or kind == "ability") and not contains(art.names(),c.text(icon)))
        raise c.fail(kind,"unknown RPG icon","blade")
    else if (kind == "panel" and not c.enum_valid(props.material,["wood","iron","parchment"])^) raise c.fail(kind,"invalid material","blade")
    else if (kind == "meter" and not c.enum_valid(props.resource,["health","mana","stamina","experience"])^) raise c.fail(kind,"invalid resource","blade")
    else if (kind == "meter" and (not c.finite(maximum) or maximum <= 0 or not c.finite(value) or value < 0 or value > maximum))
        raise c.fail(kind,"value must be between zero and a finite positive max","blade")
    else if (kind == "item-slot" and not c.enum_valid(props.rarity,["common","uncommon","rare","epic","legendary"])^) raise c.fail(kind,"invalid rarity","blade")
    else if (c.has(props,"quantity") and (not (props.quantity is int) or props.quantity < 1)) raise c.fail(kind,"quantity must be a positive int","blade")
    else if (c.has(props,"cooldown") and (not c.finite(props.cooldown) or props.cooldown < 0 or props.cooldown > 100))
        raise c.fail(kind,"cooldown must be a percentage from 0 to 100","blade")
    else if (c.has(props,"hotkey") and not (props.hotkey is string or props.hotkey is symbol)) raise c.fail(kind,"hotkey must be text","blade")
    else tags[string(kind)](props,c.children(child))
}
fn checked(node) element^ => descriptor(c.text(c.kind(node)),c.props(node),content(node))^
pub fn native(kind, props, children = []) element^ {
    let node = shared.native(kind,props,children,"blade")^
    let preset = c.text(c.option(props,"image",'default'));
    // replace built-in artwork only; custom images and explicit absence keep their contract.
    if (kind != "empty" or not contains(["default","simple"],preset)) node else
        <dtna.empty *:c.props(node),image:art.empty(preset),
            image_height:c.option(props,"image_height",if (preset == "simple") 40 else 100),*content(node)>
}
fn lower_component(kind, props, children) element^ =>
    if (kind == "empty") native(kind,props,children)^ else descriptor(kind,props,children)^
pub fn lower_slots(value) any^ => shared.lower_slots(value,"blade",["page","config-provider"],{kinds:[*kinds,"empty"],build:lower_component})^
fn corners() => [for (position in ["tl","tr","bl","br"]) art.corner(position)]

// cache lowered children so nested uncontrolled collections retain their descriptor identity.
view blade_panel: <blade.panel> state lowered:(lower_slots(~)^) {
    let node = lowered
    let p = c.props(node);
    <section *:c.styled(node,"blade-material-" ++ c.text(p.material or "wood"),"blade"),*[
        *corners(),
        if (p.title != null or p.extra != null) <header class:"blade-panel-header",*[
            <h2 class:"blade-panel-title",c.render(p.title)>,
            if (p.extra == null) null else <div class:"blade-panel-extra",c.render(p.extra)>]> else null,
        <div class:"blade-panel-body",*c.contents(node)>]>
}
view blade_meter: <blade.meter> {
    let node = checked(~)^
    let p = c.props(node)
    let kind = c.text(p.resource or "health")
    let label = c.option(p,"label",{health:"Health",mana:"Mana",stamina:"Stamina",experience:"Experience"}[kind])
    let maximum = c.option(p,"max",100)
    let value = c.option(p,"value",0);
    <div *:c.styled(node,"blade-meter-" ++ kind,"blade"),role:"progressbar",["aria-label"]:label,
        ["aria-valuemin"]:"0",["aria-valuemax"]:string(maximum),["aria-valuenow"]:string(value),
        <div class:"blade-meter-heading",<span label>>
        <div class:"blade-meter-shell",*[
            <div class:"blade-meter-rail",<div class:"blade-meter-fill",style:"width:" ++ string(value/maximum*100) ++ "%;">>,
            art.meter_frame(),
            if (c.option(p,"show_value",true)) <span class:"blade-meter-value",string(value) ++ " / " ++ string(maximum)> else null]>>
}
view blade_rpg_icon: <blade.rpg_icon> {
    let node = checked(~)^
    let p = c.props(node);
    art.icon(p.name,c.option(p,"size",64),p.label,{*:c.attrs(p),class:p.class,style:p.style})
}
fn button(node) {
    let p = c.props(node)
    let ability = c.kind(node) == 'ability'
    let disabled = p.disabled == true or (ability and c.option(p,"cooldown",0) > 0)
    let label = c.option(p,"label",if (p.icon != null) replace(c.text(p.icon),"-"," ") else "Empty equipment slot")
    let size = if (c.has(p,"size")) "width:" ++ c.px(p.size) ++ ";height:" ++ c.px(p.size) ++ ";" else ""
    let rarity = c.text(p.rarity or "common");
    <button *:c.styled(node,if (ability) "" else "blade-rarity-" ++ rarity,"blade"),type:"button",
        style:size ++ c.text(p.style),*:c.boolean_attr("disabled",disabled),
        ["aria-label"]:label,["aria-pressed"]:if (ability) null else c.aria(p.selected),
        ["aria-disabled"]:c.aria(disabled),["data-rarity"]:if (ability) null else rarity,
        ["data-cooldown"]:if (ability) string(c.option(p,"cooldown",0)) else null,*[
        if (p.icon == null) <span class:"blade-empty-slot",["aria-hidden"]:"true","◇"> else art.icon(p.icon,64),
        if (not ability and p.quantity != null) <span class:"blade-slot-quantity",string(p.quantity)> else null,
        if (ability and c.option(p,"cooldown",0) > 0) <span class:"blade-cooldown",["aria-hidden"]:"true",style:"height:" ++ string(p.cooldown) ++ "%;"> else null,
        if (ability and p.hotkey != null) <kbd class:"blade-hotkey",p.hotkey> else null]>
}
// native buttons supply Enter/Space activation; descriptors own effects only in handlers (S12.1.3).
view blade_slot_button: <blade.item_slot> | <blade.ability> { button(checked(~)^) }
on click(evt) {
    if (interaction.root(~,evt,"blade") == null) { return 'pass' }
    if (c.props(~).disabled or (c.kind(~) == 'ability' and c.option(c.props(~),"cooldown",0) > 0)) { return 'prevent-default' }
    emit("ui_action",c.action(~,'activate',c.props(~).icon))
    'pass'
}
