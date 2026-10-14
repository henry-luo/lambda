// Physical OOXML measurements lower to CSS pixels at 96 dpi; no document CSS is copied verbatim.
import c: .common
import theme: .theme

fn integer_value(value) => if (value == null) null else int(value) ^ { null }
fn numeric(node, fields, units) => map([for (key,attribute in fields,
    let value = integer_value(c.attr(node,attribute)) where value != null) (key,value/units)])
pub fn declarations(values) => join([for (key,value in values where value != null)
    string(key) ++ ":" ++ string(value) ++ ";"],"")
pub fn attributes(style) => if (style == "") {} else {style:style}
fn px(value) => if (value == null) null else string(value) ++ "px"
fn quoted(value) => "\"" ++ replace(replace(replace(replace(value,"\\","\\\\"),"\"","\\\""),"\n"," "),"\r"," ") ++ "\""

pub fn run_props(node, colors) {
    let font = theme.font(c.child(node,"rFonts"),colors)
    let color = theme.color(c.child(node,"color"),colors)
    let size = integer_value(c.attr(c.child(node,"sz"),"val"))
    let spacing = integer_value(c.attr(c.child(node,"spacing"),"val"));
    {*:if (font == null or font == "") {} else {font:font},
        *:if (color == null) {} else {color:color},
        *:if (size == null or size <= 0) {} else {font_size:size/1.5},
        *:if (spacing == null) {} else {tracking:spacing/15.0}}
}
pub fn paragraph_props(node, colors) {
    let spacing = c.child(node,"spacing")
    let indent = c.child(node,"ind")
    let alignment = {left:"left",right:"right",center:"center",both:"justify",start:"start",end:"end"}[c.attr(c.child(node,"jc"),"val")]
    let fill = theme.color(c.child(node,"shd"),colors,"fill","themeFill","themeFillShade","themeFillTint","transparent");
    {*:numeric(spacing,{before:"before",after:"after"},15.0),
        *:numeric(spacing,{line:"line"},1.0),
        *:numeric(indent,{left:"left",right:"right",first_line:"firstLine",hanging:"hanging"},15.0),
        *:if (c.attr(spacing,"lineRule") == null) {} else {line_rule:c.attr(spacing,"lineRule")},
        *:if (alignment == null) {} else {align:alignment},
        *:if (fill == null) {} else {background:fill}}
}
pub fn run_style(props, defaults = false) => declarations({
    ["font-family"]:if (props.font == null) null else quoted(props.font) ++ ", sans-serif",
    ["font-size"]:px(props.font_size),color:props.color,
    ["letter-spacing"]:px(props.tracking),
    ["font-weight"]:if (props.b == null and not defaults) null else if (props.b == true) "bold" else "normal",
    ["font-style"]:if (props.i == null and not defaults) null else if (props.i == true) "italic" else "normal",
    ["text-transform"]:if (props.caps == null) null else if (props.caps) "uppercase" else "none",
    ["font-variant"]:if (props.smallCaps == null) null else if (props.smallCaps) "small-caps" else "normal"})

pub fn paragraph_style(props, run, numbering) {
    let leading = if (props.line == null) null else if (props.line_rule == null or props.line_rule == "auto")
        string(props.line/240.0) else px(max(props.line/15.0,if (props.line_rule == "atLeast") c.get(run,"font_size",0.0) else 0.0));
    run_style(run,true) ++ declarations({
        ["margin-top"]:px(props.before),["margin-bottom"]:px(props.after),
        ["margin-left"]:px(if (numbering == null) props.left else 0.0),
        ["margin-right"]:px(props.right),["text-indent"]:px(if (numbering != null) null
            else if (props.first_line != null) props.first_line else if (props.hanging != null) 0.0-props.hanging else null),
        ["text-align"]:props.align,["line-height"]:leading,["background-color"]:props.background})
}

