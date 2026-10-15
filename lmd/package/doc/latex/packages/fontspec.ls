// latex/packages/fontspec.ls — font families by name: fontspec's setters and
// \newfontfamily switches, polyglossia's script fonts, and the text font packages'
// faces (vibe/impl/Lambda_Impl_Latex_Packages.md §13). Lambda bundles the Computer
// Modern and Latin Modern faces only (§17.3): any other family is asked for by name
// in CSS with a generic fallback, and reported as a font substitution.

import util: ~~.util
import color: ~~.elements.color

pub let COMMANDS = ["setmainfont", "setsansfont", "setmonofont", "newfontfamily", "renewfontfamily",
    "setfontfamily", "newfontface", "renewfontface", "fontspec", "addfontfeatures", "addfontfeature",
    "defaultfontfeatures"]
pub let PREAMBLE = ["setmainfont", "setsansfont", "setmonofont", "newfontfamily", "renewfontfamily",
    "setfontfamily", "newfontface", "renewfontface", "defaultfontfeatures"]
pub let OPTIONS = ["no-math", "no-config", "quiet", "silent", "math", "config"]

// the faces Lambda ships, under the names documents use for them
let BUNDLED = ["Latin Modern Roman", "Latin Modern Sans", "Latin Modern Mono", "CMU Serif",
    "CMU Sans Serif", "CMU Typewriter Text", "Computer Modern Serif", "Computer Modern Sans",
    "Computer Modern Typewriter", "lmroman10-regular", "lmsans10-regular", "lmmono10-regular"]
pub fn bundled(font) => contains(BUNDLED, font) or false

// unicode-math: the math faces that look like the bundled Computer Modern math
let BUNDLED_MATH = ["Latin Modern Math", "New Computer Modern Math", "NewCMMath-Regular", "latinmodern-math"]
pub let MATH_COMMANDS = ["setmathfont", "setmathfontface", "unimathsetup", "setoperatorfont"]
pub let MATH_PREAMBLE = MATH_COMMANDS

// the family each setter replaces, and each family's generic fallback
let ROLES = {setmainfont: "main", setsansfont: "sans", setmonofont: "mono", setmathfont: "math"}
let GENERIC = {main: "serif", sans: "sans-serif", mono: "monospace", family: "serif"}

// a family's CSS stack: the requested face, then the generic family
pub fn family_css(font, role) => "'" ++ replace(font, "'", "") ++ "'," ++ (GENERIC[role] or "serif")

// A family declaration as {role, command, font, features, offset}: \setmainfont[f]{Name}
// and its sans and mono siblings set a role; \newfontfamily\cmd[f]{Name} (and
// \setfontfamily, \newfontface) makes a switch. Polyglossia documents also write
// the switch's name as the command's next element, which `following` carries.
fn declaration(node, following) {
    if (not (node is element)) null
    else {
        let tag = string(name(node))
        let role = ROLES[tag]
        if (role != null) {
            let font = util.raw_argument(node, "required", 0)
            if (font == null) null
            else {role: role, command: null, font: trim(font), features: util.optional_raw(node),
                offset: node.source_offset}
        }
        else if (tag == "newfontfamily" or tag == "renewfontfamily" or tag == "setfontfamily" or
            tag == "newfontface" or tag == "renewfontface") {
            let braced_font = util.raw_argument(node, "required", 1)
            let braced_name = util.raw_argument(node, "required", 0)
            let target = if (braced_font != null) braced_name
                else if (following is element) "\\" ++ string(name(following)) else null
            let font = if (braced_font != null) braced_font
                else if (following is element) util.raw_argument(following, "required", 0) else null
            let features = if (braced_font != null) util.optional_raw(node)
                else if (following is element) util.optional_raw(following) else null
            if (target == null or font == null or not starts_with(trim(target), "\\")) null
            else {role: "family", command: slice(trim(target), 1, len(trim(target))), font: trim(font),
                features: features, offset: node.source_offset}
        }
        else null
    }
}

fn declarations_in(node, at) {
    if (at >= len(node)) []
    else {
        let child = node[at]
        let following = if (at + 1 < len(node)) node[at + 1] else null
        let found = declaration(child, following);
        (if (found == null) [] else [found]) ++
        (if (child is element) declarations_in(child, 0) else []) ++
        declarations_in(node, at + 1)
    }
}

