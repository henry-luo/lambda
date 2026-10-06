// AMS glyphs use the existing Lambda-script math symbol and font tables.
import symbols: lambda.doc.math.symbols

pub fn render_symbol(command) {
    let glyph = symbols.lookup_symbol(command)
    if (glyph == null) null
    else <span class: symbols.font_class_of(command), glyph>
}
