import dom
import c: lambda.ui.core.component
import icons: .icons

pub fn descriptor(props) element^ {
    let names = c.properties(props,[*c.common_props,"value","default_value","count","allow_half","allow_clear",
        "keyboard","disabled","readonly","size","character","tooltips","name"],"rate",true)^
    let flags = c.boolean_props(props,["allow_half","allow_clear","keyboard","disabled","readonly"],"rate")^
    let count = c.option(props,"count",5)
    let value = c.option(props,"value",c.option(props,"default_value",0));
    if (not (count is int) or count < 1) raise c.fail("rate","count must be a positive int")
    else if (not c.finite(value) or value < 0 or value > count) raise c.fail("rate","value must be finite and within count")
    else if (props.tooltips != null and (not (props.tooltips is array) or
        any([for (tooltip in props.tooltips) not (tooltip is string)]))) raise c.fail("rate","tooltips must be an array of strings")
    else if (props.name != null and not (props.name is string)) raise c.fail("rate","name must be a string")
    else c.node('rate',props,null,["value","default_value","count","allow_half","allow_clear","keyboard","disabled",
        "readonly","size","character","tooltips","name"])^
}
fn character_node(props,index,value) {
    let character = props.character;
    // the icon's zero-line-height wrapper matches the reference's atomic baseline.
    if (not c.has(props,"character")) <span class:"dtna-rate-icon",c.render(icons.descriptor({name:"star",theme:'filled'})^)>
    else c.render(if (character is fn) character({index:index,count:c.option(props,"count",5),value:value,disabled:props.disabled}) else character)
}
fn locked(props) => props.disabled or props.readonly
fn stars(node,value,shown) => [for (index in 0 to c.option(c.props(node),"count",5)-1)
    <button type:"button",class:"dtna-rate-star" ++ (if (index + 1 <= shown) " dtna-rate-full"
        else if (c.props(node).allow_half and shown + 0.5 >= index + 1) " dtna-rate-half" else " dtna-rate-zero"),
        role:"radio",["aria-checked"]:c.aria(value > index),["aria-posinset"]:c.text(index + 1),
        ["aria-setsize"]:c.text(c.option(c.props(node),"count",5)),["aria-label"]:if (c.props(node).tooltips[index] != null) c.props(node).tooltips[index] else c.text(index + 1),
        ["data-dtna-rate-index"]:c.text(index),title:c.props(node).tooltips[index],
        tabindex:if (locked(c.props(node))) -1 else 0,*:c.boolean_attr("disabled",c.props(node).disabled),
        <span class:"dtna-rate-first",["aria-hidden"]:"true",character_node(c.props(node),index,shown)>
        <span class:"dtna-rate-second",["aria-hidden"]:"true",character_node(c.props(node),index,shown)>>]
fn current(node,selected) => c.option(c.props(node),"value",selected)
pn pointer_value(node,evt) {
    let star = dom.closest(evt.target,".dtna-rate-star");
    if (star == null) null else (
        let index = int(dom.get_attribute(star,"data-dtna-rate-index"))^,
        let rect = dom.bounding_box(star),
        let rtl = dom.computed_style(star,"direction") == "rtl",
        let half = c.props(node).allow_half and rect != null and
            (if (rtl) evt.x - rect.x > rect.width / 2 else evt.x - rect.x < rect.width / 2),
        index + 1 - (if (half) 0.5 else 0)
    )
}
view dtna_rate: <dtna.rate> state selected:c.option(c.props(~),"default_value",0),hover:null,cleaned:null {
    let value = current(~,selected);
    <div *:c.styled(~,if (locked(c.props(~))) "dtna-rate-locked" else ""),role:"radiogroup",
        ["aria-label"]:c.props(~).label,["aria-disabled"]:c.aria(c.props(~).disabled),["aria-readonly"]:c.aria(c.props(~).readonly),
        ["data-value"]:c.text(value),*[*stars(~,value,if (hover == null) value else hover),
        if (c.props(~).name != null) <input type:"hidden",name:c.props(~).name,value:c.text(value),*:c.boolean_attr("disabled",c.props(~).disabled)>]>
}
on click(evt) {
    if (locked(c.props(~))) { return 'prevent-default' }
    let requested = pointer_value(~,evt)
    if (requested == null) { return 'pass' }
    let reset = c.option(c.props(~),"allow_clear",true) and requested == current(~,selected)
    let next = if (reset) 0 else requested
    if (not c.has(c.props(~),"value")) { selected = next }
    hover = null
    cleaned = if (reset) requested else null
    emit("ui_change",c.action(~,'rate',next))
    'pass'
}
on mousemove(evt) {
    if (locked(c.props(~))) { return 'pass' }
    let requested = pointer_value(~,evt)
    if (requested != null and requested != cleaned and requested != hover) {
        hover = requested
        cleaned = null
        emit("ui_hover",c.action(~,'rate',requested))
    }
    'pass'
}
on mouseleave(evt) {
    if (hover != null) { emit("ui_hover",c.action(~,'rate',null)) }
    hover = null
    cleaned = null
    'pass'
}
on keydown(evt) {
    if (locked(c.props(~)) or not c.option(c.props(~),"keyboard",true)) { return 'pass' }
    if (not contains(["ArrowLeft","ArrowRight"],evt.key)) { return 'pass' }
    let rtl = dom.computed_style(evt.target,"direction") == "rtl"
    let increase = (evt.key == "ArrowRight") != rtl
    let step = if (c.props(~).allow_half) 0.5 else 1
    let next = max(0,min(c.option(c.props(~),"count",5),current(~,selected) + (if (increase) step else -step)))
    if (next != current(~,selected)) {
        if (not c.has(c.props(~),"value")) { selected = next }
        hover = null
        cleaned = null
        emit("ui_change",c.action(~,'rate',next))
    }
    'prevent-default'
}
pub let css = "
.dtna-rate{display:inline-flex;align-items:center;gap:8px;margin:0;padding:0;color:#fadb14;font-size:20px;line-height:1;vertical-align:middle}
.dtna-rate.dtna-size-small{font-size:15px}.dtna-rate.dtna-size-large{font-size:25px}
.dtna-rate-star{display:inline-block;position:relative;margin:0;padding:0;border:0;background:transparent;font:inherit;line-height:1;color:inherit;cursor:pointer;text-align:start}
.dtna-rate-first,.dtna-rate-second{display:block;color:rgba(0,0,0,0.06);user-select:none}
.dtna-rate-first{position:absolute;top:0;inset-inline-start:0;width:50%;height:100%;overflow:hidden;opacity:0}
.dtna-rate .dtna-icon{width:1em;height:1em}
.dtna-rate-icon{display:inline-flex;align-items:center;line-height:0;vertical-align:-0.125em}
.dtna-rate-full .dtna-rate-second,.dtna-rate-half .dtna-rate-first{color:inherit}.dtna-rate-half .dtna-rate-first{opacity:1}
.dtna-rate-star:hover{transform:scale(1.1)}.dtna-rate-star:focus-visible{outline:1px dashed #fadb14;outline-offset:2px}
.dtna-rate-locked .dtna-rate-star{cursor:default;transform:none}
"
