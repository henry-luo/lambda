import c: lambda.ui.core.component
import responsive: lambda.ui.core.responsive

fn positive_int(value) => value is int and value > 0
let default_columns = {xs:1,sm:2,md:3,xxxl:4}
pub fn badge(props, child) element^ {
    let color = c.validate_colors(props,["color"],"badge")^
    let flags = c.boolean_props(props,["show_zero"],"badge")^;
    if (props.offset != null and (not (props.offset is array) or len(props.offset) != 2 or not all([for (part in props.offset) c.finite(part)])))
        raise c.fail("badge","offset must be [horizontal,vertical]")
    else if (props.overflow_count != null and (not c.finite(props.overflow_count) or props.overflow_count < 0))
        raise c.fail("badge","overflow_count must be nonnegative")
    else if (props.count is number and (not c.finite(props.count) or props.count < 0))
        raise c.fail("badge","count must be finite and nonnegative")
    else c.node('badge',props,child,["count","overflow_count","dot","show_zero","offset","color","status","text","size"])^
}
pub fn ribbon(props, child) element^ {
    let color = c.validate_colors(props,["color"],"badge-ribbon")^;
    if (not c.enum_valid(props.placement,["start","end"])^) raise c.fail("badge-ribbon","invalid placement")
    else c.node('badge-ribbon',props,child,["text","color","placement"])^
}
fn badge_view(node) {
    let p = c.props(node)
    let status = p.status != null
    let dot = p.dot == true or status
    let count = c.option(p,"count",null)
    let visible = dot or (count != null and (count != 0 or p.show_zero == true))
    let overflow = c.option(p,"overflow_count",99)
    let label = if (p.label != null) p.label else if (dot) null else c.text(count)
    let attached = len(content(node)) > 0 and not status
    let offset = c.option(p,"offset",[0,0]);
    <span *:c.styled(node,(if (attached) "dtna-badge-attached" else "dtna-badge-standalone") ++
        (if (dot) " dtna-badge-dot" else "") ++ (if (status) " dtna-badge-status" else "")),
        *[ *c.contents(node), if (visible) <sup class:"dtna-badge-count",role:if (label != null) "status" else null,
            ["aria-label"]:label, title:if (c.has(p,"title")) p.title else label,
            style:(if (p.color == null) "" else "background:" ++ p.color ++ ";") ++
                "--dtna-badge-x:" ++ c.px(offset[0]) ++ ";--dtna-badge-y:" ++ c.px(offset[1]) ++ ";",
            if (dot) "" else if (count is number and count > overflow) string(overflow) ++ "+" else c.render(count)> else null,
            if (p.text != null) <span class:"dtna-badge-text",c.render(p.text)> else null]>
}

