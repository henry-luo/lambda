// Reuse distributed CMU/KaTeX faces; this is resource selection, not geometry.
// Binary acquisition stays in Lambda IO (D7.1.2v2).
pub let FAMILY = "Computer Modern Serif"
// CM math symbols must resolve before platform fallback (plain.tex family 2).
pub let SYMBOL_FAMILIES = "Computer Modern Serif,KaTeX_Size1,KaTeX_AMS,KaTeX_Main"
pub let VARIANT_FAMILIES = {sans: "Computer Modern Sans", mono: "Computer Modern Typewriter",
    script: "KaTeX_Script", cal: "KaTeX_Caligraphic", fraktur: "KaTeX_Fraktur", double: "KaTeX_AMS"}

let resources = [
    {family: FAMILY, file: "latex/fonts/Serif/cmunrm.woff2"},
    {family: FAMILY, file: "latex/fonts/Serif/cmunti.woff2", style: "italic"},
    {family: FAMILY, file: "latex/fonts/Serif/cmunbx.woff2", weight: 700},
    {family: FAMILY, file: "latex/fonts/Serif/cmunbi.woff2", weight: 700, style: "italic"},
    {family: VARIANT_FAMILIES.sans, file: "latex/fonts/Sans/cmunss.woff2"},
    {family: VARIANT_FAMILIES.mono, file: "latex/fonts/Typewriter/cmuntt.woff2"},
    {family: "KaTeX_Size1", file: "math/fonts/KaTeX_Size1-Regular.woff2"},
    {family: "KaTeX_Size2", file: "math/fonts/KaTeX_Size2-Regular.woff2"},
    {family: "KaTeX_AMS", file: "math/fonts/KaTeX_AMS-Regular.woff2"},
    {family: "KaTeX_Main", file: "math/fonts/KaTeX_Main-Regular.woff2"},
    {family: VARIANT_FAMILIES.script, file: "math/fonts/KaTeX_Script-Regular.woff2"},
    {family: VARIANT_FAMILIES.cal, file: "math/fonts/KaTeX_Caligraphic-Regular.woff2"},
    {family: VARIANT_FAMILIES.fraktur, file: "math/fonts/KaTeX_Fraktur-Regular.woff2"}
]

pub fn faces() array | error {
    [for (face in resources) {font_family: face.family,
        font_weight: face.weight or 400, font_style: face.style or "normal",
        data: input(sys.lambda.home# ++ "/package/" ++ face.file, 'binary')^}]
}
