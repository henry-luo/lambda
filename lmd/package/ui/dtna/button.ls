import dom
import c: lambda.ui.core.component
import interaction: lambda.ui.core.interaction
import contract: .contract
import icons: .icons

let properties = ["size","disabled","variant","danger","shape","block","icon","type","loading",
    "icon_placement","class_names","styles","href","target","rel","download","auto_focus","ghost","auto_insert_space"]
let semantic_parts = ["root","icon","content"]
type two_chinese_chars = \(("一" to "龥"){2})
fn loading_config(props) => if (props.loading is map)
    {active:true,delay:c.option(props.loading,"delay",0),icon:props.loading.icon}
    else {active:props.loading == true,delay:0,icon:null}
fn settings(props) => (let config = loading_config(props), {active:config.active,delay:config.delay})
pub fn descriptor(props, child) element^ {
    let names = c.properties(props,[*c.common_props,*properties],'button',true)^
    let flags = c.boolean_props(props,["disabled","danger","block","auto_focus","ghost","auto_insert_space"],'button')^
    let parts = c.validate_parts(props,semantic_parts,'button')^
    let native = contract.validate('button',props)^;
    if (c.has(props,"loading") and not (props.loading is bool or props.loading is map)) raise c.fail('button',"loading must be bool or {delay,icon}")
    else (
        let loading_names = if (props.loading is map) c.properties(props.loading,["delay","icon"],"button.loading")^ else true,
        let delay = loading_config(props).delay,
        if (not c.finite(delay) or delay < 0) raise c.fail('button',"loading delay must be finite nonnegative milliseconds")
        else if (not c.enum_valid(props.icon_placement,["start","end"])^) raise c.fail('button',"icon_placement must be start or end")
        else if (any([for (field in ["href","target","rel","download"] where c.has(props,field)) not (props[field] is string)]))
            raise c.fail('button',"link attributes must be strings")
        else c.node('button',props,child,properties)^
    )
}
fn busy(props, clock) => (let config = settings(props), config.active and
    (config.delay == 0 or (clock.settings == config and clock.shown)))
