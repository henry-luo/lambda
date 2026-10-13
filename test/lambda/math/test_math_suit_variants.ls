// stix's \var...suit commands paint their own card-suit glyphs, not the command name.
import math: lambda.doc.math.math
import snapshot: .mod_svg_snapshot

let suits = [
    {source: "\\varheartsuit", glyph: "♥"}, {source: "\\vardiamondsuit", glyph: "♦"},
    {source: "\\varspadesuit", glyph: "♤"}, {source: "\\varclubsuit", glyph: "♧"},
    {source: "\\heartsuit", glyph: "♡"}, {source: "\\spadesuit", glyph: "♠"}
];
[for (suit in suits) {
    let box = math.render_box(parse(suit.source, 'math')^)^;
    [suit.source, snapshot.painted_text(box.element) == suit.glyph, box.type]
}]
