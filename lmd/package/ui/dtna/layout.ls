import c: lambda.ui.core.component
import responsive: lambda.ui.core.responsive

// one breakpoint table drives validation and CSS; rules preserve the source nodes (S12.1.3).
pub let breakpoints = responsive.breakpoints
let column_fields = ["span","offset","order","push","pull","flex"]
let align_values = {top:"flex-start",middle:"center",bottom:"flex-end",stretch:"stretch"}
let justify_values = {start:"flex-start",end:"flex-end",center:"center",["space-around"]:"space-around",["space-between"]:"space-between",["space-evenly"]:"space-evenly"}
fn valid_align(value) => (value is string or value is symbol) and c.has(align_values,c.text(value))
fn valid_justify(value) => (value is string or value is symbol) and c.has(justify_values,c.text(value))
fn flex_parts(value) => [for (part in split(trim(value)," ") where part != "") part]
fn flex_factor(value) => c.css_number(value)
fn valid_flex(value) => if (value is number) c.finite(value) and value >= 0 else if (not (value is string)) false
    else if (contains(["auto","none","initial"],trim(value)) or c.css_length(value)) true
    else (let parts = flex_parts(value),
        len(parts) >= 1 and len(parts) <= 3 and flex_factor(parts[0]) and
        (len(parts) < 2 or flex_factor(parts[1])) and
        (len(parts) < 3 or parts[2] == "auto" or c.css_length(parts[2])))
fn flex_value(value) => if (value is number) string(value) ++ " " ++ string(value) ++ " auto"
    else if (trim(value) == "auto") "1 1 auto"
    else if (c.css_length(value)) "0 0 " ++ c.length(value) else trim(value)
fn valid_column(field, value) => if (field == "flex") valid_flex(value)
    else value is int and (field == "order" or (value >= 0 and value <= 24))
fn column_values(value) => if (value is int) {span:value} else value
fn validate_column(values) bool^ {
    let names = c.properties(values,column_fields,"col")^;
    if (not all([for (key,value in values) valid_column(string(key),value)]))
        raise c.fail("col","span/offset/push/pull must be ints from 0 to 24; order must be int; flex must be a nonnegative factor or CSS flex value") else true
}
fn valid_gap(value) => c.css_length(value)
fn validate_gap(value) bool^ => responsive.validate(value,valid_gap,"row.gutter")^
fn gaps(props) => if (props.gutter is array) props.gutter else [c.option(props,"gutter",0),0]
pub fn row(props, child) element^ {
    let gap = gaps(props);
    if (len(gap) != 2) raise c.fail("row","gutter arrays need horizontal and vertical entries")
    else (
        let x = validate_gap(gap[0])^,
        let y = validate_gap(gap[1])^,
        let align = responsive.validate(c.option(props,"align","top"),valid_align,"row.align")^,
        let justify = responsive.validate(c.option(props,"justify","start"),valid_justify,"row.justify")^,
        c.node('row',props,child,["gutter","align","justify","wrap"])^
    )
}
pub fn col(props, child) element^ {
    let base = validate_column(map([for (key,value in props where contains(column_fields,string(key))) (key,value)]))^
    let sizes = [for (bp in breakpoints where c.has(props,bp.key)) validate_column(column_values(props[bp.key]))^];
    c.node('col',props,child,[*column_fields,*[for (bp in breakpoints) bp.key]])^
}
fn percent(value) => string(value * 100.0 / 24) ++ "%"
fn column_property(field, value) => if (field == "flex") flex_value(value) else if (field == "order") string(value)
    else if ((field == "push" or field == "pull") and value == 0) "auto" else percent(value)
fn column_variables(prefix, values) => join([for (field,value in values)
    "--dtna-col-" ++ prefix ++ "-" ++ string(field) ++ ":" ++ column_property(string(field),value) ++ ";" ++
    (if (string(field) == "span") "--dtna-col-" ++ prefix ++ "-display:" ++ (if (value == 0) "none" else "block") ++ ";" else "")],"")
