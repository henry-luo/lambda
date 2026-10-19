// Public document math API: layout, source formatting and MathML conversion.
import typeset: .typeset
import mathml: .mathml
import mathml_layout: .mathml_layout
import writer: .format

pub fn to_latex(ast) => writer.to_latex(ast)
pub fn to_ascii(ast) => writer.to_ascii(ast)
pub fn format(ast, options = null) string^ => writer.format(ast, options)^

// The pure mathml module can also be imported directly by runtime-only hosts.
pub fn to_mathml(ast, options = null) element^ => mathml.to_mathml(ast, options)^
pub fn parse_mathml(source) element^ => mathml.parse_mathml(source)^

// Options: display, color, font_family, fonts [{font_family, data: binary}],
// font_size (CSS px, optional), base_uri (directory for relative images). Box dimensions remain in em.
pub fn render_box(ast, options = null) map | error {
    if (mathml.is_mathml(ast)) mathml_layout.render(ast, options)^
    else typeset.render(ast, options)^
}
pub fn render_math(ast, options = null) element | error { render_box(ast, options)^.element }
pub fn render_box_element(measured_box) { measured_box.element }
pub fn render_display(ast) { render_math(ast, {display: true})^ }
pub fn render_inline(ast) { render_math(ast, {display: false})^ }
pub fn render_standalone(ast) { render_math(ast, {display: true, standalone: true})^ }

// Generic commands the typesetter has no rendering for; it paints their names,
// so a caller must report them rather than count the output as success.
pub fn unmapped_commands(ast) => typeset.unmapped_commands(ast)
// symbols a \boldsymbol leaves at regular weight (no bundled bold glyph)
pub fn regular_bold_symbols(ast) array | error { typeset.regular_bold_symbols(ast)^ }

// Each SVG carries the font declarations needed by its text glyphs.
pub fn stylesheet(options = null) { "" }
