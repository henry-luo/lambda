import c: lambda.ui.core.component
import icons: .icons

fn placement(value) => if (c.text(value) == "right") "end" else if (c.text(value) == "left") "start" else c.text(value)
pub fn descriptor(props) element^ {
    let flags = c.boolean_props(props,["reverse"],"timeline")^
    let items = c.option(props,"items",[]);
    if (not (items is array)) raise c.fail("timeline","items must be an array")
    else if (not c.enum_valid(props.mode,["start","end","left","right","alternate"])^) raise c.fail("timeline","invalid mode")
    else if (not c.enum_valid(props.orientation,["horizontal","vertical"])^) raise c.fail("timeline","invalid orientation")
    else (
        let valid = [for (item in items) validate_item(item)^],
        c.node('timeline',props,null,["items","mode","orientation","reverse","pending","pending_icon"])^
    )
}
fn validate_item(item) bool^ {
    let names = c.properties(item,["key","title","content","label","children","color","icon","dot","placement","position","loading","class","style"],"timeline.item")^
    let flags = c.boolean_props(item,["loading"],"timeline.item")^
    let color = c.validate_colors(item,["color"],"timeline.item")^;
    if (not c.enum_valid(item.placement,["start","end"])^ or not c.enum_valid(item.position,["start","end","left","right"])^)
        raise c.fail("timeline","invalid item placement") else true
}
fn item_color(item) {
    let color = c.option(item,"color","blue");
    if (color == "blue") "var(--dtna-primary)" else if (color == "red") "var(--dtna-error)"
    else if (color == "green") "var(--dtna-success)" else if (color == "gray") "#bfbfbf" else color
}
fn marker(item) {
    let custom = c.option(item,"icon",item.dot);
    if (custom != null) c.render(custom) else if (item.loading) icons.render({name:"loading"})^
    else <span class:"dtna-timeline-dot">
}
view dtna_timeline: <dtna kind:'timeline'> {
    let p = ~.props
    let mode = placement(c.option(p,"mode","start"))
    // resolve placement before reversing; legacy pending has no alternating placement.
    let original = [*[for (index,item in c.option(p,"items",[])) {*:item,_side:placement(c.option(item,"placement",c.option(item,"position",
        if (mode == "alternate") (if (index % 2 == 0) "start" else "end") else mode)))}],*(if (p.pending != null and p.pending != false)
        [{content:if (p.pending == true) null else p.pending,icon:p.pending_icon,loading:true,_side:"start"}] else [])]
    let items = if (p.reverse) reverse(original) else original
    let labels = any([for (item in items) c.option(item,"title",item.label) != null])
    let split = labels or mode == "alternate";
    <ol *:c.styled(~,"dtna-timeline-" ++ c.text(c.option(p,"orientation","vertical")) ++
        (if (split) " dtna-timeline-labelled" else "") ++ (if (mode == "end") " dtna-timeline-end" else "")),
        *[for (index,item in items) (
            let side = item._side,
            let title = c.option(item,"title",item.label),
            <li class:"dtna-timeline-item dtna-timeline-item-" ++ side ++ (if (item.loading) " dtna-timeline-pending" else "") ++
                (if ((if (p.reverse) item.loading else items[index+1].loading)) " dtna-timeline-rail-pending" else "") ++
                (if (item.class == null) "" else " " ++ item.class),style:c.style_with(item,"--dtna-timeline-color:" ++ item_color(item) ++ ";"),
                *[<div class:"dtna-timeline-rail",<span class:"dtna-timeline-marker",["aria-hidden"]:"true",marker(item)>>,
                if (split) <div class:"dtna-timeline-title",c.render(title)> else null,
                <div class:"dtna-timeline-content",*[if (not split and title != null) <div class:"dtna-timeline-title",c.render(title)> else null,
                    c.render(c.option(item,"content",item.children))]>]>)]>
}
pub let css = "
.dtna-timeline{list-style:none;padding:0;margin:0}.dtna-timeline-item{position:relative;display:grid;grid-template-columns:24px minmax(0,1fr);padding:0;min-height:44px}
.dtna-timeline-rail{position:relative;grid-column:1;grid-row:1;display:flex;justify-content:center;color:var(--dtna-timeline-color)}
.dtna-timeline-rail:after{content:'';position:absolute;top:20px;bottom:0;inset-inline-start:11px;border-inline-start:2px solid var(--dtna-border)}
.dtna-timeline-item:last-child .dtna-timeline-rail:after{display:none}.dtna-timeline-rail-pending .dtna-timeline-rail:after{border-inline-start-style:dotted}
.dtna-timeline-marker{display:inline-flex;align-items:center;justify-content:center;position:relative;z-index:1;width:24px;height:24px;background:var(--dtna-background)}
.dtna-timeline-dot{position:static;width:10px;height:10px;border:2px solid currentColor;border-radius:50%;background:var(--dtna-background)}
.dtna-timeline-content{grid-column:2;grid-row:1;min-width:0;padding:0 0 20px 8px}.dtna-timeline-title{font-weight:400;margin-bottom:4px}
.dtna-timeline-labelled .dtna-timeline-item{grid-template-columns:minmax(0,1fr) 24px minmax(0,1fr)}
.dtna-timeline-labelled .dtna-timeline-rail{grid-column:2}.dtna-timeline-labelled .dtna-timeline-title{grid-column:1;grid-row:1;text-align:end;padding-inline-end:8px}
.dtna-timeline-labelled .dtna-timeline-content{grid-column:3;padding-inline-start:8px}
.dtna-timeline-labelled .dtna-timeline-item-end .dtna-timeline-title{grid-column:3;text-align:start;padding-inline-start:8px;padding-inline-end:0}
.dtna-timeline-labelled .dtna-timeline-item-end .dtna-timeline-content{grid-column:1;text-align:end;padding-inline-end:8px;padding-inline-start:0}
.dtna-timeline-end:not(.dtna-timeline-labelled) .dtna-timeline-item{grid-template-columns:minmax(0,1fr) 24px}.dtna-timeline-end:not(.dtna-timeline-labelled) .dtna-timeline-rail{grid-column:2}
.dtna-timeline-end:not(.dtna-timeline-labelled) .dtna-timeline-content{grid-column:1;text-align:end;padding-inline-end:8px}
.dtna-timeline-horizontal{display:flex}.dtna-timeline-horizontal .dtna-timeline-item{flex:1;min-width:0;min-height:0;grid-template-columns:minmax(0,1fr);grid-template-rows:10px auto}
.dtna-timeline-horizontal .dtna-timeline-rail{grid-column:1;grid-row:1;justify-content:center}.dtna-timeline-horizontal .dtna-timeline-marker{height:10px}
.dtna-timeline-horizontal .dtna-timeline-rail:after{inset-inline-start:calc(50% + 10px);inset-inline-end:calc(-50% + 10px);top:4px;bottom:auto;border:0;border-top:2px solid var(--dtna-border)}
.dtna-timeline-horizontal .dtna-timeline-content{grid-column:1;grid-row:2;padding:8px 0 0;text-align:center}
.dtna-timeline-horizontal.dtna-timeline-labelled .dtna-timeline-item{grid-template-rows:auto 24px auto}
.dtna-timeline-horizontal.dtna-timeline-labelled .dtna-timeline-title{grid-column:1;grid-row:1;text-align:start;padding:0 16px 8px 0}
.dtna-timeline-horizontal.dtna-timeline-labelled .dtna-timeline-rail{grid-row:2}.dtna-timeline-horizontal.dtna-timeline-labelled .dtna-timeline-content{grid-row:3}
.dtna-timeline-horizontal.dtna-timeline-labelled .dtna-timeline-item-end .dtna-timeline-title{grid-row:3}.dtna-timeline-horizontal.dtna-timeline-labelled .dtna-timeline-item-end .dtna-timeline-content{grid-row:1}
"
