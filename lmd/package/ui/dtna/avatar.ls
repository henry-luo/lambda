import c: lambda.ui.core.component
import responsive: lambda.ui.core.responsive

let sizes = {small:24,middle:32,large:40}
fn positive(value) => c.finite(value) and value > 0
pub fn descriptor(props, child) element^ {
    let flags = c.boolean_props(props,["draggable"],"avatar")^
    let size = c.option(props,"size","middle")
    let valid_size = if (size is map) responsive.validate(size,positive,"avatar.size")^
        else if (positive(size) or (size != null and c.enum_valid(size,["small","middle","large"])^)) true
        else raise c.fail("avatar","size must be small/middle/large, a positive number or a breakpoint map");
    if (props.dimension != null and not positive(props.dimension)) raise c.fail("avatar","dimension must be positive")
    else if (not c.enum_valid(props.shape,["circle","square"])^) raise c.fail("avatar","shape must be circle or square")
    else if (props.src != null and not (props.src is string or props.src is element)) raise c.fail("avatar","src must be a URL or element")
    else if (not all([for (field in ["alt","srcset"] where props[field] != null) props[field] is string])) raise c.fail("avatar","alt and srcset must be strings")
    else c.node('avatar',props,child,["size","shape","dimension","src","srcset","icon","alt","draggable"])^
}
fn dimensions(props) {
    let size = if (props.dimension != null) props.dimension else c.option(props,"size","middle")
    let side = if (size is number) size else if (size is map) 32 else sizes[c.text(size)]
    // the reference uses the current breakpoint's entry, reverting to its base size for a missing entry.
    let values = if (size is map) map([for (bp in responsive.breakpoints) (bp.key,c.option(size,bp.key,32))]) else null;
    {side:side,values:values,fonts:if (size is map) map([for (bp in responsive.breakpoints)
        (bp.key,if (c.has(size,bp.key)) c.px(size[bp.key]/2) else "var(--dtna-font-size)")]) else null,
        number:size is number,responsive:size is map}
}
fn metrics(side) => c.px(side)
view dtna_avatar: <dtna kind:'avatar'> {
    let p = ~.props
    let size = dimensions(p)
    let image = p.src != null
    let icon = not image and p.icon != null
    let fontsize = if (size.number) (if (icon) size.side/2 else 18) else null;
    <span *:c.styled(~,"dtna-avatar-" ++ c.text(c.option(p,"shape","circle")) ++
            (if (image) " dtna-avatar-image" else if (icon) " dtna-avatar-icon" else "") ++
            (if (size.responsive) " " ++ responsive.classes("dtna-avatar-size",size.values) else "")),
        ["aria-label"]:p.label,
        style:c.style_with(p,"--dtna-avatar-size-base:" ++ c.px(size.side) ++ ";" ++
            (if (size.responsive) responsive.variables("dtna-avatar-size",size.values,metrics) ++
                responsive.variables("dtna-avatar-font",size.fonts,(value) => value) else "") ++
            (if (fontsize == null) "" else "font-size:" ++ c.px(fontsize) ++ ";")),
        *[if (p.src is string) <img src:p.src,srcset:p.srcset,alt:c.text(c.option(p,"alt",p.label)),
            draggable:if (p.draggable == null) null else c.aria(p.draggable)>
        else if (p.src is element) c.render(p.src)
        else if (icon) c.render(p.icon) else <span class:"dtna-avatar-string",*c.contents(~)>]>
}
fn responsive_rule(bp) {
    let side = "var(--dtna-avatar-size-" ++ bp.key ++ ")"
    let rule = ".dtna-avatar-size-" ++ bp.key ++ "{width:" ++ side ++ ";height:" ++ side ++ ";font-size:var(--dtna-avatar-font-" ++ bp.key ++ ")}";
    if (bp.width == 0) rule else "@media(min-width:" ++ c.px(bp.width) ++ "){" ++ rule ++ "}"
}
pub let css = "
.dtna-avatar{position:relative;display:inline-flex;align-items:center;justify-content:center;width:var(--dtna-avatar-size-base,32px);height:var(--dtna-avatar-size-base,32px);border:1px solid transparent;border-radius:50%;overflow:hidden;vertical-align:middle;white-space:nowrap;background:var(--dtna-disabled-text);color:#fff;font-size:var(--dtna-font-size);flex-shrink:0}
.dtna-avatar-square{border-radius:var(--dtna-radius)}.dtna-avatar.dtna-size-small.dtna-avatar-square{border-radius:max(0px,calc(var(--dtna-radius) - 2px))}.dtna-avatar.dtna-size-large.dtna-avatar-square{border-radius:calc(var(--dtna-radius) + 2px)}
.dtna-avatar-image{background:transparent}.dtna-avatar>img{display:block;width:100%;height:100%;object-fit:cover}
.dtna-avatar-icon{font-size:18px}.dtna-avatar.dtna-size-small.dtna-avatar-icon{font-size:14px}.dtna-avatar.dtna-size-large.dtna-avatar-icon{font-size:var(--dtna-font-size-heading-3)}
.dtna-avatar-icon>.dtna-icon{width:1em;height:1em}.dtna-avatar-string{display:inline-block}
" ++ join([for (bp in responsive.breakpoints) responsive_rule(bp)],"")
