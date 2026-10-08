import radiant

fn close(a, b) => abs(a - b) < 0.001

// fixture advances/bounds are fixed in font units, independent of installed fonts.
let bytes = input("test/ui/svg_font_assets/rectangle.ttf", 'binary')^
let faces = [{font_family: "MetricSnapshot", data: bytes}]
let font = {font_family: "MetricSnapshot", font_size: 20}
let requests = [for (text in ["AA", "", "  "]) {text: text, font: font}]
let native = radiant.measure_text(requests, faces)
let html = "<html><body><svg><style>@font-face{font-family:MetricSnapshot;src:url('test/ui/svg_font_assets/rectangle.ttf')}</style><text font-family='MetricSnapshot' font-size='20'>AA</text></svg></body></html>"
let svg = radiant.measure_svg_text(html, 100, 100)
let face = radiant.font_metrics(font, faces)
let large = radiant.measure_text([{text: "AA", font: {*:font, font_size: 40}}], faces)
let spaced = radiant.measure_text([{text: "A A", font: {*:font, letter_spacing: 2, word_spacing: 3}}], faces)
let unspaced = radiant.measure_text([{text: "A A", font: font}], faces)
let checks = [
    {name: "font bytes own measurement", ok: close(native[0].advance, 40)},
    {name: "resolved font identity", ok: native[0].resolved_fonts == [face.font_family]},
    {name: "same SVG advance", ok: close(native[0].width, svg[0].width)},
    {name: "same SVG bounds", ok: close(native[0].top, svg[0].top) and close(native[0].right, svg[0].right)},
    {name: "ink is distinct from line metrics", ok: native[0].ink != null and native[0].logical != null},
    {name: "empty text", ok: native[1].width == 0 and native[1].height == 0 and native[1].ink == null},
    {name: "spaces retain advance without ink", ok: native[2].width > 0 and native[2].ink == null},
    {name: "size scaling", ok: close(large[0].width, native[0].width * 2)},
    {name: "shared spacing", ok: close(spaced[0].width - unspaced[0].width, 9)},
    {name: "font metrics", ok: face.ascent > 0 and face.units_per_em == 1000 and face.font_size == 20},
    {name: "invalid font size", ok: radiant.measure_text([{text: "a", font: {*:font, font_size: 0}}], faces) == null},
    {name: "invalid font snapshot", ok: radiant.font_metrics(font, [{font_family: "MetricSnapshot", data: b'\x000102'}]) == null},
    {name: "empty batch", ok: radiant.measure_text([], faces) == []},
    {name: "SVG queries skip text siblings", ok: len(radiant.measure_svg_text(
        "<html><body>before<svg>before<text>A</text>after</svg>after</body></html>", 100, 100)) == 1}
];
[for (check in checks where check.ok != true) check.name]
