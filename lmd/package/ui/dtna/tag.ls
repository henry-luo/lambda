import dom
import c: lambda.ui.core.component
import tokens: .tokens
import icons: .icons

// preset seeds belong to the pinned Ant light palette; semantic colors remain inherited tokens.
let presets = {magenta:"#eb2f96",pink:"#eb2f96",red:"#f5222d",volcano:"#fa541c",orange:"#fa8c16",
    gold:"#faad14",yellow:"#fadb14",lime:"#a0d911",green:"#52c41a",cyan:"#13c2c2",
    blue:"#1677ff",geekblue:"#2f54eb",purple:"#722ed1"}
let semantics = {success:"success",processing:"primary",info:"primary",error:"error",warning:"warning"}
fn color_name(props) string^ => c.text(c.option(props,"color",props.status))^
fn variant(props) string^ => c.text(c.option(props,"variant",if (props.bordered == true) "outlined" else "filled"))^
pub fn descriptor(kind, props, child) element^ {
    let checkable = kind == 'checkable-tag'
    let names = c.properties(props,[*c.common_props,"disabled","icon",*(if (checkable)
        ["checked","default_checked"] else ["color","status","variant","bordered","closable","close_icon","visible","default_visible","href","target"])],kind,true)^
    let flags = c.boolean_props(props,["disabled","bordered","closable","visible","default_visible"],kind)^
    let color = color_name(props)^;
    if (c.has(props,"visible") and c.has(props,"default_visible")) raise c.fail(kind,"visible and default_visible are mutually exclusive")
    else if (props.href != null and not (props.href is string)) raise c.fail(kind,"href must be a string")
    else if (props.target != null and not (props.target is string)) raise c.fail(kind,"target must be a string")
    else if (color != "" and color != "default" and not c.has(presets,color) and not c.has(semantics,color) and
        not (if (variant(props)^ == "solid") c.css_color(color) else tokens.color(color)))
        raise c.fail(kind,"custom filled/outlined colors require #rrggbb; solid accepts a CSS color")
    else c.node(kind,props,child,if (checkable) ["disabled","icon","checked","default_checked"]
        else ["disabled","icon","color","status","variant","bordered","closable","close_icon","visible","default_visible","href","target"])^
}
fn colors(props) {
    let color = color_name(props)^
    let style = variant(props)^
    let semantic = semantics[color]
    let palette = if (c.has(presets,color)) tokens.palette(presets[color])^ else null
    let foreground = if (semantic != null) "var(--dtna-" ++ semantic ++ ")" else if (palette != null) palette[6] else color
    let background = if (semantic != null) "var(--dtna-" ++ semantic ++ "-background)" else if (palette != null) palette[0]
        else if (tokens.color(color)) tokens.color_lightness(color,0.95)^ else null
    let border = if (semantic != null) "var(--dtna-" ++ semantic ++ "-border)" else if (palette != null) palette[2] else color;
    if (color == "" or color == "default") ""
    else "color:" ++ (if (style == "solid") "#fff" else foreground) ++ ";background-color:" ++
        (if (style == "solid") (if (palette != null) palette[5] else foreground) else background) ++ ";border-color:" ++
        (if (style == "solid") (if (palette != null) palette[5] else foreground) else if (style == "outlined") border else "transparent") ++ ";"
}
fn parts(node) => [if (c.props(node).icon != null) <span class:"dtna-tag-icon",c.render(c.props(node).icon)>,
    <span class:"dtna-tag-content",*c.contents(node)>]
