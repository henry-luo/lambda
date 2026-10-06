// LaTeX owns figure context; the TikZ package owns picture semantics.
import tikz: lambda.doc.tikz.tikz
import util: .util

pub fn render_picture(island) {
    tikz.render_ast(island) ^ {
        let offset = island.source_offset;
        <div class: "latex-tikz-unsupported",
            role: "img", 'aria-label': "Unsupported TikZ picture",
            util.unsupported_element("tikz", "TikZ picture could not be rendered: " ++ ^.message,
                offset);
            <pre island.raw_source>
        >
    }
}