fn column(node) {
    let base = {span:c.option(c.props(node),"span",24),offset:c.option(c.props(node),"offset",0),order:c.option(c.props(node),"order",0),
        push:c.option(c.props(node),"push",0),pull:c.option(c.props(node),"pull",0)}
    let sizes = [for (bp in breakpoints where c.has(c.props(node),bp.key)) {key:bp.key,values:column_values(c.props(node)[bp.key])}];
    <div *:c.styled(node,join([for (size in sizes) join([for (field,value in size.values) "dtna-col-" ++ size.key ++ "-" ++ string(field)]," ")]," ")),
        style:c.style_with(c.props(node),column_variables("base",base) ++ join([for (size in sizes) column_variables(size.key,size.values)],"") ++
            // the reference's base flex is inline and therefore takes precedence over responsive flex classes.
            (if (c.has(c.props(node),"flex")) "flex:" ++ flex_value(c.props(node).flex) ++ ";" else "")), *c.contents(node)>
}
fn gap_variables(axis, value) => if (value is map) join([for (key,gap in value)
    "--dtna-row-" ++ string(key) ++ "-" ++ axis ++ ":" ++ c.length(gap) ++ ";"],"")
    else "--dtna-row-base-" ++ axis ++ ":" ++ c.length(value) ++ ";"
fn gap_classes(axis, value) => if (value is map) [for (key,gap in value) "dtna-row-" ++ string(key) ++ "-" ++ axis] else []
fn row_view(node) {
    let gap = gaps(c.props(node))
    let align = c.option(c.props(node),"align","top")
    let justify = c.option(c.props(node),"justify","start")
    let classes = [*gap_classes("x",gap[0]),*gap_classes("y",gap[1]),
        responsive.classes("dtna-row-align",align),responsive.classes("dtna-row-justify",justify)];
    <div *:c.styled(node,join(classes," ")), style:c.style_with(c.props(node),
        "--dtna-row-base-x:0px;--dtna-row-base-y:0px;--dtna-row-align-base:flex-start;--dtna-row-justify-base:flex-start;" ++
        gap_variables("x",gap[0]) ++ gap_variables("y",gap[1]) ++
        responsive.variables("dtna-row-align",align,(value) => align_values[c.text(value)]) ++
        responsive.variables("dtna-row-justify",justify,(value) => justify_values[c.text(value)]) ++
        "flex-wrap:" ++ (if (c.props(node).wrap == false) "nowrap" else "wrap") ++ ";"), *c.contents(node)>
}
fn breakpoint_rules(key) =>
    ".dtna-row-" ++ key ++ "-x{--dtna-gutter:var(--dtna-row-" ++ key ++ "-x)}" ++
    ".dtna-row-" ++ key ++ "-y{row-gap:var(--dtna-row-" ++ key ++ "-y)}" ++
    ".dtna-col-" ++ key ++ "-span{flex:0 0 var(--dtna-col-" ++ key ++ "-span);max-width:var(--dtna-col-" ++ key ++ "-span);display:var(--dtna-col-" ++ key ++ "-display)}" ++
    ".dtna-col-" ++ key ++ "-offset{margin-inline-start:var(--dtna-col-" ++ key ++ "-offset)}" ++
    ".dtna-col-" ++ key ++ "-order{order:var(--dtna-col-" ++ key ++ "-order)}" ++
    ".dtna-col-" ++ key ++ "-push{inset-inline-start:var(--dtna-col-" ++ key ++ "-push)}" ++
    ".dtna-col-" ++ key ++ "-pull{inset-inline-end:var(--dtna-col-" ++ key ++ "-pull)}" ++
    ".dtna-col-" ++ key ++ "-flex{flex:var(--dtna-col-" ++ key ++ "-flex)}"
pub let css = join([for (bp in breakpoints)
    if (bp.width == 0) breakpoint_rules(bp.key) else "@media(min-width:" ++ c.px(bp.width) ++ "){" ++ breakpoint_rules(bp.key) ++ "}"],"") ++
    responsive.rules("dtna-row-align","align-items") ++ responsive.rules("dtna-row-justify","justify-content")

