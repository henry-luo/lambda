// Normalized glyphs shared by photo overlays and inline document artwork.
pub let kinds = ["arrow","heart","star"]
pub fn glyph(kind) =>
    if (kind=="arrow") <path d:"M4 36 H58 V8 L98 50 L58 92 V64 H4 Z">
    else if (kind=="heart") <path d:"M50 92 C35 78 4 56 4 30 C4 3 38 0 50 23 C62 0 96 3 96 30 C96 56 65 78 50 92 Z">
    else if (kind=="star") <path d:"M50 3 L62 35 L97 36 L70 58 L80 93 L50 73 L20 93 L30 58 L3 36 L38 35 Z">
    else null