pub fn descriptions(props) element^ {
    let column = responsive.validate(c.option(props,"column",default_columns),positive_int,"descriptions.column")^
    let flags = c.boolean_props(props,["bordered","colon"],"descriptions")^;
    if (not (props.items is array)) raise c.fail("descriptions","items must be an array")
    else if (not c.enum_valid(props.layout,["horizontal","vertical"])^) raise c.fail("descriptions","invalid layout")
    else (
        let items = [for (item in props.items) validate_description_item(item)^],
        c.node('descriptions',props,null,["items","column","layout","bordered","colon","extra","size"])^
    )
}
fn validate_description_item(item) bool^ {
    let names = c.properties(item,["key","label","children","span","label_style","content_style"],"description-item")^;
    responsive.validate(c.option(item,"span",1),(span) => positive_int(span) or span == 'filled' or span == "filled","description-item.span")^
}
pub fn description_item(props, child) map^ {
    let names = c.properties(props,["key","label","span","label_style","content_style"],"description-item")^;
    let result = {*:props,children:child}
    let valid = validate_description_item(result)^;
    result
}
fn description_span(value) => "span " ++ string(value)
fn columns(value) => "repeat(" ++ string(value) ++ ",minmax(0,1fr))"
// clamp overflowing cells and fill the final cell of each row, including responsive rows.
fn description_spans(items, count, key, used = 0) {
    if (len(items) == 0) [] else (
        let requested = responsive.at(c.option(items[0],"span",1),key,1),
        let remaining = count - used,
        let span = if (c.text(requested) == "filled" or len(items) == 1) remaining else min(requested,remaining),
        [span,*description_spans(slice(items,1,len(items)),count,key,if (used+span == count) 0 else used+span)])
}
fn descriptions_view(node) {
    let p = c.props(node)
    let column = map([for (bp in responsive.breakpoints) (bp.key,
        responsive.at(c.option(p,"column",default_columns),bp.key,responsive.at(default_columns,bp.key,3)))])
    let packed = [for (bp in responsive.breakpoints) {key:bp.key,spans:description_spans(p.items,responsive.at(column,bp.key,3),bp.key)}];
    <section *:c.styled(node,(if (p.bordered) "dtna-descriptions-bordered " else "") ++
        (if (c.text(p.layout) == "vertical") "dtna-descriptions-vertical" else "")),
        *[if (p.title != null or p.extra != null) <div class:"dtna-descriptions-header",
            <div class:"dtna-descriptions-title",c.render(p.title)><div c.render(p.extra)>> else null,
        <dl class:"dtna-descriptions-grid " ++ responsive.classes("dtna-description-columns",column),
            style:"--dtna-description-columns-base:repeat(3,minmax(0,1fr));" ++ responsive.variables("dtna-description-columns",column,columns),
            *[for (index,item in p.items) (
                let spans = map([for (bp in packed) (bp.key,bp.spans[index])]),
                <div class:"dtna-description " ++ responsive.classes("dtna-description-span",spans),
                style:"--dtna-description-span-base:span 1;" ++ responsive.variables("dtna-description-span",spans,description_span),
                <dt style:item.label_style,*[c.render(item.label),if (p.colon != false and c.text(p.layout) != "vertical") ":" else null]>
                <dd style:item.content_style,c.render(item.children)>>)]>]>
}
view dtna_display: <dtna.badge> | <dtna.badge_ribbon> | <dtna.descriptions> {
    if (c.kind(~) == 'badge') badge_view(~)
    else if (c.kind(~) == 'descriptions') descriptions_view(~)
    else <div *:c.styled(~,"dtna-ribbon-" ++ c.text(c.option(c.props(~),"placement","end"))),
        *[*c.contents(~),<div class:"dtna-ribbon",style:if (c.props(~).color == null) null else "background:" ++ c.props(~).color ++ ";",c.render(c.props(~).text)>]>
}
pub let css = "
.dtna-badge-standalone .dtna-badge-count{position:relative;display:inline-block;top:auto;right:auto}
.dtna-badge-attached .dtna-badge-count{right:calc(-10px + var(--dtna-badge-x));top:calc(-10px + var(--dtna-badge-y))}
.dtna-badge-dot .dtna-badge-count{width:6px;min-width:6px;height:6px;padding:0;border-radius:50%}
.dtna-badge-status{display:inline-flex;align-items:center;gap:8px}
.dtna-badge-status.dtna-status-success .dtna-badge-count{background:var(--dtna-success)}
.dtna-badge-status.dtna-status-warning .dtna-badge-count{background:var(--dtna-warning)}
.dtna-badge-status.dtna-status-default .dtna-badge-count{background:#d9d9d9}
.dtna-badge-status.dtna-status-processing .dtna-badge-count{background:var(--dtna-primary)}
.dtna-badge.dtna-size-small .dtna-badge-count{height:14px;min-width:14px;line-height:14px;padding:0 4px;font-size:10px}
.dtna-badge-ribbon{position:relative}.dtna-ribbon{position:absolute;top:16px;inset-inline-end:-8px;padding:0 8px;height:22px;line-height:22px;background:var(--dtna-primary);color:#fff;border-radius:4px 4px 0 4px}
.dtna-ribbon-start .dtna-ribbon{inset-inline-end:auto;inset-inline-start:-8px;border-radius:4px 4px 4px 0}
.dtna-descriptions-header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}.dtna-descriptions-title{font-size:16px;font-weight:600}
.dtna-descriptions-grid{display:grid;grid-template-columns:var(--dtna-description-columns-base);margin:0}
.dtna-description{grid-column:var(--dtna-description-span-base)}
.dtna-descriptions-grid .dtna-description{min-width:0;padding:0 16px 16px 0;gap:8px}
.dtna-descriptions-grid dt{min-width:0;flex-shrink:0}.dtna-descriptions-grid dd{min-width:0;overflow-wrap:break-word}
.dtna-descriptions-vertical .dtna-description{flex-direction:column}.dtna-descriptions-bordered .dtna-descriptions-grid{border-top:1px solid var(--dtna-border);border-inline-start:1px solid var(--dtna-border)}
.dtna-descriptions-bordered .dtna-description{padding:0;border-inline-end:1px solid var(--dtna-border);border-bottom:1px solid var(--dtna-border);gap:0}
.dtna-descriptions-bordered dt{background:var(--dtna-surface);padding:16px 24px}.dtna-descriptions-bordered dd{padding:16px 24px}
.dtna-descriptions.dtna-size-middle dt,.dtna-descriptions.dtna-size-middle dd{padding:12px 24px}.dtna-descriptions.dtna-size-small dt,.dtna-descriptions.dtna-size-small dd{padding:8px 16px}
" ++ responsive.rules("dtna-description-columns","grid-template-columns") ++ responsive.rules("dtna-description-span","grid-column")
