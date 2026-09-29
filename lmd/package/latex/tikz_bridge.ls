// LaTeX owns figure context; the TikZ package owns picture semantics.
import tikz: lambda.doc.tikz.tikz

pub fn render_picture(island) {
    tikz.render_ast(island) ^ {
        <div class: "latex-tikz-unsupported",
            role: "img", 'aria-label': "Unsupported TikZ picture",
            <strong "TikZ picture could not be rendered: " ++ ^.message>
            <pre island.raw_source>
        >
    }
}
