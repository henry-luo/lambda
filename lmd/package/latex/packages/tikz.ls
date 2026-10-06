// Preserve the existing TikZ script engine as the sole drawing implementation.
import bridge: ~~.tikz_bridge

pub fn render_picture(island) => bridge.render_picture(island)
