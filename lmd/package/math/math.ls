// Public font-driven math API. Math5 Phases 11–12.
import typeset: .typeset

// Options: display, color, font_family, fonts [{font_family, data: binary}],
// font_size (CSS px, optional), base_uri (directory for relative images). Box dimensions remain in em.
pub fn render_box(ast, options = null) map | error { typeset.render(ast, options)^ }
pub fn render_math(ast, options = null) element | error { render_box(ast, options)^.element }
pub fn render_box_element(measured_box) { measured_box.element }
pub fn render_display(ast) { render_math(ast, {display: true})^ }
pub fn render_inline(ast) { render_math(ast, {display: false})^ }
pub fn render_standalone(ast) { render_math(ast, {display: true, standalone: true})^ }

// Each SVG carries the font declarations needed by its text glyphs.
pub fn stylesheet(options = null) { "" }
