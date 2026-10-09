// AMS commands share the selected-font math renderer, including symbol fallback.
import symbols: lambda.doc.math.symbols
import math: lambda.doc.math.math

pub fn render_symbol(command) {
    let glyph = symbols.lookup_symbol(command)
    if (glyph == null) null
    else math.render_inline(parse("\\" ++ command, 'math')^)
}
