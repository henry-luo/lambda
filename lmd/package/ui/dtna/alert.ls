import c: lambda.ui.core.component
import interaction: lambda.ui.core.interaction
import icons: .icons

let properties = ["message","description","status","banner","show_icon","icon","action","closable",
    "close_icon","close_label","visible","default_visible","variant","class_names","styles"]
let semantic_parts = ["root","icon","section","title","description","actions","close"]
pub fn descriptor(props, child) element^ {
    let names = c.properties(props,[*c.common_props,*properties],'alert',true)^
    let flags = c.boolean_props(props,["banner","show_icon","visible","default_visible"],'alert')^
    let parts = c.validate_parts(props,semantic_parts,'alert')^;
    if (c.has(props,"visible") and c.has(props,"default_visible")) raise c.fail('alert',"visible and default_visible are mutually exclusive")
    else if ((c.has(props,"status") and props.status == null) or (c.has(props,"variant") and props.variant == null))
        raise c.fail('alert',"status and variant must name a supported choice")
    else if (props.close_label != null and not (props.close_label is string)) raise c.fail('alert',"close_label must be a string")
    else c.node('alert',props,child,properties)^
}
fn closable(props) => c.option(props,"closable",props.close_icon != null and props.close_icon != false)
fn status(props) string^ => c.text(c.option(props,"status",if (props.banner) 'warning' else 'info'))^
fn default_icon(props) => icons.descriptor({name:{success:"check-circle",info:"info-circle",warning:"exclamation-circle",error:"close-circle"}[status(props)^],theme:'filled'})^
fn presentation(node) {
    let p = node.props
    let description = p.description != null
    let title = c.option(p,"title",p.message)
    let root = c.part_attrs(p,"root",c.classes('alert',p,"dtna-status-" ++ status(p)^ ++
        " dtna-alert-" ++ c.text(c.option(p,"variant",'outlined')) ++
        (if (description) " dtna-alert-with-description" else "") ++ (if (p.banner) " dtna-alert-banner" else "")))
    let show_icon = c.option(p,"show_icon",c.option(p,"banner",false))
    let close_icon = c.option(p,"close_icon",true);
    <div *:c.attrs(p),*:root,style:c.text(root.style) ++ c.text(p.style),role:c.option(p,"role","alert"),["data-show"]:"true",*[
        if (show_icon) <span *:c.part_attrs(p,"icon","dtna-alert-icon"),["aria-hidden"]:"true",c.render(c.option(p,"icon",default_icon(p)))> else null,
        <div *:c.part_attrs(p,"section","dtna-alert-section"),*[
            if (title != null) <div *:c.part_attrs(p,"title","dtna-alert-title"),c.render(title)> else null,
            if (description) <div *:c.part_attrs(p,"description","dtna-alert-description"),c.render(p.description)> else null,*c.contents(node)]>,
        if (p.action != null) <div *:c.part_attrs(p,"actions","dtna-alert-actions"),c.render(p.action)> else null,
        if (closable(p)) <button *:c.part_attrs(p,"close","dtna-alert-close dtna-close"),type:"button",["aria-label"]:c.option(p,"close_label","Close"),
            if (close_icon == true) c.render(icons.descriptor({name:"close"})^) else if (close_icon == false) null else c.render(close_icon)> else null]>
}
view dtna_alert: <dtna kind:'alert'> state visible:c.option(~.props,"default_visible",true) {
    if (c.option(~.props,"visible",visible)) presentation(~) else <span hidden:"">
}
on click(evt) {
    let close = interaction.target(~,evt,".dtna-alert-close")
    if (closable(~.props) and close != null) {
        // retaining controlled visibility cancels a close without a synchronous callback contract (S12.1.3).
        if (not c.has(~.props,"visible")) { visible = false }
        emit("ui_action",c.action(~,'close',false))
        return 'prevent-default'
    }
    'pass'
}
pub let css = "
.dtna-alert{position:relative;display:flex;align-items:center;padding:8px 12px;font-size:var(--dtna-font-size);line-height:var(--dtna-line-height);color:var(--dtna-text);border:1px solid var(--dtna-primary-border);border-radius:calc(var(--dtna-radius) + 2px);background:var(--dtna-primary-background);overflow-wrap:break-word}
.dtna-alert.dtna-status-success{border-color:var(--dtna-success-border);background:var(--dtna-success-background)}.dtna-alert.dtna-status-warning{border-color:var(--dtna-warning-border);background:var(--dtna-warning-background)}.dtna-alert.dtna-status-error{border-color:var(--dtna-error-border);background:var(--dtna-error-background)}
.dtna-alert.dtna-alert-filled{border-color:transparent}.dtna-alert-banner{border:0;border-radius:0}
.dtna-alert-section{flex:1;min-width:0}.dtna-alert-title{color:var(--dtna-text)}
.dtna-alert-icon{display:inline-flex;margin-inline-end:8px;line-height:0;color:var(--dtna-primary)}.dtna-alert-icon .dtna-icon{width:1em;height:1em}
.dtna-status-success>.dtna-alert-icon{color:var(--dtna-success)}.dtna-status-warning>.dtna-alert-icon{color:var(--dtna-warning)}.dtna-status-error>.dtna-alert-icon{color:var(--dtna-error)}
.dtna-alert-with-description{align-items:flex-start;padding:20px 24px}.dtna-alert-with-description>.dtna-alert-icon{margin-inline-end:12px;font-size:var(--dtna-font-size-heading-3)}
.dtna-alert-with-description .dtna-alert-title{margin-bottom:8px;font-size:calc(var(--dtna-font-size) + 2px)}
.dtna-alert-description{font-size:var(--dtna-font-size);line-height:var(--dtna-line-height)}.dtna-alert-description>pre{margin:0;padding:0}
.dtna-alert-actions{margin-inline-start:8px}.dtna-alert-close{display:inline-flex;align-items:center;justify-content:center;flex:none;margin-inline-start:8px;padding:0;border:0;background:transparent;overflow:hidden;font:inherit;font-size:12px;line-height:12px;color:var(--dtna-text-secondary);cursor:pointer}
.dtna-alert-close:hover{color:var(--dtna-text)}.dtna-alert-close:focus-visible{outline:2px solid var(--dtna-primary);outline-offset:2px}
"
