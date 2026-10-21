// D5.3.3: retain score, glyph/text metrics and SVG owners across allocations.
// The full reflow/A4/option matrix lives in music_layout.ls; one small score
// reaches the same allocating package boundaries in the forced-GC sweep.
import music: lambda.music
import output: lambda.music.svg

let score = music.load("test/music/study.musicxml")^
let sheet = music.layout(score, {width:620.0, mode:'pages', page_height:850.0})^
let svg = music.svg(sheet)^
let document = output.document(sheet)^
let xml = format(svg, 'xml')^;
[
    score.title == "Morning Study",
    len(score.events) == 12,
    len(sheet.events) == len(score.events),
    len(sheet.pages) > 0,
    len(sheet.profile.glyphs) > 0,
    all([for (id,e in sheet.events)
        e.bounds.right >= e.bounds.left and e.bounds.bottom >= e.bounds.top]),
    name(svg) == 'svg',
    name(document) == 'div',
    contains(xml, "data-event-id"),
    contains(xml, "@font-face"),
    contains(xml, "Morning Study"),
    len(sheet.diagnostics) == 0
]
