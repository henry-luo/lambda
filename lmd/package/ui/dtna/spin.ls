import dom
import c: lambda.ui.core.component
import progress: .progress

let properties = ["size","spinning","delay","description","tip","indicator","fullscreen","percent","class_names","styles"]
let semantic_parts = ["root","section","indicator","description","container"]
pub fn descriptor(props, child) element^ {
    let names = c.properties(props,[*c.common_props,*properties],'spin',true)^
    let flags = c.boolean_props(props,["spinning","fullscreen"],'spin')^
    let parts = c.validate_parts(props,semantic_parts,'spin')^
    let delay = c.option(props,"delay",0);
    if (not c.finite(delay) or delay < 0) raise c.fail('spin',"delay must be finite nonnegative milliseconds")
    else if (c.has(props,"percent") and props.percent != 'auto' and not c.finite(props.percent))
        raise c.fail('spin',"percent must be finite or auto")
    else c.node('spin',props,child,properties)^
}
fn settings(props) => {spinning:c.option(props,"spinning",true),delay:c.option(props,"delay",0),automatic:props.percent == 'auto'}
fn percentage(props, clock) => if (props.percent == 'auto') clock.progress else max(0,min(100,c.option(props,"percent",0)))
fn dots() => <span class:"dtna-spin-dot",*[for (index in 1 to 4) <i class:"dtna-spin-dot-item">]>
fn indicator(props, clock) {
    let percent = percentage(props,clock)
    let attributes = c.part_attrs(props,"indicator","dtna-spin-indicator");
    if (c.has(props,"indicator")) <span *:attributes,
        c.render(if (props.indicator is fn) props.indicator({size:c.option(props,"size",'middle'),percent:percent,spinning:clock.shown}) else props.indicator)>
    else if (percent > 0) <span *:attributes,class:attributes.class ++ " dtna-spin-progress",
        c.render(progress.descriptor({type:'circle',percent:percent,show_info:false,dimension:100,stroke_width:20,
            rail_color:"rgba(0,0,0,0.06)",stroke_color:"currentColor",status:'normal'})^)>
    else <span *:attributes,["aria-hidden"]:"true",dots()>
}
fn presentation(node, clock) {
    let p = c.props(node)
    let shown = c.option(p,"spinning",true) and clock.shown
    let nested = len(content(node)) > 0 or p.fullscreen
    let description = c.option(p,"description",p.tip)
    // Spin sizes change the indicator alone, unlike shared control-size font tokens.
    let root = c.part_attrs(p,"root",c.classes('spin',{*:p,size:null},
        (if (shown) "dtna-spin-spinning" else "") ++ (if (nested) "" else " dtna-spin-section " ++ c.text(p.class_names.section)) ++
        (if (p.size == null) "" else " dtna-spin-" ++ string(p.size)) ++ (if (p.fullscreen) " dtna-spin-fullscreen" else "")))
    let section = [indicator(p,{*:clock,shown:shown}),if (description != null) <div *:c.part_attrs(p,"description","dtna-spin-description"),c.render(description)> else null];
    <div *:c.attrs(p),*:root,style:c.text(root.style) ++ (if (nested) "" else c.text(p.styles.section)) ++ c.text(p.style),
        role:c.option(p,"role","status"),["aria-live"]:"polite",["aria-busy"]:c.aria(shown),["aria-label"]:p.label,
        *[*(if (shown) (if (nested) [<div *:c.part_attrs(p,"section","dtna-spin-section"),*section>] else section) else []),
        if (len(content(node)) > 0) <div *:c.part_attrs(p,"container","dtna-spin-container"),*c.contents(node)> else null]>
}
// timers use the existing document-owned frame queue; pure projection only reads explicit clock state.
pn advance(node, evt, previous) {
    let config = settings(c.props(node))
    let changed = config != previous.settings
    let restarted = previous.settings == null or config.spinning != previous.settings.spinning or config.delay != previous.settings.delay
    let automatic_changed = config.automatic != previous.settings.automatic
    let now = evt.time_stamp
    let start = if (restarted) now else previous.start
    let shown = config.spinning and now - start >= config.delay
    let value = if (restarted or automatic_changed) 0 else previous.progress
    let deadline = if (restarted or automatic_changed or not previous.shown) now + 200 else previous.update_at
    let stepped = if (shown and config.automatic and now >= deadline)
        value + (100-value)*(if (value <= 30) 0.05 else if (value <= 70) 0.03 else if (value <= 96) 0.01 else 0) else value
    let update_at = if (now >= deadline) now + 200 else deadline
    let waiting = config.spinning and (not shown or (config.automatic and stepped <= 96))
    if (changed and previous.token > 0) { dom.cancel_frame(evt.target,previous.token) }
    {settings:config,start:start,shown:shown,progress:stepped,update_at:update_at,
        token:if (waiting) dom.request_frame(evt.target,"dtna_spin_tick") else 0}
}
view dtna_spin: <dtna.spin> state clock:{settings:null,start:0,progress:0,update_at:0,token:0,
    shown:c.option(c.props(~),"spinning",true) and c.option(c.props(~),"delay",0) == 0} {
    presentation(~,clock)
}
on render(evt) {
    if (evt.event_phase == 2 and clock.settings != settings(c.props(~))) { clock = advance(~,evt,clock) }
    'pass'
}
on dtna_spin_tick(evt) {
    if (evt.detail == clock.token and clock.token > 0) { clock = advance(~,evt,clock) }
    'pass'
}
pub let css = "
.dtna-spin{display:block;position:relative;font-size:var(--dtna-font-size);line-height:var(--dtna-line-height)}
.dtna-spin-section{display:flex;align-items:center;flex-direction:column;gap:12px;color:var(--dtna-primary)}.dtna-spin.dtna-spin-section{display:inline-flex}
.dtna-spin>.dtna-spin-section{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);z-index:1}
.dtna-spin{--dtna-spin-dot-size:20px}.dtna-spin-small{--dtna-spin-dot-size:14px}.dtna-spin-large{--dtna-spin-dot-size:32px}
.dtna-spin-indicator{display:inline-block;width:1em;height:1em;font-size:var(--dtna-spin-dot-size);line-height:1;flex:none}
.dtna-spin-dot{position:relative;display:inline-block;width:1em;height:1em;line-height:1;transform:rotate(45deg);animation:dtna-spin-rotate 1.2s linear infinite}
.dtna-spin-dot-item{position:absolute;display:block;width:calc((1em - 2px) / 2);height:calc((1em - 2px) / 2);border-radius:100%;background:currentColor;transform:scale(0.75);opacity:0.3;animation:dtna-spin-pulse 1s linear infinite alternate}
.dtna-spin-dot-item:nth-child(1){top:0;inset-inline-start:0}.dtna-spin-dot-item:nth-child(2){top:0;inset-inline-end:0;animation-delay:0.4s}.dtna-spin-dot-item:nth-child(3){bottom:0;inset-inline-end:0;animation-delay:0.8s}.dtna-spin-dot-item:nth-child(4){bottom:0;inset-inline-start:0;animation-delay:1.2s}
.dtna-spin-description{font-size:var(--dtna-font-size);line-height:1}.dtna-spin-container{position:relative;transition:opacity 0.3s}
.dtna-spin-container:after{position:absolute;inset:0;z-index:10;width:100%;height:100%;background:var(--dtna-background);opacity:0;content:'';pointer-events:none;transition:opacity 0.3s}
.dtna-spin-spinning>.dtna-spin-container{opacity:0.5;user-select:none;pointer-events:none}
.dtna-spin-spinning>.dtna-spin-container:after{opacity:0.4;pointer-events:auto}.dtna-spin-spinning .dtna-spin-description{text-shadow:0 0 5px var(--dtna-background)}
.dtna-spin-fullscreen{position:fixed;inset:0;background:rgba(0,0,0,0.45);z-index:1000;opacity:0;pointer-events:none}.dtna-spin-fullscreen.dtna-spin-spinning{opacity:1;pointer-events:auto}.dtna-spin-fullscreen>.dtna-spin-section{color:white}
.dtna-spin-progress .dtna-progress,.dtna-spin-progress .dtna-progress-ring{width:100%;height:100%}
@keyframes dtna-spin-rotate{to{transform:rotate(405deg)}}@keyframes dtna-spin-pulse{to{opacity:1}}
@media(prefers-reduced-motion:reduce){.dtna-spin-dot,.dtna-spin-dot-item{animation:none}.dtna-spin-container{transition:none}}
"
