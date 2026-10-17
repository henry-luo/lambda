// test_math_phase5_packages.ls — math constructs from the Phase V M8 packages
// (vibe/impl/Lambda_Impl_Latex_Packages.md §12): mathtools colon relations and
// environments, bm, cancel's \cancelto, nicefrac/xfrac slanted fractions, and the
// bounded mhchem profile.
import math: lambda.doc.math.math
import sym: lambda.doc.math.symbols
import mhchem: lambda.doc.math.mhchem

fn ast(src) => parse(src, {type: "math"}) ^ { null }
fn html(src) => (format(math.render_inline(ast(src)) ^ { "ERR" }, 'html') ^ { null }) or "ERR"
fn has(text, part) => index_of(text, part) != null
// unmapped commands, and whether the formula renders
fn check(src) => {unmapped: math.unmapped_commands(ast(src)), renders: (math.render_box(ast(src)) ^ { null }) != null}

// ---- mathtools colon relations: composed glyphs, one relation atom ----
"colon symbols:"; [for (name in ["coloneqq", "Coloneqq", "eqqcolon", "dblcolon", "vcentcolon", "colonapprox"])
    sym.lookup_symbol(name)]
"colon class:"; [sym.classify_symbol("coloneqq")]
let colon = html("a \\coloneqq b")
// no glue between the colon and the equals sign: the = starts where the colon's advance ends
"colon atom:"; has(colon, "<g transform=\"translate(277 0)\"><text x=\"0\" y=\"0\" font-family=\"Computer Modern Serif\" font-size=\"1000\" font-weight=\"400\" font-style=\"normal\">=</text>")

// ---- bm ----
"bm:"; check("\\bm{x}\\hm{\\alpha}")
"bm bold:"; has(html("\\bm{x}"), "font-weight=\"700\" font-style=\"italic\">x")

// ---- cancel and slanted fractions ----
"cancelto:"; check("\\cancelto{0}{x^2}")
"cancelto kind:"; has(html("\\cancelto{0}{x}"), "data-math-kind=\"cancelto\"")
"slanted:"; [check("\\nicefrac{1}{2}"), check("\\sfrac{3}{4}"), check("\\sfrac{a}[:]{b}")]
"slanted kind:"; has(html("\\nicefrac{1}{2}"), "data-math-kind=\"slanted-fraction\"")

// ---- mathtools environments ----
"environments:"; [for (env in ["psmallmatrix", "bsmallmatrix", "Bsmallmatrix", "vsmallmatrix", "Vsmallmatrix",
    "drcases", "multlined", "lgathered", "rgathered"]) check("\\begin{" ++ env ++ "}a\\\\b\\end{" ++ env ++ "}")]
"starred cases text column:"; [for (item in ast("\\begin{dcases*}x & if $x>0$\\\\ 0 & otherwise\\end{dcases*}")[0].body
    where item is element) name(item)]
"unknown environment:"; math.unmapped_commands(ast("\\begin{weird}x\\end{weird}"))

// ---- mhchem: \ce notation to TeX math ----
"ce:"; [for (s in ["H2O", "2H2 + O2 -> 2H2O", "Na+(aq) + Cl-(aq)", "CrO4^2-", "[Cu(NH3)4]^2+",
    "^{227}_{90}Th", "CuSO4*5H2O", "CH2=CH2", "HC#CH", "A ->[H2O][heat] B", "N2 + 3H2 <=> 2NH3",
    "A <=>> B", "A <<=> B", "A <--> B", "CaCO3 v", "CO2 ^", "e-", "C_nH_{2n+2}", "1/2O2", "Fe^{II}",
    "$n$ H2O", "H2O(l)", "\\alpha-Al2O3"]) mhchem.equation(s)]
"pu:"; [for (s in ["123 kJ/mol", "1.2e3 kJ", "J mol-1", "6.02e-23 mol^-1", "25 m2", "5 m.s-2"]) mhchem.quantity(s)]
"chemistry renders:"; [check("\\ce{2H2 + O2 -> 2H2O}"), check("\\ce{A ->[H2O][heat] B}"), check("\\pu{1.2e3 kJ/mol}")]
"chemistry unmapped:"; math.unmapped_commands(ast("\\ce{A \\bond{-} B}"))