// every family declaration of a document, in source order
pub fn declarations(ast) => if (ast is element) declarations_in(ast, 0) else []

// the declared switch named `command`, or null
pub fn switch(families, command) {
    let found = [for (f in families or [] where f.command == command) f];
    (if (len(found) == 0) null else found[len(found) - 1])
}

// the last declaration for a role, or null
pub fn role_declaration(families, role) {
    let found = [for (f in families or [] where f.role == role) f];
    (if (len(found) == 0) null else found[len(found) - 1])
}

// ---- features ---------------------------------------------------------------------

let LIGATURES = {TeX: "", Common: "", Required: "", Contextual: "", NoCommon: "font-variant-ligatures:no-common-ligatures",
    Rare: "font-variant-ligatures:discretionary-ligatures", Discretionary: "font-variant-ligatures:discretionary-ligatures",
    Historic: "font-variant-ligatures:historical-ligatures"}
let NUMBERS = {OldStyle: "oldstyle-nums", Lowercase: "oldstyle-nums", Lining: "lining-nums", Uppercase: "lining-nums",
    Proportional: "proportional-nums", Monospaced: "tabular-nums", SlashedZero: "slashed-zero"}
// features that select shaping or a font's encoding mapping: the browser shapes text
// by its script, so they need no CSS
let NEUTRAL = ["Script", "Language", "Mapping", "Renderer"]

fn values(raw) => [for (part in util.split_top_level(replace(replace(trim(raw), "{", ""), "}", ""), ",")) trim(part)]

// The CSS of a feature list: Ligatures, Numbers, Scale (a number), Color, and
// Letters=SmallCaps; the other features are listed in `unknown`
pub fn features(raw, colors) {
    let keys = util.parse_kv_options(raw)
    let parts = [for (key, value at keys) feature(string(key), string(value), colors)];
    {css: join([for (p in parts where p.css != null and p.css != "") p.css ++ ";"], ""),
     unknown: [for (p in parts where p.css == null) p.key]}
}

fn feature(key, value, colors) {
    let css = if (key == "Ligatures")
            (let picked = [for (v in values(value)) LIGATURES[v]],
             if (any([for (p in picked) p == null])) null else join([for (p in picked where p != "") p], ";"))
        else if (key == "Numbers")
            (let picked = [for (v in values(value)) NUMBERS[v]],
             if (any([for (p in picked) p == null])) null else "font-variant-numeric:" ++ join(picked, " "))
        else if (key == "Scale") (let factor = float(trim(value)) ^ { null },
            if (factor == null) null else "font-size:" ++ string(factor) ++ "em")
        else if (key == "Color") (let css_color = color.spec_color("HTML", trim(value), colors),
            if (css_color == null) null else "color:" ++ css_color)
        else if (key == "Letters" and trim(value) == "SmallCaps") "font-variant:small-caps"
        else if (contains(NEUTRAL, key) or false) ""
        else null;
    {key: key, css: css}
}

// the diagnostics of a document's declarations: a face Lambda does not ship (math
// keeps its bundled faces), and features with no CSS
pub fn issues(families, colors) {
    [for (f in families, issue in
        [*(if (f.role == "math") (if (contains(BUNDLED_MATH, f.font) or false) []
            else [util.diagnostic("font-substitution", "unicode-math", f.font,
                f.font ++ " is not bundled; math keeps the bundled Computer Modern faces", f.offset)])
          else if (bundled(f.font)) [] else [util.diagnostic("font-substitution", "fontspec", f.font,
            f.font ++ " is not bundled; the CSS asks for it by name with a generic fallback", f.offset)]),
         *[for (key in features(f.features, colors).unknown) util.diagnostic("unsupported-font-feature",
            "fontspec", key, "Unsupported fontspec feature: " ++ key, f.offset)]]) issue]
}

// the CSS of a family switch or of \fontspec in text: the family and its features
pub fn switch_css(font, raw_features, colors) =>
    "font-family:" ++ family_css(font, "family") ++ ";" ++ features(raw_features, colors).css