fn content_parts(node, shown) {
    let p = node.props
    let config = loading_config(p)
    let icon = if (p.icon != null and not shown) p.icon else if (config.active and config.icon != null) config.icon
        else if (shown) icons.descriptor({name:"loading"})^ else null
    let contents = content(node)
    let inserted = c.option(p,"auto_insert_space",true) and p.icon == null and
        not contains(['text','link'],p.variant) and len(contents) == 1 and contents[0] is two_chinese_chars;
    [if (icon != null) <span *:c.part_attrs(p,"icon","dtna-button-icon"),c.render(icon)> else null,
        if (len(contents) > 0) <span *:c.part_attrs(p,"content","dtna-button-content"),
            *(if (inserted) [slice(contents[0],0,1) ++ " " ++ slice(contents[0],1,2)] else c.contents(node))> else null]
}
fn presentation(node, clock) {
    let p = node.props
    let shown = busy(p,clock)
    let root = c.part_attrs(p,"root",c.classes('button',p,(if (p.danger) "dtna-danger" else "") ++
        (if (p.shape == null) "" else " dtna-shape-" ++ c.text(p.shape)) ++ (if (p.block) " dtna-block" else "") ++
        (if (p.icon_placement == 'end') " dtna-button-icon-end" else "") ++
        (if (p.ghost and not contains(['text','link'],p.variant)) " dtna-button-ghost" else "") ++
        (if (p.disabled) " dtna-button-disabled" else "") ++
        (if (len(content(node)) == 0 and (p.icon != null or shown)) " dtna-button-icon-only" else "")))
    let attributes = {*:c.attrs(p),*:root,style:c.text(root.style) ++ c.text(p.style),
        *:(if (p.label == null) {} else {["aria-label"]:p.label}),["aria-busy"]:c.aria(shown),["aria-disabled"]:if (p.disabled or shown) "true" else null,
        *:c.boolean_attr("autofocus",p.auto_focus)};
    if (c.has(p,"href")) <a *:attributes,href:if (p.disabled) null else p.href,target:p.target,rel:p.rel,download:p.download,
        tabindex:if (p.disabled) -1 else c.option(p,"tabindex",0),*content_parts(node,shown)>
    // public enums accept symbols; native HTML attributes require their text value.
    else <button *:attributes,type:c.text(c.option(p,"type","button")),*:c.boolean_attr("disabled",p.disabled),*content_parts(node,shown)>
}
// delayed loading uses the document frame queue, so removal invalidates pending delivery (D7.5.3).
pn advance(node, evt, previous) {
    let config = settings(node.props)
    let changed = config != previous.settings
    let start = if (changed) evt.time_stamp else previous.start
    let shown = config.active and evt.time_stamp-start >= config.delay;
    if (changed and previous.token > 0) { dom.cancel_frame(evt.target,previous.token) }
    {settings:config,start:start,shown:shown,token:if (config.active and not shown) dom.request_frame(evt.target,"dtna_button_tick") else 0}
}
view dtna_button: <dtna kind:'button'> state clock:{settings:null,start:0,shown:false,token:0} {
    presentation(~,clock)
}
on render(evt) {
    if (evt.event_phase == 2 and clock.settings != settings(~.props)) { clock = advance(~,evt,clock) }
    'pass'
}
on dtna_button_tick(evt) {
    if (evt.detail == clock.token and clock.token > 0) { clock = advance(~,evt,clock) }
    'pass'
}
on click(evt) {
    if (interaction.root(~,evt) == null) { return 'pass' }
    if (~.props.disabled or busy(~.props,clock)) { return 'prevent-default' }
    emit("ui_action",c.action(~,'click'))
    'pass'
}
// appearance rules set tokens; one state selector applies each hover/active/disabled policy.
pub let css = "
.dtna-button{--dtna-button-accent:var(--dtna-primary);--dtna-button-accent-hover:var(--dtna-primary-hover);--dtna-button-accent-active:var(--dtna-primary-active);--dtna-button-color:var(--dtna-text);--dtna-button-color-hover:var(--dtna-button-accent-hover);--dtna-button-color-active:var(--dtna-button-accent-active);--dtna-button-border:var(--dtna-border);--dtna-button-border-hover:var(--dtna-button-accent-hover);--dtna-button-border-active:var(--dtna-button-accent-active);--dtna-button-bg:var(--dtna-background);--dtna-button-bg-hover:var(--dtna-button-bg);--dtna-button-bg-active:var(--dtna-button-bg);--dtna-button-shadow:0 2px 0 rgba(0,0,0,0.02)}
.dtna-button{position:relative;display:inline-flex;align-items:center;justify-content:center;gap:8px;cursor:pointer;white-space:nowrap;text-align:center;text-decoration:none;user-select:none;padding-inline:15px;line-height:calc(var(--dtna-font-size) + 8px);border-radius:var(--dtna-button-radius,var(--dtna-radius));color:var(--dtna-button-color);border-color:var(--dtna-button-border);background:var(--dtna-button-bg);box-shadow:var(--dtna-button-shadow)}
.dtna-button.dtna-size-small{padding-inline:7px;--dtna-button-radius:max(0px,calc(var(--dtna-radius) - 2px))}.dtna-button.dtna-size-large{--dtna-button-radius:calc(var(--dtna-radius) + 2px)}
.dtna-button-icon{display:inline-flex;align-items:center;flex:none}.dtna-button-icon-end{flex-direction:row-reverse}
.dtna-button.dtna-button-icon-only{padding-inline:0;width:var(--dtna-control-height)}
.dtna-button[aria-busy=true]{opacity:0.65;cursor:default}
.dtna-button:not(:disabled):not(.dtna-button-disabled):hover{border-color:var(--dtna-button-border-hover);color:var(--dtna-button-color-hover);background:var(--dtna-button-bg-hover)}
.dtna-button:not(:disabled):not(.dtna-button-disabled):active{border-color:var(--dtna-button-border-active);color:var(--dtna-button-color-active);background:var(--dtna-button-bg-active)}
.dtna-button:focus-visible{outline:2px solid var(--dtna-primary-background);outline-offset:1px}
.dtna-button.dtna-danger{--dtna-button-accent:var(--dtna-error);--dtna-button-accent-hover:var(--dtna-error-hover);--dtna-button-accent-active:var(--dtna-error-active);--dtna-button-color:var(--dtna-button-accent);--dtna-button-border:var(--dtna-button-accent);--dtna-button-shadow:0 2px 0 rgba(255,38,5,0.06)}
.dtna-button.dtna-variant-primary{--dtna-button-color:#fff;--dtna-button-color-hover:#fff;--dtna-button-color-active:#fff;--dtna-button-border:transparent;--dtna-button-border-hover:transparent;--dtna-button-border-active:transparent;--dtna-button-bg:var(--dtna-button-accent);--dtna-button-bg-hover:var(--dtna-button-accent-hover);--dtna-button-bg-active:var(--dtna-button-accent-active);--dtna-button-shadow:0 2px 0 rgba(5,145,255,0.1)}
.dtna-button.dtna-danger.dtna-variant-primary{--dtna-button-shadow:0 2px 0 rgba(255,38,5,0.06)}
.dtna-button.dtna-variant-text,.dtna-button.dtna-variant-link{--dtna-button-border:transparent;--dtna-button-border-hover:transparent;--dtna-button-border-active:transparent;--dtna-button-bg:transparent;--dtna-button-bg-disabled:transparent;--dtna-button-border-disabled:transparent;--dtna-button-shadow:none}
.dtna-button.dtna-variant-link{--dtna-button-color:var(--dtna-button-accent)}.dtna-button.dtna-variant-dashed{border-style:dashed}
.dtna-button.dtna-variant-text{--dtna-button-color-hover:var(--dtna-button-color);--dtna-button-color-active:var(--dtna-button-color);--dtna-button-bg-hover:rgba(0,0,0,0.04);--dtna-button-bg-active:rgba(0,0,0,0.15)}
.dtna-button.dtna-danger.dtna-variant-text{--dtna-button-bg-hover:var(--dtna-error-background);--dtna-button-bg-active:var(--dtna-error-border)}
.dtna-button.dtna-block{width:100%}.dtna-button.dtna-shape-round{border-radius:var(--dtna-control-height)}.dtna-button.dtna-shape-circle{width:var(--dtna-control-height);padding:0;border-radius:50%}
.dtna-button.dtna-button-ghost{--dtna-button-bg:transparent;--dtna-button-bg-hover:transparent;--dtna-button-bg-active:transparent;--dtna-button-color:var(--dtna-background);--dtna-button-border:var(--dtna-background);--dtna-button-shadow:none}
.dtna-button.dtna-button-ghost.dtna-variant-primary,.dtna-button.dtna-button-ghost.dtna-danger{--dtna-button-color:var(--dtna-button-accent);--dtna-button-border:var(--dtna-button-accent);--dtna-button-color-hover:var(--dtna-button-accent-hover);--dtna-button-color-active:var(--dtna-button-accent-active);--dtna-button-border-hover:var(--dtna-button-accent-hover);--dtna-button-border-active:var(--dtna-button-accent-active)}
.dtna-button:disabled,.dtna-button.dtna-button-disabled{color:var(--dtna-disabled-text);background:var(--dtna-button-bg-disabled,var(--dtna-disabled-background));border-color:var(--dtna-button-border-disabled,var(--dtna-border));cursor:not-allowed;box-shadow:none}
.dtna-button:disabled>*{pointer-events:none}
"