fn presentation(node) {
    let p = c.props(node)
    let body = [*parts(node),if (p.closable) <button type:"button",class:"dtna-tag-close",["aria-label"]:"Close",
        *:c.boolean_attr("disabled",p.disabled),c.render(c.option(p,"close_icon",icons.descriptor({name:"close",width:10,height:10})^))>]
    let attributes = {*:c.styled(node,"dtna-tag-" ++ variant(p)^ ++ (if (p.disabled) " dtna-tag-disabled" else "")),
        ["aria-disabled"]:if (p.disabled) "true" else null,
        style:c.style_with(p,if (p.disabled) "" else colors(p))};
    if (p.href != null) <a *:attributes,href:if (p.disabled) null else p.href,target:p.target,*body>
    else <span *:attributes,*body>
}
view dtna_tag: <dtna.tag> state visible:c.option(c.props(~),"default_visible",true) {
    if (c.option(c.props(~),"visible",visible)) presentation(~) else <span hidden:"">
}
on click(evt) {
    if (c.props(~).disabled) { return 'prevent-default' }
    if (c.props(~).closable and dom.closest(evt.target,".dtna-tag-close") != null) {
        // a controlled owner declines closing by retaining visible:true; requests cannot mutate its prop.
        if (not c.has(c.props(~),"visible")) { visible = false }
        emit("ui_action",c.action(~,'close',false))
        return 'prevent-default'
    }
    'pass'
}
view dtna_checkable_tag: <dtna.checkable_tag> state checked:c.option(c.props(~),"default_checked",false) {
    let selected = c.option(c.props(~),"checked",checked);
    <button *:c.styled(~,"dtna-tag dtna-tag-checkable" ++ (if (selected) " dtna-tag-checked" else "")),
        type:"button",role:"checkbox",["aria-checked"]:c.aria(selected),["aria-disabled"]:c.aria(c.props(~).disabled),
        *:c.boolean_attr("disabled",c.props(~).disabled),*parts(~)>
}
on click(evt) {
    if (c.props(~).disabled) { return 'prevent-default' }
    let next = not c.option(c.props(~),"checked",checked)
    if (not c.has(c.props(~),"checked")) { checked = next }
    emit("ui_change",c.action(~,'check',next))
    'pass'
}
pub let css = "
.dtna-tag{display:inline-block;position:relative;height:auto;padding:0 7px;font-size:calc(var(--dtna-font-size) - 2px);line-height:calc(var(--dtna-font-size) + 6px);white-space:nowrap;border:1px solid transparent;border-radius:max(0px,calc(var(--dtna-radius) - 2px));text-align:start;background:var(--dtna-disabled-background);color:var(--dtna-text);text-decoration:none;vertical-align:baseline}
.dtna-tag-outlined{border-color:var(--dtna-border)}.dtna-tag-solid{background:#000;color:#fff}
.dtna-tag-icon{display:inline-flex;align-items:center;line-height:0;vertical-align:-0.125em}.dtna-tag-icon+.dtna-tag-content{margin-inline-start:7px}.dtna-tag-icon .dtna-icon{width:1em;height:1em}
.dtna-tag-close{display:inline-flex;align-items:center;vertical-align:-0.125em;margin-inline-start:3px;padding:0;border:0;background:transparent;color:var(--dtna-text-secondary);cursor:pointer;font:inherit;font-size:10px;line-height:0}
.dtna-tag-close:hover{color:var(--dtna-text)}.dtna-tag-close:disabled{color:var(--dtna-disabled-text);cursor:not-allowed}
.dtna-tag-disabled{color:var(--dtna-disabled-text);background:var(--dtna-disabled-background);cursor:not-allowed}.dtna-tag-disabled.dtna-tag-outlined{border-color:var(--dtna-border)}
.dtna-tag-checkable{font-family:inherit;cursor:pointer;background:transparent;border-color:transparent}
.dtna-tag-checkable:not(.dtna-tag-checked):hover{color:var(--dtna-primary);background:var(--dtna-disabled-background)}
.dtna-tag-checked{color:#fff;background:var(--dtna-primary)}.dtna-tag-checked:hover{background:var(--dtna-primary-hover)}
.dtna-tag-checkable:active{color:#fff;background:var(--dtna-primary-active)}
.dtna-tag-checkable:disabled{color:var(--dtna-disabled-text);background:transparent;cursor:not-allowed}
.dtna-tag-checkable.dtna-tag-checked:disabled{background:var(--dtna-disabled-background)}
.dtna-tag-checkable:focus-visible,.dtna-tag-close:focus-visible{outline:2px solid var(--dtna-primary);outline-offset:2px}
"