pub fn spacing(kind, props, child) element^ {
    let gap = c.option(props,"gap",null);
    if (gap != null and not (valid_gap(gap) or (gap is array and len(gap) == 2 and all([for (part in gap) valid_gap(part)]))))
        raise c.fail(kind,"gap must be a nonnegative number/CSS length or [horizontal,vertical]")
    else if (not c.enum_valid(props.direction,["horizontal","vertical"])^ or
        not c.enum_valid(props.align,["start","end","flex-start","flex-end","center","baseline","stretch"])^ or
        not c.enum_valid(props.justify,["start","end","flex-start","flex-end","center","space-around","space-between","space-evenly"])^)
        raise c.fail(kind,"invalid direction or alignment")
    else if (kind == 'flex' and c.has(props,"flex") and not valid_flex(props.flex)) raise c.fail(kind,"invalid flex value")
    // preserve slot boundaries before element content merges adjacent text (S2.6.4).
    else c.node(kind,props,if (kind == 'space') [for (slot in c.children(child) where slot != null) <dtna_space_slot value:slot>] else child,
        ["direction","gap","align","justify","wrap",*(if (kind == 'space') ["separator"] else ["flex"])])^
}
pub fn compact(kind, props, child) element^ {
    if (not c.enum_valid(props.direction,["horizontal","vertical"])^) raise c.fail(kind,"invalid direction")
    else c.node(kind,props,child,["direction","block","size","disabled"])^
}
fn spacing_style(props) {
    let gap = props.gap
    let gap_css = if (gap == null) "var(--dtna-spacing)" else if (gap is array) c.length(gap[1]) ++ " " ++ c.length(gap[0]) else c.length(gap);
    "display:flex;flex-direction:" ++ (if (c.text(props.direction) == "vertical") "column" else "row") ++ ";gap:" ++ gap_css ++
        ";align-items:" ++ c.text(c.option(props,"align","center")) ++ ";justify-content:" ++ c.text(c.option(props,"justify","flex-start")) ++
        ";flex-wrap:" ++ (if (props.wrap) "wrap" else "nowrap") ++ ";" ++
        (if (c.has(props,"flex")) "flex:" ++ flex_value(props.flex) ++ ";" else "")
}
fn space(node) {
    let children = [for (child in content(node) where child != null) child];
    <div *:c.styled(node),style:c.style_with(c.props(node),spacing_style(c.props(node))),
        *[for (index,child in children) (
            if (index > 0 and c.props(node).separator != null) <span class:"dtna-space-separator",["aria-hidden"]:"true",c.render(c.props(node).separator)> else null,
            <div class:"dtna-space-item",c.render(if (child is element and name(child) == 'dtna_space_slot') child.value else child)>)]>
}
pub fn divider(props, child) element^ {
    let flags = c.boolean_props(props,["dashed","plain"],"divider")^;
    if (not c.enum_valid(props.direction,["horizontal","vertical"])^ or not c.enum_valid(props.placement,["start","center","end"])^)
        raise c.fail("divider","invalid direction or placement")
    else if (c.text(props.direction) == "vertical" and child != null) raise c.fail("divider","vertical dividers cannot have labels")
    else c.node('divider',props,child,["direction","placement","dashed","plain"])^
}
fn divider_view(node) {
    let labelled = len(content(node)) > 0;
    <div *:c.styled(node,"dtna-divider-" ++ c.text(c.option(c.props(node),"placement","center")) ++
            (if (labelled) " dtna-divider-labelled" else "") ++ (if (c.props(node).dashed) " dtna-divider-dashed" else "") ++ (if (c.props(node).plain) " dtna-divider-plain" else "")),
        role:"separator",["aria-orientation"]:c.text(c.option(c.props(node),"direction","horizontal")),
        *[if (labelled) (<span class:"dtna-divider-line">,<span class:"dtna-divider-label",*c.contents(node)>,<span class:"dtna-divider-line">) else null]>
}
view dtna_layout_parts: <dtna.row> | <dtna.col> | <dtna.space> | <dtna.flex> | <dtna.space_compact> | <dtna.button_group> | <dtna.divider> {
    if (c.kind(~) == 'row') row_view(~)
    else if (c.kind(~) == 'col') column(~)
    else if (c.kind(~) == 'space') space(~)
    else if (c.kind(~) == 'flex') <div *:c.styled(~),style:c.style_with(c.props(~),spacing_style(c.props(~))),*c.contents(~)>
    else if (c.kind(~) == 'divider') divider_view(~)
    else <fieldset *:c.styled(~,"dtna-compact" ++ (if (c.text(c.props(~).direction) == "vertical") " dtna-compact-vertical" else "") ++
            (if (c.props(~).block) " dtna-compact-block" else "")),role:"group",["aria-label"]:c.props(~).label,
            *:c.boolean_attr("disabled",c.props(~).disabled),*c.contents(~)>
}