fn border(node, colors) {
    let kind = c.attr(node,"val")
    let css_kind = {single:"solid",double:"double",dotted:"dotted",dashed:"dashed",dashSmallGap:"dashed",dotDash:"dashed",dotDotDash:"dashed"}[kind];
    if (contains(["nil","none"],kind)) "none"
    else if (css_kind == null) null else
        px(max(0.0,c.pixels(c.attr(node,"sz"),6.0))) ++ " " ++ css_kind ++ " " ++ c.get({color:theme.color(node,colors,"color")},"color","#000000")
}
fn margins(node) => map([for (side in ["top","right","bottom","left"],
    let value = c.child(node,side),let size = integer_value(c.attr(value,"w"))
    where size != null and c.attr(value,"type") != "pct") ("padding_" ++ side,size/15.0)])
fn borders(node, colors) => map([for (side in ["top","right","bottom","left","insideH","insideV"],
    let value = border(c.child(node,side),colors) where value != null) ("border_" ++ side,value)])
fn width(node) {
    let value = integer_value(c.attr(node,"w"))
    let kind = c.attr(node,"type");
    if (value == null or value <= 0) null else if (kind == "pct") string(value/50.0) ++ "%"
    else if (kind == "dxa") px(value/15.0) else null
}
pub fn table_props(node, colors) {
    let fill = theme.color(c.child(node,"shd"),colors,"fill","themeFill","themeFillShade","themeFillTint","#ffffff");
    {*:borders(c.child(node,"tblBorders"),colors),*:margins(c.child(node,"tblCellMar")),
        *:if (fill == null) {} else {background:fill},
        *:if (width(c.child(node,"tblW")) == null) {} else {width:width(c.child(node,"tblW"))},
        *:if (c.attr(c.child(node,"tblLayout"),"type") == null) {} else {layout:c.attr(c.child(node,"tblLayout"),"type")},
        *:numeric(c.child(node,"tblInd"),{indent:"w"},15.0)}
}
pub fn cell_props(node, colors) {
    let fill = theme.color(c.child(node,"shd"),colors,"fill","themeFill","themeFillShade","themeFillTint","#ffffff")
    let align = {top:"top",center:"middle",bottom:"bottom"}[c.attr(c.child(node,"vAlign"),"val")];
    {*:borders(c.child(node,"tcBorders"),colors),*:margins(c.child(node,"tcMar")),
        *:if (fill == null) {} else {background:fill},*:if (align == null) {} else {align:align}}
}
pub fn table_style(props, grid) => declarations({
    width:c.get(props,"width",if (len(grid) > 0 and sum(grid) > 0) px(sum(grid)) else null),
    ["max-width"]:"100%",["border-collapse"]:"collapse",
    ["table-layout"]:if (props.layout == "fixed") "fixed" else null,
    ["background-color"]:props.background,["margin-left"]:px(props.indent)})

pub fn cell_style(table, cell, row, row_count, row_span, column, column_count, column_span) {
    let edges = {top:if (row == 0) "top" else "insideH",
        bottom:if (row+row_span >= row_count) "bottom" else "insideH",
        left:if (column == 0) "left" else "insideV",
        right:if (column+column_span >= column_count) "right" else "insideV"};
    declarations({*:map([for (side,edge in edges)
            ("border-" ++ string(side),c.get(cell,"border_" ++ string(side),table["border_" ++ edge]))]),
        *:map([for (side in ["top","right","bottom","left"])
            ("padding-" ++ side,px(c.get(cell,"padding_" ++ side,table["padding_" ++ side])))]),
        ["background-color"]:cell.background,["vertical-align"]:cell.align})
}

pub fn page_style(section, run) {
    let size = c.child(section,"pgSz")
    let margins = c.child(section,"pgMar")
    let dimensions = numeric(size,{width:"w",height:"h"},15.0)
    let spacing = numeric(margins,{top:"top",right:"right",bottom:"bottom",left:"left"},15.0);
    run_style(run,true) ++ declarations({width:px(dimensions.width),["min-height"]:px(dimensions.height),
        *:map([for (side,value in spacing) ("padding-" ++ string(side),px(value))])})
}
