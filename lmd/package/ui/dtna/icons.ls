import c: lambda.ui.core.component
import tokens: .tokens

// pinned, licensed SVG data is Input-owned; no mutable catalog or native widget state.
let assets = input(sys.lambda.home# ++ "/package/ui/dtna/icon_assets.json",'json')^
let aliases = {["chevron-down"]:"down",info:"info-circle",warning:"warning"}
fn icon_name(props) {
    let requested = c.text(c.option(props,"name","info"));
    c.option(aliases,requested,requested)
}
pub fn names() => [for (key,definitions in assets) c.text(key)]
pub fn variants() => [for (key,definitions in assets) for (theme,definition in definitions) {name:c.text(key),theme:c.text(theme)}]
pub fn validate(props) bool^ {
    let flags = c.boolean_props(props,["spin"],"icon")^
    let colors = c.validate_colors(props,["primary_color","secondary_color"],"icon")^
    let theme = c.text(c.option(props,"theme","outlined"))
    let key = icon_name(props);
    if (not c.enum_valid(props.theme,["outlined","filled","two-tone"])^) raise c.fail("icon","invalid theme")
    else if (not c.has(assets,key) or not c.has(assets[key],theme)) raise c.fail("icon","unsupported name/theme " ++ key ++ "/" ++ theme)
    else if (props.rotate != null and not c.finite(props.rotate)) raise c.fail("icon","rotate must be finite degrees")
    else if (props.primary_color != null and props.secondary_color == null and not tokens.color(props.primary_color))
        raise c.fail("icon","automatic secondary color requires #rrggbb; otherwise supply secondary_color")
    else if (any([for (field in ["width","height"] where props[field] != null) not c.css_length(props[field]) or (props[field] is number and props[field] == 0)]))
        raise c.fail("icon","dimensions must be positive numbers or nonnegative CSS lengths") else true
}
pub fn descriptor(props) element^ {
    let valid = validate(props)^;
    c.node('icon',props,null,["name","width","height","theme","rotate","spin","primary_color","secondary_color"])^
}
fn attributes(values, props) => map([for (key,value in values)
    (key,if (value == "__dtna_primary__") c.option(props,"primary_color","var(--dtna-primary)")
        else if (value == "__dtna_secondary__") (if (c.has(props,"secondary_color")) props.secondary_color
            else if (props.primary_color != null) (tokens.palette(props.primary_color)^)[0] else "var(--dtna-primary-background)") else value)])
fn shapes(nodes, props) => [for (node in nodes)
    if (node.tag == "path") <path *:attributes(node.attrs,props)>
    else <g *:attributes(node.attrs,props),*shapes(c.option(node,"children",[]),props)>]
pub fn render(props) element^ {
    let valid = validate(props)^
    let definition = assets[icon_name(props)][c.text(c.option(props,"theme","outlined"))];
    <svg *:definition.attrs,*:c.attrs(props),class:c.classes('icon',props,if (props.spin or icon_name(props) == "loading") "dtna-icon-spin" else ""),
        fill:"currentColor",width:c.option(props,"width","1em"),height:c.option(props,"height","1em"),
        style:(if (props.width == null) "" else "width:" ++ c.length(props.width) ++ ";") ++
            (if (props.height == null) "" else "height:" ++ c.length(props.height) ++ ";") ++
            (if (props.rotate == null) "" else "transform:rotate(" ++ string(props.rotate) ++ "deg);") ++ c.text(props.style),
        role:if (props.label == null) null else "img",["aria-label"]:props.label,["aria-hidden"]:if (props.label == null) "true" else null,
        *shapes(definition.children,props)>
}
pub let css = "
@keyframes dtna-icon-rotate{to{transform:rotate(360deg)}}
.dtna-icon-spin{animation:dtna-icon-rotate 1s linear infinite}
@media(prefers-reduced-motion:reduce){.dtna-icon-spin{animation:none}}
"
