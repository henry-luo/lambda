import c: lambda.ui.core.component
import icons: .icons

fn validate_part(props, owner, allowed = ["size","shape","width","height"], width_array = false) bool^ {
    let names = c.properties(props,allowed,owner)^;
    if (props.size != null and not (c.enum_valid(props.size,["small","middle","large"])^ or (c.finite(props.size) and props.size > 0)))
        raise c.fail(owner,"size must be small/middle/large or a positive number")
    else if (not c.enum_valid(props.shape,["circle","square","round","default"])^) raise c.fail(owner,"invalid shape")
    else if (props.rows != null and (not (props.rows is int) or props.rows < 0 or props.rows > 1000)) raise c.fail(owner,"rows must be an int from 0 to 1000")
    else if (not all([for (field in ["width","height"] where props[field] != null)
        if (width_array and props[field] is array and field == "width") all([for (part in props[field]) c.css_length(part)]) else c.css_length(props[field])]))
        raise c.fail(owner,"dimensions must be nonnegative numbers or CSS lengths") else true
}
pub fn descriptor(props, child) element^ {
    let flags = c.boolean_props(props,["active","loading","round"],"skeleton")^
    let parts = [for (field in ["avatar","title","paragraph"] where c.has(props,field))
        if (props[field] is map) validate_part(props[field],"skeleton." ++ field,
            if (field == "avatar") ["size","shape"] else if (field == "title") ["width"] else ["width","rows"],field == "paragraph")^
        else if (props[field] is bool) true else raise c.fail("skeleton",field ++ " must be bool or an options map")];
    if (props.rows != null and (not (props.rows is int) or props.rows < 0 or props.rows > 1000)) raise c.fail("skeleton","rows must be an int from 0 to 1000")
    else c.node('skeleton',props,child,["active","loading","round","avatar","paragraph","rows"])^
}
pub fn part(kind, props, child = null) element^ {
    let flags = c.boolean_props(props,["active","block"],kind)^
    let dimensions = validate_part(map([for (key,value in props where contains(["shape","width","height"],string(key))) (key,value)]),kind)^;
    if (props.dimension != null and (not c.finite(props.dimension) or props.dimension <= 0)) raise c.fail(kind,"dimension must be positive")
    else c.node(kind,props,child,["size","shape","width","height","dimension","active","block"])^
}
fn part_options(value) => if (value is map) value else {}
fn scale(props, fallback) => c.option(props,"dimension",if (props.size is number) props.size else
    if (c.text(props.size) == "large") 40 else if (c.text(props.size) == "small") 24 else fallback)
fn shape_style(props, circular = false) => "border-radius:" ++
    (if (c.text(props.shape) == "circle" or (circular and props.shape == null)) "50%" else if (c.text(props.shape) == "round") "999px" else "4px") ++ ";"
fn block(kind, props, child = null) {
    let side = scale(props,32)
    let avatar = kind == "avatar"
    let image = kind == "image"
    let width = c.option(props,"width",if (avatar) side else if (image) 96 else if (kind == "input") 160 else if (kind == "node") 100 else 64)
    let height = c.option(props,"height",if (image) 96 else if (kind == "node") 100 else side);
    <span class:"dtna-skeleton-shape dtna-skeleton-" ++ kind,["aria-hidden"]:"true",
        style:"width:" ++ (if (props.block) "100%" else c.length(width)) ++ ";height:" ++ c.length(height) ++ ";" ++ shape_style(props,avatar),
        *[if (image) icons.render({name:"picture",width:40,height:40})^ else null,*c.children(c.render(child))]>
}
fn paragraph_width(props, index, rows) => if (props.width is array) (if (props.width[index] == null) "100%" else props.width[index])
    else if (props.width != null and index == rows-1) props.width else if (index == rows-1 and rows > 1) "61%" else "100%"
view dtna_skeleton: <dtna kind:'skeleton'> {
    let p = ~.props
    let paragraph = part_options(p.paragraph)
    let title = part_options(p.title)
    let rows = c.option(paragraph,"rows",c.option(p,"rows",3));
    if (p.loading == false) <div *:c.styled(~),*c.contents(~)> else
    <div *:c.styled(~,if (p.active) "dtna-skeleton-active" else ""),["aria-busy"]:"true",["aria-label"]:c.option(p,"label","Loading"),
        *[if (p.avatar != null and p.avatar != false) <div class:"dtna-skeleton-header",block("avatar",part_options(p.avatar))> else null,
        <div class:"dtna-skeleton-section",*[
            if (p.title != false) <div class:"dtna-skeleton-shape dtna-skeleton-title",["aria-hidden"]:"true",
                style:"width:" ++ c.length(c.option(title,"width","38%")) ++ ";" ++ (if (p.round) "border-radius:999px;" else "")> else null,
            if (p.paragraph != false) <div class:"dtna-skeleton-paragraph",["aria-hidden"]:"true",*[for (index in 0 to (rows-1))
                <div class:"dtna-skeleton-shape dtna-skeleton-line",style:"width:" ++ c.length(paragraph_width(paragraph,index,rows)) ++ ";" ++
                    (if (p.round) "border-radius:999px;" else "")>]> else null]>]>
}
view dtna_skeleton_part: <dtna kind:'skeleton-avatar' | 'skeleton-button' | 'skeleton-input' | 'skeleton-image' | 'skeleton-node'> {
    <span *:c.styled(~,if (~.props.active) "dtna-skeleton-active" else ""),["aria-busy"]:"true",["aria-label"]:c.option(~.props,"label","Loading"),
        block(slice(c.text(~.kind),9),~.props,c.contents(~))>
}
pub let css = "
.dtna-skeleton{display:flex;gap:16px;width:100%}.dtna-skeleton-section{flex:1;min-width:0}.dtna-skeleton-header{flex-shrink:0}
.dtna-skeleton-shape{display:inline-flex;align-items:center;justify-content:center;background:var(--dtna-disabled-background);border-radius:4px;color:#bfbfbf;vertical-align:top}
.dtna-skeleton-title{height:16px;margin:0 0 24px}.dtna-skeleton-paragraph .dtna-skeleton-line{display:block;height:16px;margin:0 0 16px}
.dtna-skeleton-paragraph .dtna-skeleton-line:last-child{margin-bottom:0}
.dtna-skeleton-active .dtna-skeleton-shape{animation:dtna-skeleton-pulse 1.4s ease-in-out infinite}
@keyframes dtna-skeleton-pulse{0%,100%{opacity:1}50%{opacity:0.5}}
@media(prefers-reduced-motion:reduce){.dtna-skeleton-active .dtna-skeleton-shape{animation:none}}
"
